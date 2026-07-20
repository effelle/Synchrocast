// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace synchrocast {

static constexpr size_t SYNCHROCAST_WIRE_MAX_PAYLOAD_SIZE = 96;
#if defined(USE_SYNCHROCAST_FAN)
static constexpr size_t SYNCHROCAST_MAX_PAYLOAD_SIZE = 96;
#elif defined(USE_SYNCHROCAST_TEXT_SENSOR)
static constexpr size_t SYNCHROCAST_MAX_PAYLOAD_SIZE = 64;
#elif defined(USE_SYNCHROCAST_COVER) || defined(USE_SYNCHROCAST_VALVE)
static constexpr size_t SYNCHROCAST_MAX_PAYLOAD_SIZE = 32;
#else
static constexpr size_t SYNCHROCAST_MAX_PAYLOAD_SIZE = 16;
#endif

enum class SynchrocastRole : uint8_t {
  LEADER = 0,
  FOLLOWER = 1,
  CONTROLLER = 2,
  SATELLITE = 3,
};

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
  TEXT_SENSOR,
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
  CANONICAL_STATE,
};

union SynchrocastPayload {
  float float_val;
  uint32_t hash_val;
  bool bool_val;
  uint8_t raw_bytes[SYNCHROCAST_MAX_PAYLOAD_SIZE];
};

// Keep the 4-byte fields first and never send this in-memory structure
// verbatim. The wire codec serializes every multibyte field explicitly.
struct SynchrocastPacket {
  uint32_t entity_hash{0};
  uint32_t source_boot_id{0};
  SynchrocastPayload payload{};
  SynchrocastMessageType msg_type{SynchrocastMessageType::HEARTBEAT};
  SynchrocastDomain domain{SynchrocastDomain::UNKNOWN};
  SynchrocastIntent intent{SynchrocastIntent::NONE};
  SynchrocastRole source_role{SynchrocastRole::FOLLOWER};
  uint8_t payload_len{0};
};

static_assert(sizeof(SynchrocastPayload) == SYNCHROCAST_MAX_PAYLOAD_SIZE,
              "Synchrocast payload size changed");
#if defined(USE_SYNCHROCAST_FAN)
static_assert(sizeof(SynchrocastPacket) == 112,
              "Synchrocast fan packet must remain bounded");
#elif defined(USE_SYNCHROCAST_TEXT_SENSOR)
static_assert(sizeof(SynchrocastPacket) == 80,
               "Synchrocast packet must remain bounded");
#elif defined(USE_SYNCHROCAST_COVER) || defined(USE_SYNCHROCAST_VALVE)
static_assert(sizeof(SynchrocastPacket) == 48,
              "Synchrocast actuator packet must remain bounded");
#else
static_assert(sizeof(SynchrocastPacket) == 32,
              "Synchrocast packet must remain compact without text sensors");
#endif

class SynchrocastDomainHandler {
 public:
  virtual ~SynchrocastDomainHandler() = default;
  virtual SynchrocastDomain get_domain() const = 0;
  virtual bool accepts_state_broadcast(uint32_t entity_hash) const {
    return true;
  }
  virtual void handle_intent(const SynchrocastPacket &packet) = 0;
  virtual void handle_state_broadcast(const SynchrocastPacket &packet) = 0;
  virtual void on_transport_recovered() {}
  virtual void loop() {}
  virtual void dump_config() {}
};

}  // namespace synchrocast
}  // namespace esphome
