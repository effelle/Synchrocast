// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "synchrocast_component.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>

#if defined(USE_ESP32)
#include <esp_random.h>
#elif defined(USE_ESP8266)
#include <Arduino.h>
static uint32_t esp_random() { return static_cast<uint32_t>(::random()); }
#endif

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast";

void SynchrocastComponent::setup() {
  this->boot_id_ = esp_random();
  if (this->boot_id_ == 0) {
    this->boot_id_ = 1;
  }
  this->transport_runtime_.set_sink(this);
#ifdef USE_SYNCHROCAST_STANDALONE_TRANSPORT
  auto &standalone = global_synchrocast_standalone_transport();
  if (!standalone.configure(this->requested_transport_,
                            this->requested_udp_port_)) {
    ESP_LOGE(TAG, "Standalone transport setup failed for group=0x%08" PRIX32,
             this->group_hash_);
    this->mark_failed();
    return;
  }
  this->transport_runtime_.set_standalone_backend(&standalone);
#endif
#ifdef USE_SYNCHROCAST_CFX_SYNC_BRIDGE
  this->transport_runtime_.set_cfx_sync_backend(&this->cfx_sync_adapter_);
#endif

  if (!this->transport_runtime_.configure(
          this->transport_owner_, this->requested_transport_,
          this->requested_udp_port_)) {
    ESP_LOGE(TAG, "Transport arbitration failed for group=0x%08" PRIX32,
             this->group_hash_);
    this->mark_failed();
    return;
  }
  this->log_transport_transition_(this->transport_runtime_.status().state);
}

void SynchrocastComponent::loop() {
  this->transport_runtime_.refresh();
  const auto state = this->transport_runtime_.status().state;
  this->log_transport_transition_(state);
  if (state == SynchrocastTransportState::BLOCKED) {
    this->mark_failed();
    return;
  }
  this->dispatcher_.loop();
  if (state == SynchrocastTransportState::ATTACHED_TO_CFX_SYNC ||
      state == SynchrocastTransportState::STANDALONE_ACTIVE) {
    const uint32_t now = millis();
    const bool heartbeat_due =
        this->last_heartbeat_ms_ == 0 ||
        now - this->last_heartbeat_ms_ >= this->heartbeat_interval_ms_;
    const bool retry_due =
        this->last_heartbeat_attempt_ms_ == 0 ||
        now - this->last_heartbeat_attempt_ms_ >= 5000;
    if (heartbeat_due && retry_due) {
      this->send_heartbeat_();
    }
  }
  this->maybe_log_stats_();
}

void SynchrocastComponent::dump_config() {
  const auto status = this->transport_runtime_.status();
  ESP_LOGCONFIG(TAG, "Synchrocast:");
  ESP_LOGCONFIG(TAG, "  Role: %s", role_to_string_(this->role_));
  ESP_LOGCONFIG(TAG, "  Group hash: 0x%08" PRIX32, this->group_hash_);
  ESP_LOGCONFIG(TAG, "  Transport owner: %s",
                synchrocast_transport_owner_to_string(status.owner));
  ESP_LOGCONFIG(TAG, "  Transport state: %s",
                synchrocast_transport_state_to_string(status.state));
  if (status.udp_port != 0) {
    ESP_LOGCONFIG(TAG, "  UDP port: %u",
                  static_cast<unsigned>(status.udp_port));
  }
  ESP_LOGCONFIG(TAG, "  Heartbeat interval: %" PRIu32 " ms",
                this->heartbeat_interval_ms_);
  ESP_LOGCONFIG(TAG,
                "  Authenticated codec: v%u, max frame=%u bytes, tag=%u bytes",
                static_cast<unsigned>(SynchrocastPacketCodec::VERSION),
                static_cast<unsigned>(SynchrocastPacketCodec::MAX_FRAME_SIZE),
                static_cast<unsigned>(SynchrocastPacketCodec::AUTH_TAG_SIZE));
  this->dispatcher_.dump_config();
}

bool SynchrocastComponent::on_transport_packet(
    const SynchrocastTransportSource &source, const uint8_t *data,
    size_t size) {
  this->shared_frames_received_++;
  SynchrocastPacket packet;
  const auto result = SynchrocastPacketCodec::decode(
      data, size, this->group_hash_, this->key_, packet);
  if (result == SynchrocastDecodeResult::NOT_SYNCHROCAST ||
      result == SynchrocastDecodeResult::WRONG_GROUP) {
    return false;
  }

  this->shared_frames_claimed_++;
  if (result != SynchrocastDecodeResult::OK) {
    if (result == SynchrocastDecodeResult::BAD_AUTH) {
      this->authentication_failures_++;
    } else {
      this->malformed_packets_++;
    }
    ESP_LOGV(TAG,
             "Rejected frame: reason=%s transport=%u path=%u bytes=%u "
             "group=0x%08" PRIX32,
             decode_result_to_string_(result),
             static_cast<unsigned>(source.transport),
             static_cast<unsigned>(source.receive_path),
             static_cast<unsigned>(size), this->group_hash_);
    return true;
  }

  const uint32_t sequence =
      (static_cast<uint32_t>(data[20]) << 24) |
      (static_cast<uint32_t>(data[21]) << 16) |
      (static_cast<uint32_t>(data[22]) << 8) |
      static_cast<uint32_t>(data[23]);
  if (!this->role_allows_message_(packet.source_role, packet.msg_type)) {
    this->role_rejections_++;
    ESP_LOGV(TAG, "Rejected role=%s type=%u boot=0x%08" PRIX32,
             role_to_string_(packet.source_role),
             static_cast<unsigned>(packet.msg_type), packet.source_boot_id);
    return true;
  }
  if (!this->accept_sequence_(packet.source_boot_id, sequence)) {
    this->replayed_packets_++;
    ESP_LOGV(TAG,
             "Rejected duplicate/stale frame boot=0x%08" PRIX32
             " sequence=%" PRIu32,
             packet.source_boot_id, sequence);
    return true;
  }

  const auto queued = this->dispatcher_.enqueue_packet(packet);
  if (queued == SynchrocastEnqueueResult::QUEUE_FULL ||
      queued == SynchrocastEnqueueResult::INVALID_PACKET) {
    this->enqueue_failures_++;
  } else {
    this->authenticated_packets_++;
  }
  return true;
}

void SynchrocastComponent::on_transport_recovered(uint32_t generation) {
  this->transport_recoveries_++;
  this->last_heartbeat_ms_ = 0;
  this->dispatcher_.on_transport_recovered();
  ESP_LOGV(TAG,
           "Transport recovered generation=%" PRIu32
           "; canonical state refresh scheduled",
           generation);
}

bool SynchrocastComponent::send_packet(const SynchrocastPacket &packet) {
  if (!this->role_allows_message_(this->role_, packet.msg_type)) {
    ESP_LOGV(TAG, "Local role=%s cannot send message type=%u",
             role_to_string_(this->role_),
             static_cast<unsigned>(packet.msg_type));
    return false;
  }

  SynchrocastPacket outbound = packet;
  outbound.source_role = this->role_;
  const uint32_t sequence = this->next_sequence_();
  std::array<uint8_t, SynchrocastPacketCodec::MAX_FRAME_SIZE> frame{};
  size_t frame_size = 0;
  if (!SynchrocastPacketCodec::encode(outbound, this->group_hash_,
                                      this->boot_id_, sequence, this->key_,
                                      frame, frame_size)) {
    ESP_LOGV(TAG, "Could not encode local packet type=%u domain=%u",
             static_cast<unsigned>(packet.msg_type),
             static_cast<unsigned>(packet.domain));
    return false;
  }

  const auto status = this->transport_runtime_.status();
  bool sent = false;
  if (this->requested_transport_ != SynchrocastRequestedTransport::UDP &&
      (status.active_transports & SYNCHROCAST_TRANSPORT_ESPNOW) != 0) {
    if (this->transport_runtime_.send_broadcast(
            SynchrocastTransportKind::ESPNOW, frame.data(), frame_size)) {
      sent = true;
      this->physical_frames_sent_++;
    }
  }
  if (this->requested_transport_ != SynchrocastRequestedTransport::ESPNOW &&
      (status.active_transports & SYNCHROCAST_TRANSPORT_UDP) != 0) {
    if (this->transport_runtime_.send_broadcast(
            SynchrocastTransportKind::UDP, frame.data(), frame_size)) {
      sent = true;
      this->physical_frames_sent_++;
    }
  }

  if (sent) {
    this->packets_sent_++;
  } else {
    this->send_failures_++;
  }
  return sent;
}

const char *SynchrocastComponent::role_to_string_(SynchrocastRole role) {
  switch (role) {
    case SynchrocastRole::LEADER:
      return "leader";
    case SynchrocastRole::CONTROLLER:
      return "controller";
    case SynchrocastRole::SATELLITE:
      return "satellite";
    default:
      return "follower";
  }
}

const char *SynchrocastComponent::decode_result_to_string_(
    SynchrocastDecodeResult result) {
  switch (result) {
    case SynchrocastDecodeResult::BAD_AUTH:
      return "authentication failed";
    case SynchrocastDecodeResult::MALFORMED:
      return "malformed";
    case SynchrocastDecodeResult::UNSUPPORTED_VERSION:
      return "unsupported version";
    case SynchrocastDecodeResult::UNSUPPORTED_TYPE:
      return "unsupported type";
    case SynchrocastDecodeResult::WRONG_GROUP:
      return "wrong group";
    case SynchrocastDecodeResult::NOT_SYNCHROCAST:
      return "not Synchrocast";
    default:
      return "ok";
  }
}

bool SynchrocastComponent::role_allows_message_(
    SynchrocastRole role, SynchrocastMessageType type) const {
  if (type == SynchrocastMessageType::HEARTBEAT) {
    return true;
  }
  if (type == SynchrocastMessageType::STATE_BROADCAST) {
    return role == SynchrocastRole::LEADER;
  }
  return role == SynchrocastRole::CONTROLLER ||
         role == SynchrocastRole::SATELLITE;
}

bool SynchrocastComponent::accept_sequence_(uint32_t boot_id,
                                            uint32_t sequence) {
  if (boot_id == 0 || sequence == 0 || boot_id == this->boot_id_) {
    return false;
  }

  ReplayState *available = nullptr;
  ReplayState *oldest = &this->replay_states_[0];
  for (auto &state : this->replay_states_) {
    if (state.active && state.boot_id == boot_id) {
      if (sequence <= state.last_sequence) {
        return false;
      }
      state.last_sequence = sequence;
      state.last_seen_ms = millis();
      return true;
    }
    if (!state.active && available == nullptr) {
      available = &state;
    }
    if (state.last_seen_ms < oldest->last_seen_ms) {
      oldest = &state;
    }
  }

  auto *state = available != nullptr ? available : oldest;
  state->active = true;
  state->boot_id = boot_id;
  state->last_sequence = sequence;
  state->last_seen_ms = millis();
  return true;
}

uint32_t SynchrocastComponent::next_sequence_() {
  this->tx_sequence_++;
  if (this->tx_sequence_ == 0) {
    this->boot_id_ = esp_random();
    if (this->boot_id_ == 0) {
      this->boot_id_ = 1;
    }
    this->tx_sequence_ = 1;
  }
  return this->tx_sequence_;
}

void SynchrocastComponent::send_heartbeat_() {
  SynchrocastPacket packet;
  packet.msg_type = SynchrocastMessageType::HEARTBEAT;
  this->last_heartbeat_attempt_ms_ = millis();
  if (this->send_packet(packet)) {
    this->last_heartbeat_ms_ = millis();
    ESP_LOGV(TAG, "Heartbeat sent boot=0x%08" PRIX32, this->boot_id_);
  }
}

void SynchrocastComponent::maybe_log_stats_() {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  const uint32_t now = millis();
  if (this->last_stats_log_ms_ != 0 &&
      now - this->last_stats_log_ms_ < 60000) {
    return;
  }
  this->last_stats_log_ms_ = now;
  ESP_LOGV(TAG,
           "Stats tx_packets=%" PRIu32 " tx_frames=%" PRIu32
           " tx_failed=%" PRIu32 " rx_frames=%" PRIu32
           " rx_claimed=%" PRIu32 " rx_authenticated=%" PRIu32
           " malformed=%" PRIu32 " auth_failed=%" PRIu32
           " replayed=%" PRIu32 " role_rejected=%" PRIu32
           " enqueue_failed=%" PRIu32 " recoveries=%" PRIu32,
           this->packets_sent_, this->physical_frames_sent_,
           this->send_failures_, this->shared_frames_received_,
           this->shared_frames_claimed_, this->authenticated_packets_,
           this->malformed_packets_, this->authentication_failures_,
           this->replayed_packets_, this->role_rejections_,
           this->enqueue_failures_, this->transport_recoveries_);
  this->dispatcher_.log_stats();
#endif
}

void SynchrocastComponent::log_transport_transition_(
    SynchrocastTransportState state) {
  if (state == this->last_logged_transport_state_) {
    return;
  }
  this->last_logged_transport_state_ = state;
  const auto status = this->transport_runtime_.status();
  ESP_LOGI(TAG, "Transport state=%s owner=%s active=0x%02X udp_port=%u",
           synchrocast_transport_state_to_string(state),
           synchrocast_transport_owner_to_string(status.owner),
           static_cast<unsigned>(status.active_transports),
           static_cast<unsigned>(status.udp_port));
}

}  // namespace synchrocast
}  // namespace esphome
