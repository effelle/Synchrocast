// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "synchrocast_component.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast";
static constexpr uint32_t STATE_OWNER_STALE_AFTER_MS = 180000;
static constexpr uint8_t STATE_REQUEST_ATTEMPTS = 3;
static constexpr uint32_t STATE_REQUEST_RETRY_MS[STATE_REQUEST_ATTEMPTS - 1] = {
    1000, 3000};

void SynchrocastComponent::setup() {
  get_mac_address_raw(this->node_id_.data());
  if (synchrocast_node_id_is_zero(this->node_id_)) {
    ESP_LOGE(TAG, "Could not establish stable hardware node identity");
    this->mark_failed();
    return;
  }
  this->reset_boot_id_();
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
  if (this->applies_canonical_state()) {
    this->schedule_state_requests_("startup");
  }
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
    this->maybe_send_state_request_();
  }
  this->maybe_log_stats_();
}

void SynchrocastComponent::dump_config() {
  const auto status = this->transport_runtime_.status();
  ESP_LOGCONFIG(TAG, "Synchrocast:");
  ESP_LOGCONFIG(TAG, "  Role: %s", role_to_string_(this->role_));
  ESP_LOGCONFIG(TAG, "  Group hash: 0x%08" PRIX32, this->group_hash_);
  char node_id[18];
  format_mac_addr_upper(this->node_id_.data(), node_id);
  ESP_LOGCONFIG(TAG, "  Stable node ID: %s", node_id);
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
  uint32_t sequence = 0;
  const auto result = SynchrocastPacketCodec::decode(
      data, size, this->group_hash_, this->key_, packet, sequence);
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

  if (!this->role_allows_message_(packet.source_role, packet.msg_type)) {
    this->role_rejections_++;
    ESP_LOGV(TAG, "Rejected role=%s type=%u boot=0x%08" PRIX32,
             role_to_string_(packet.source_role),
             static_cast<unsigned>(packet.msg_type), packet.source_boot_id);
    return true;
  }
  if (!this->accept_sequence_(packet.source_node_id, packet.source_boot_id,
                              sequence)) {
    this->replayed_packets_++;
    ESP_LOGV(TAG,
             "Rejected duplicate/stale frame boot=0x%08" PRIX32
             " sequence=%" PRIu32,
             packet.source_boot_id, sequence);
    return true;
  }

  if (packet.msg_type == SynchrocastMessageType::STATE_BROADCAST &&
      !this->accept_state_owner_(packet)) {
    return true;
  }
  if (packet.msg_type == SynchrocastMessageType::STATE_REQUEST &&
      this->role_ != SynchrocastRole::LEADER) {
    this->authenticated_packets_++;
    ESP_LOGV(TAG, "Ignored STATE_REQUEST on non-leader role=%s",
             role_to_string_(this->role_));
    return true;
  }

  char source_node[18];
  format_mac_addr_upper(packet.source_node_id.data(), source_node);
  ESP_LOGV(TAG,
           "Authenticated frame node=%s boot=0x%08" PRIX32
           " sequence=%" PRIu32 " type=%u",
           source_node, packet.source_boot_id, sequence,
           static_cast<unsigned>(packet.msg_type));

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
  if (this->applies_canonical_state()) {
    this->schedule_state_requests_("transport-recovery");
  }
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
                                      this->node_id_, this->boot_id_, sequence,
                                      this->key_,
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
  if (type == SynchrocastMessageType::STATE_REQUEST) {
    return role == SynchrocastRole::FOLLOWER ||
           role == SynchrocastRole::SATELLITE;
  }
  return role == SynchrocastRole::CONTROLLER ||
         role == SynchrocastRole::SATELLITE;
}

bool SynchrocastComponent::accept_sequence_(const SynchrocastNodeId &node_id,
                                            uint32_t boot_id,
                                            uint32_t sequence) {
  if (synchrocast_node_id_is_zero(node_id) || boot_id == 0 || sequence == 0 ||
      node_id == this->node_id_) {
    return false;
  }

  ReplayState *available = nullptr;
  ReplayState *oldest = &this->replay_states_[0];
  for (auto &state : this->replay_states_) {
    if (state.active && state.node_id == node_id &&
        state.boot_id == boot_id) {
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
  state->node_id = node_id;
  state->boot_id = boot_id;
  state->last_sequence = sequence;
  state->last_seen_ms = millis();
  return true;
}

bool SynchrocastComponent::accept_state_owner_(
    const SynchrocastPacket &packet) {
  const uint32_t now = millis();
  const bool no_owner =
      synchrocast_node_id_is_zero(this->state_owner_node_id_);
  const bool same_owner = this->state_owner_node_id_ == packet.source_node_id;
  const bool owner_stale = !no_owner &&
                           now - this->state_owner_last_seen_ms_ >=
                               STATE_OWNER_STALE_AFTER_MS;
  if (no_owner || same_owner || owner_stale) {
    if (no_owner || owner_stale) {
      char node_id[18];
      format_mac_addr_upper(packet.source_node_id.data(), node_id);
      ESP_LOGV(TAG, "%s canonical leader node=%s boot=0x%08" PRIX32,
               owner_stale ? "Replaced stale" : "Acquired", node_id,
               packet.source_boot_id);
    } else if (this->state_owner_boot_id_ != packet.source_boot_id) {
      ESP_LOGV(TAG,
               "Canonical leader restarted boot=0x%08" PRIX32
               " -> 0x%08" PRIX32,
               this->state_owner_boot_id_, packet.source_boot_id);
    }
    this->state_owner_node_id_ = packet.source_node_id;
    this->state_owner_boot_id_ = packet.source_boot_id;
    this->state_owner_last_seen_ms_ = now;
    return true;
  }

  this->state_owner_conflicts_++;
  if (this->last_state_conflict_log_ms_ == 0 ||
      now - this->last_state_conflict_log_ms_ >= 60000) {
    char owner[18];
    char contender[18];
    format_mac_addr_upper(this->state_owner_node_id_.data(), owner);
    format_mac_addr_upper(packet.source_node_id.data(), contender);
    ESP_LOGV(TAG,
             "Ignored competing canonical leader owner=%s contender=%s",
             owner, contender);
    this->last_state_conflict_log_ms_ = now;
  }
  return false;
}

void SynchrocastComponent::reset_boot_id_() {
  const uint32_t previous = this->boot_id_;
  this->boot_id_ = 0;
  if (!random_bytes(reinterpret_cast<uint8_t *>(&this->boot_id_),
                    sizeof(this->boot_id_)) ||
      this->boot_id_ == 0 || this->boot_id_ == previous) {
    do {
      this->boot_id_ = random_uint32();
    } while (this->boot_id_ == 0 || this->boot_id_ == previous);
  }
}

uint32_t SynchrocastComponent::next_sequence_() {
  this->tx_sequence_++;
  if (this->tx_sequence_ == 0) {
    this->reset_boot_id_();
    this->tx_sequence_ = 1;
  }
  return this->tx_sequence_;
}

void SynchrocastComponent::schedule_state_requests_(const char *reason) {
  this->state_request_active_ = true;
  this->state_request_attempt_ = 0;
  const uint16_t identity_tail =
      (static_cast<uint16_t>(this->node_id_[4]) << 8) | this->node_id_[5];
  this->next_state_request_ms_ = millis() + (identity_tail % 251);
  ESP_LOGV(TAG, "Scheduled authenticated STATE_REQUEST sequence after %s",
           reason);
}

void SynchrocastComponent::maybe_send_state_request_() {
  if (!this->state_request_active_ ||
      static_cast<int32_t>(millis() - this->next_state_request_ms_) < 0) {
    return;
  }
  SynchrocastPacket packet;
  packet.msg_type = SynchrocastMessageType::STATE_REQUEST;
  if (!this->send_packet(packet)) {
    this->next_state_request_ms_ = millis() + 1000;
    ESP_LOGV(TAG, "STATE_REQUEST send deferred; transport unavailable");
    return;
  }

  this->state_requests_sent_++;
  this->state_request_attempt_++;
  ESP_LOGV(TAG, "STATE_REQUEST sent attempt=%u/%u",
           static_cast<unsigned>(this->state_request_attempt_),
           static_cast<unsigned>(STATE_REQUEST_ATTEMPTS));
  if (this->state_request_attempt_ >= STATE_REQUEST_ATTEMPTS) {
    this->state_request_active_ = false;
    return;
  }
  this->next_state_request_ms_ =
      millis() + STATE_REQUEST_RETRY_MS[this->state_request_attempt_ - 1];
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
           " enqueue_failed=%" PRIu32 " recoveries=%" PRIu32
           " state_requests=%" PRIu32 " leader_conflicts=%" PRIu32,
           this->packets_sent_, this->physical_frames_sent_,
           this->send_failures_, this->shared_frames_received_,
           this->shared_frames_claimed_, this->authenticated_packets_,
           this->malformed_packets_, this->authentication_failures_,
           this->replayed_packets_, this->role_rejections_,
           this->enqueue_failures_, this->transport_recoveries_,
           this->state_requests_sent_, this->state_owner_conflicts_);
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
