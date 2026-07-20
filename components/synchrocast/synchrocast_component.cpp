// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "synchrocast_component.h"

#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast";

void SynchrocastComponent::setup() {
  this->transport_runtime_.set_sink(this);
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
    ESP_LOGCONFIG(TAG, "  Shared UDP port: %u",
                  static_cast<unsigned>(status.udp_port));
  }
  ESP_LOGCONFIG(TAG, "  Heartbeat interval: %" PRIu32 " ms",
                this->heartbeat_interval_ms_);
  this->dispatcher_.dump_config();
}

bool SynchrocastComponent::on_transport_packet(
    const SynchrocastTransportSource &source, const uint8_t *data,
    size_t size) {
  this->shared_frames_received_++;
  if (this->frame_handler_ == nullptr) {
    ESP_LOGV(TAG,
             "Shared frame left unclaimed: transport=%u path=%u bytes=%u "
             "group=0x%08" PRIX32,
             static_cast<unsigned>(source.transport),
             static_cast<unsigned>(source.receive_path),
             static_cast<unsigned>(size), this->group_hash_);
    return false;
  }

  const bool handled = this->frame_handler_->handle_transport_frame(
      source, data, size, this->dispatcher_);
  if (handled) {
    this->shared_frames_claimed_++;
  }
  return handled;
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
