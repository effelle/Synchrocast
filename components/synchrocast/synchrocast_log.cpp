// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "synchrocast_log.h"

namespace esphome {
namespace synchrocast {

const char *synchrocast_message_type_to_string(SynchrocastMessageType type) {
  switch (type) {
    case SynchrocastMessageType::HEARTBEAT:
      return "HEARTBEAT";
    case SynchrocastMessageType::STATE_BROADCAST:
      return "STATE_BROADCAST";
    case SynchrocastMessageType::INTENT_REQUEST:
      return "INTENT_REQUEST";
    case SynchrocastMessageType::STATE_REQUEST:
      return "STATE_REQUEST";
    default:
      return "UNKNOWN_MESSAGE";
  }
}

const char *synchrocast_domain_to_string(SynchrocastDomain domain) {
  switch (domain) {
    case SynchrocastDomain::UNKNOWN:
      return "UNKNOWN";
    case SynchrocastDomain::LIGHT:
      return "LIGHT";
    case SynchrocastDomain::COVER:
      return "COVER";
    case SynchrocastDomain::FAN:
      return "FAN";
    case SynchrocastDomain::CLIMATE:
      return "CLIMATE";
    case SynchrocastDomain::LOCK:
      return "LOCK";
    case SynchrocastDomain::MEDIA_PLAYER:
      return "MEDIA_PLAYER";
    case SynchrocastDomain::VALVE:
      return "VALVE";
    case SynchrocastDomain::SWITCH:
      return "SWITCH";
    case SynchrocastDomain::BUTTON:
      return "BUTTON";
    case SynchrocastDomain::NUMBER:
      return "NUMBER";
    case SynchrocastDomain::SELECT:
      return "SELECT";
    case SynchrocastDomain::ALARM_PANEL:
      return "ALARM_PANEL";
    case SynchrocastDomain::SENSOR:
      return "SENSOR";
    case SynchrocastDomain::BINARY_SENSOR:
      return "BINARY_SENSOR";
    case SynchrocastDomain::TEXT_SENSOR:
      return "TEXT_SENSOR";
    default:
      return "UNKNOWN_DOMAIN";
  }
}

const char *synchrocast_intent_to_string(SynchrocastIntent intent) {
  switch (intent) {
    case SynchrocastIntent::NONE:
      return "NONE";
    case SynchrocastIntent::TURN_ON:
      return "TURN_ON";
    case SynchrocastIntent::TURN_OFF:
      return "TURN_OFF";
    case SynchrocastIntent::TOGGLE:
      return "TOGGLE";
    case SynchrocastIntent::OPEN:
      return "OPEN";
    case SynchrocastIntent::CLOSE:
      return "CLOSE";
    case SynchrocastIntent::STOP:
      return "STOP";
    case SynchrocastIntent::LOCK:
      return "LOCK";
    case SynchrocastIntent::UNLOCK:
      return "UNLOCK";
    case SynchrocastIntent::PRESS:
      return "PRESS";
    case SynchrocastIntent::PLAY:
      return "PLAY";
    case SynchrocastIntent::PAUSE:
      return "PAUSE";
    case SynchrocastIntent::ARM_AWAY:
      return "ARM_AWAY";
    case SynchrocastIntent::ARM_HOME:
      return "ARM_HOME";
    case SynchrocastIntent::ARM_NIGHT:
      return "ARM_NIGHT";
    case SynchrocastIntent::DISARM:
      return "DISARM";
    case SynchrocastIntent::TRIGGER:
      return "TRIGGER";
    case SynchrocastIntent::SET_POSITION:
      return "SET_POSITION";
    case SynchrocastIntent::SET_SPEED:
      return "SET_SPEED";
    case SynchrocastIntent::SET_VOLUME:
      return "SET_VOLUME";
    case SynchrocastIntent::SET_TEMP:
      return "SET_TEMP";
    case SynchrocastIntent::SET_VALUE:
      return "SET_VALUE";
    case SynchrocastIntent::SET_MODE:
      return "SET_MODE";
    case SynchrocastIntent::SET_OPTION:
      return "SET_OPTION";
    case SynchrocastIntent::CANONICAL_STATE:
      return "CANONICAL_STATE";
    default:
      return "UNKNOWN_INTENT";
  }
}

}  // namespace synchrocast
}  // namespace esphome
