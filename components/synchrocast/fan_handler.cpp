// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_FAN

#include "fan_handler.h"

#include "synchrocast_log.h"
#include "synchrocast_packet_utils.h"

#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.fan";

bool FanHandler::register_entity(uint32_t entity_hash, fan::Fan *entity) {
  const auto result = this->entities_.register_entity(entity_hash, entity);
  switch (result) {
    case EntityRegistrationResult::ADDED: {
      size_t index;
      this->entities_.find(entity_hash, index);
      const int speed_count = entity->get_traits().supported_speed_count();
      this->speed_counts_[index] =
          speed_count <= 0 ? 0 : static_cast<uint8_t>(speed_count > 255 ? 255 : speed_count);
      ESP_LOGV(TAG, "Registered fan '%s' hash=0x%08" PRIX32 " speeds=%u (%u/%u)",
               entity->get_name().c_str(), entity_hash, static_cast<unsigned>(this->speed_counts_[index]),
               static_cast<unsigned>(this->entities_.size()), static_cast<unsigned>(MAX_ENTITIES));
      return true;
    }
    case EntityRegistrationResult::ALREADY_REGISTERED:
      ESP_LOGV(TAG, "Fan hash=0x%08" PRIX32 " already registered", entity_hash);
      return true;
    case EntityRegistrationResult::INVALID_ENTITY:
      ESP_LOGE(TAG, "Cannot register a null fan for hash 0x%08" PRIX32, entity_hash);
      break;
    case EntityRegistrationResult::HASH_COLLISION:
      ESP_LOGE(TAG, "Fan entity hash collision for 0x%08" PRIX32, entity_hash);
      break;
    case EntityRegistrationResult::CAPACITY_EXCEEDED:
      ESP_LOGE(TAG, "Fan registry is full (maximum %u entities)", static_cast<unsigned>(MAX_ENTITIES));
      break;
  }
  return false;
}

void FanHandler::handle_intent(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::INTENT_REQUEST) {
    ESP_LOGV(TAG, "Rejected %s passed to handle_intent", synchrocast_message_type_to_string(packet.msg_type));
    return;
  }
  this->dispatch_(packet, false);
}

void FanHandler::handle_state_broadcast(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::STATE_BROADCAST) {
    ESP_LOGV(TAG, "Rejected %s passed to handle_state_broadcast",
             synchrocast_message_type_to_string(packet.msg_type));
    return;
  }
  this->dispatch_(packet, true);
}

void FanHandler::dispatch_(const SynchrocastPacket &packet, bool state_broadcast) {
  if (packet.domain != SynchrocastDomain::FAN) {
    ESP_LOGV(TAG, "Rejected domain=%s", synchrocast_domain_to_string(packet.domain));
    return;
  }

  size_t entity_index;
  auto *entity = this->entities_.find(packet.entity_hash, entity_index);
  if (entity == nullptr) {
    ESP_LOGV(TAG, "No fan for hash=0x%08" PRIX32 " intent=%s", packet.entity_hash,
             synchrocast_intent_to_string(packet.intent));
    return;
  }

  if (state_broadcast && packet.intent == SynchrocastIntent::TOGGLE) {
    ESP_LOGV(TAG, "Rejected relative TOGGLE state for fan hash=0x%08" PRIX32, packet.entity_hash);
    return;
  }

  switch (packet.intent) {
    case SynchrocastIntent::TURN_ON:
      ESP_LOGV(TAG, "Apply %s TURN_ON to '%s' hash=0x%08" PRIX32,
               synchrocast_message_type_to_string(packet.msg_type), entity->get_name().c_str(), packet.entity_hash);
      entity->turn_on().perform();
      break;
    case SynchrocastIntent::TURN_OFF:
      ESP_LOGV(TAG, "Apply %s TURN_OFF to '%s' hash=0x%08" PRIX32,
               synchrocast_message_type_to_string(packet.msg_type), entity->get_name().c_str(), packet.entity_hash);
      entity->turn_off().perform();
      break;
    case SynchrocastIntent::TOGGLE:
      ESP_LOGV(TAG, "Apply INTENT_REQUEST TOGGLE to '%s' hash=0x%08" PRIX32, entity->get_name().c_str(),
               packet.entity_hash);
      entity->toggle().perform();
      break;
    case SynchrocastIntent::SET_SPEED: {
      float speed_value;
      if (!read_finite_float_payload(packet, speed_value)) {
        ESP_LOGV(TAG, "Rejected SET_SPEED payload_len=%u for fan hash=0x%08" PRIX32,
                 static_cast<unsigned>(packet.payload_len), packet.entity_hash);
        return;
      }

      const int speed_count = this->speed_counts_[entity_index];
      if (speed_count <= 0 || speed_value < 1.0f || speed_value > static_cast<float>(speed_count)) {
        ESP_LOGV(TAG, "Rejected fan speed=%.3f supported=1..%d hash=0x%08" PRIX32, speed_value, speed_count,
                 packet.entity_hash);
        return;
      }

      const int speed = static_cast<int>(speed_value);
      if (speed_value != static_cast<float>(speed)) {
        ESP_LOGV(TAG, "Rejected non-integer fan speed=%.3f hash=0x%08" PRIX32, speed_value, packet.entity_hash);
        return;
      }

      ESP_LOGV(TAG, "Apply %s SET_SPEED=%d to '%s' hash=0x%08" PRIX32,
               synchrocast_message_type_to_string(packet.msg_type), speed, entity->get_name().c_str(),
               packet.entity_hash);
      auto call = entity->turn_on();
      call.set_speed(speed).perform();
      break;
    }
    default:
      ESP_LOGV(TAG, "Unsupported fan intent=%s hash=0x%08" PRIX32,
               synchrocast_intent_to_string(packet.intent), packet.entity_hash);
      break;
  }
}

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_FAN
