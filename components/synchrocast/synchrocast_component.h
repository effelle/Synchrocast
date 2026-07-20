// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "cfx_sync_transport_adapter.h"
#include "synchrocast_dispatcher.h"
#include "synchrocast_packet_codec.h"
#include "synchrocast_standalone_transport.h"
#include "synchrocast_transport_runtime.h"

#include "esphome/core/component.h"

#include <array>
#include <cstdint>

namespace esphome {
namespace synchrocast {

class SynchrocastComponent final : public Component,
                                   public SynchrocastTransportPacketSink {
 public:
  void set_role(SynchrocastRole role) { this->role_ = role; }
  void set_group_hash(uint32_t group_hash) { this->group_hash_ = group_hash; }
  void set_key(const std::array<uint8_t, 32> &key) { this->key_ = key; }
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
#if defined(USE_SYNCHROCAST_STANDALONE_TRANSPORT) && defined(USE_ESPNOW)
  void set_espnow(espnow::ESPNowComponent *espnow) {
    global_synchrocast_standalone_transport().set_espnow(espnow);
  }
#endif
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
  bool send_packet(const SynchrocastPacket &packet);

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::LATE - 2.0f;
  }

  bool on_transport_packet(const SynchrocastTransportSource &source,
                           const uint8_t *data, size_t size) override;
  void on_transport_recovered(uint32_t generation) override;

  bool publishes_canonical_state() const {
    return this->role_ == SynchrocastRole::LEADER;
  }
  bool applies_canonical_state() const {
    return this->role_ == SynchrocastRole::FOLLOWER ||
           this->role_ == SynchrocastRole::SATELLITE;
  }
  bool accepts_intent_requests() const {
    return this->role_ == SynchrocastRole::LEADER;
  }

  SynchrocastTransportStatus transport_status() const {
    return this->transport_runtime_.status();
  }

 protected:
  struct ReplayState {
    bool active{false};
    uint32_t boot_id{0};
    uint32_t last_sequence{0};
    uint32_t last_seen_ms{0};
  };

  static const char *role_to_string_(SynchrocastRole role);
  static const char *decode_result_to_string_(SynchrocastDecodeResult result);
  bool role_allows_message_(SynchrocastRole role,
                            SynchrocastMessageType type) const;
  bool accept_sequence_(uint32_t boot_id, uint32_t sequence);
  uint32_t next_sequence_();
  void send_heartbeat_();
  void maybe_log_stats_();
  void log_transport_transition_(SynchrocastTransportState state);

  SynchrocastDispatcher dispatcher_;
  SynchrocastTransportRuntime transport_runtime_;
#ifdef USE_SYNCHROCAST_CFX_SYNC_BRIDGE
  CFXSyncTransportAdapter cfx_sync_adapter_;
#endif
  std::array<uint8_t, 32> key_{};
  std::array<ReplayState, 8> replay_states_{};
  SynchrocastRole role_{SynchrocastRole::FOLLOWER};
  SynchrocastTransportOwner transport_owner_{
      SynchrocastTransportOwner::SYNCHROCAST};
  SynchrocastRequestedTransport requested_transport_{
      SynchrocastRequestedTransport::AUTO};
  SynchrocastTransportState last_logged_transport_state_{
      SynchrocastTransportState::UNCONFIGURED};
  uint32_t group_hash_{0};
  uint32_t boot_id_{0};
  uint32_t tx_sequence_{0};
  uint32_t heartbeat_interval_ms_{30000};
  uint32_t last_heartbeat_ms_{0};
  uint32_t last_heartbeat_attempt_ms_{0};
  uint32_t last_stats_log_ms_{0};
  uint32_t shared_frames_received_{0};
  uint32_t shared_frames_claimed_{0};
  uint32_t packets_sent_{0};
  uint32_t physical_frames_sent_{0};
  uint32_t send_failures_{0};
  uint32_t authenticated_packets_{0};
  uint32_t malformed_packets_{0};
  uint32_t authentication_failures_{0};
  uint32_t replayed_packets_{0};
  uint32_t role_rejections_{0};
  uint32_t enqueue_failures_{0};
  uint32_t transport_recoveries_{0};
  uint16_t requested_udp_port_{0};
};

}  // namespace synchrocast
}  // namespace esphome
