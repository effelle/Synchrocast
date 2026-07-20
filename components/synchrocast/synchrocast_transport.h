// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace synchrocast {

enum class SynchrocastRequestedTransport : uint8_t {
  AUTO = 0,
  ESPNOW = 1,
  UDP = 2,
};

enum class SynchrocastTransportOwner : uint8_t {
  NONE = 0,
  SYNCHROCAST = 1,
  CFX_SYNC = 2,
};

enum class SynchrocastTransportState : uint8_t {
  UNCONFIGURED = 0,
  STANDALONE_ACTIVE = 1,
  WAITING_FOR_CFX_SYNC = 2,
  ATTACHED_TO_CFX_SYNC = 3,
  BLOCKED = 4,
};

enum class SynchrocastTransportKind : uint8_t {
  NONE = 0,
  ESPNOW = 1,
  UDP = 2,
};

enum class SynchrocastReceivePath : uint8_t {
  NORMAL = 0,
  UNKNOWN_PEER = 1,
};

static constexpr uint8_t SYNCHROCAST_TRANSPORT_ESPNOW = 0x01;
static constexpr uint8_t SYNCHROCAST_TRANSPORT_UDP = 0x02;
static constexpr size_t SYNCHROCAST_TRANSPORT_MTU = 250;

struct SynchrocastTransportSource {
  SynchrocastTransportKind transport{SynchrocastTransportKind::NONE};
  SynchrocastReceivePath receive_path{SynchrocastReceivePath::NORMAL};
  std::array<uint8_t, 6> mac{};
  uint32_t ipv4{0};
  uint16_t port{0};
  bool identity_valid{false};
};

struct SynchrocastTransportBackendStatus {
  bool owner_present{false};
  uint8_t active_transports{0};
  uint16_t udp_port{0};
  uint8_t api_version{0};
  uint32_t recovery_generation{0};
};

struct SynchrocastTransportStatus {
  SynchrocastTransportOwner owner{SynchrocastTransportOwner::NONE};
  SynchrocastTransportState state{
      SynchrocastTransportState::UNCONFIGURED};
  uint8_t active_transports{0};
  uint16_t udp_port{0};
};

class SynchrocastTransportPacketSink {
 public:
  virtual ~SynchrocastTransportPacketSink() = default;
  virtual bool on_transport_packet(const SynchrocastTransportSource &source,
                                   const uint8_t *data, size_t size) = 0;
  virtual void on_transport_recovered(uint32_t generation) {
    (void) generation;
  }
};

class SynchrocastTransportBackend {
 public:
  virtual ~SynchrocastTransportBackend() = default;
  virtual bool attach(SynchrocastTransportPacketSink *sink) = 0;
  virtual void detach(SynchrocastTransportPacketSink *sink) = 0;
  virtual bool send_broadcast(SynchrocastTransportKind transport,
                              const uint8_t *data, size_t size) = 0;
  virtual bool send_to(const SynchrocastTransportSource &destination,
                       const uint8_t *data, size_t size) = 0;
  virtual SynchrocastTransportBackendStatus status() const = 0;
  virtual void loop() {}
};

const char *synchrocast_transport_owner_to_string(
    SynchrocastTransportOwner owner);
const char *synchrocast_transport_state_to_string(
    SynchrocastTransportState state);

}  // namespace synchrocast
}  // namespace esphome
