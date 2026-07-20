// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "cfx_sync_transport_adapter.h"
#include "synchrocast_dispatcher.h"
#include "synchrocast_transport_runtime.h"

#include "esphome/core/component.h"

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace synchrocast {

enum class SynchrocastRole : uint8_t {
  LEADER = 0,
  FOLLOWER = 1,
  CONTROLLER = 2,
  SATELLITE = 3,
};

class SynchrocastTransportFrameHandler {
 public:
  virtual ~SynchrocastTransportFrameHandler() = default;
  virtual bool handle_transport_frame(
      const SynchrocastTransportSource &source, const uint8_t *data,
      size_t size, SynchrocastDispatcher &dispatcher) = 0;
};

class SynchrocastComponent final : public Component,
                                   public SynchrocastTransportPacketSink {
 public:
  void set_role(SynchrocastRole role) { this->role_ = role; }
  void set_group_hash(uint32_t group_hash) { this->group_hash_ = group_hash; }
  void set_transport_owner(SynchrocastTransportOwner owner) {
    this->transport_owner_ = owner;
  }
  void set_requested_transport(SynchrocastRequestedTransport transport) {
    this->requested_transport_ = transport;
  }
  void set_requested_udp_port(uint16_t port) {
    this->requested_udp_port_ = port;
  }
  void set_heartbeat_interval(uint32_t interval_ms) {
    this->heartbeat_interval_ms_ = interval_ms;
  }
  void set_frame_handler(SynchrocastTransportFrameHandler *handler) {
    this->frame_handler_ = handler;
  }

  bool register_domain_handler(SynchrocastDomainHandler *handler) {
    return this->dispatcher_.register_handler(handler);
  }
  SynchrocastEnqueueResult enqueue_packet(const SynchrocastPacket &packet) {
    return this->dispatcher_.enqueue_packet(packet);
  }
  bool send_transport_broadcast(SynchrocastTransportKind transport,
                                const uint8_t *data, size_t size) {
    return this->transport_runtime_.send_broadcast(transport, data, size);
  }
  bool send_transport_packet(
      const SynchrocastTransportSource &destination, const uint8_t *data,
      size_t size) {
    return this->transport_runtime_.send_to(destination, data, size);
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::LATE - 2.0f;
  }

  bool on_transport_packet(const SynchrocastTransportSource &source,
                           const uint8_t *data, size_t size) override;

  SynchrocastTransportStatus transport_status() const {
    return this->transport_runtime_.status();
  }

 protected:
  static const char *role_to_string_(SynchrocastRole role);
  void log_transport_transition_(SynchrocastTransportState state);

  SynchrocastDispatcher dispatcher_;
  SynchrocastTransportRuntime transport_runtime_;
#ifdef USE_SYNCHROCAST_CFX_SYNC_BRIDGE
  CFXSyncTransportAdapter cfx_sync_adapter_;
#endif
  SynchrocastTransportFrameHandler *frame_handler_{nullptr};
  SynchrocastRole role_{SynchrocastRole::FOLLOWER};
  SynchrocastTransportOwner transport_owner_{
      SynchrocastTransportOwner::SYNCHROCAST};
  SynchrocastRequestedTransport requested_transport_{
      SynchrocastRequestedTransport::AUTO};
  SynchrocastTransportState last_logged_transport_state_{
      SynchrocastTransportState::UNCONFIGURED};
  uint32_t group_hash_{0};
  uint32_t heartbeat_interval_ms_{30000};
  uint32_t shared_frames_received_{0};
  uint32_t shared_frames_claimed_{0};
  uint16_t requested_udp_port_{0};
};

}  // namespace synchrocast
}  // namespace esphome
