// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace esphome {
namespace synchrocast {

enum class SynchrocastMessageType : uint8_t {
  HEARTBEAT = 0,
  STATE_BROADCAST = 1,
  INTENT_REQUEST = 2,
};

enum class SynchrocastDomain : uint8_t {
  UNKNOWN = 0,
  LIGHT,
  COVER,
  FAN,
  CLIMATE,
  LOCK,
  MEDIA_PLAYER,
  VALVE,
  SWITCH,
  BUTTON,
  NUMBER,
  SELECT,
  ALARM_PANEL,
  SENSOR,
  BINARY_SENSOR,
};

enum class SynchrocastIntent : uint8_t {
  NONE = 0,
  TURN_ON,
  TURN_OFF,
  TOGGLE,
  OPEN,
  CLOSE,
  STOP,
  LOCK,
  UNLOCK,
  PRESS,
  PLAY,
  PAUSE,
  ARM_AWAY,
  ARM_HOME,
  ARM_NIGHT,
  DISARM,
  TRIGGER,
  SET_POSITION,
  SET_SPEED,
  SET_VOLUME,
  SET_TEMP,
  SET_VALUE,
  SET_MODE,
  SET_OPTION,
};

union SynchrocastPayload {
  float float_val;
  uint32_t hash_val;
  bool bool_val;
  uint8_t raw_bytes[16];
};

// Keep the 4-byte fields first so this application-layer packet occupies 24
// bytes without packing or unaligned accesses. Transport codecs must serialize
// fields explicitly and must not send this in-memory structure verbatim.
struct SynchrocastPacket {
  uint32_t entity_hash{0};
  SynchrocastPayload payload{};
  SynchrocastMessageType msg_type{SynchrocastMessageType::HEARTBEAT};
  SynchrocastDomain domain{SynchrocastDomain::UNKNOWN};
  SynchrocastIntent intent{SynchrocastIntent::NONE};
  uint8_t payload_len{0};
};

static_assert(sizeof(SynchrocastPayload) == 16, "Synchrocast payload size changed");
static_assert(sizeof(SynchrocastPacket) == 24, "Synchrocast packet must remain compact");

class SynchrocastDomainHandler {
 public:
  virtual ~SynchrocastDomainHandler() = default;
  virtual SynchrocastDomain get_domain() const = 0;
  virtual void handle_intent(const SynchrocastPacket &packet) = 0;
  virtual void handle_state_broadcast(const SynchrocastPacket &packet) = 0;
};

}  // namespace synchrocast
}  // namespace esphome
