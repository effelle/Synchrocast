// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "cover_handler.h"

#include "synchrocast_log.h"
#include "synchrocast_packet_utils.h"

#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.cover";

bool CoverHandler::register_entity(uint32_t entity_hash, cover::Cover *entity) {
  const auto result = this->entities_.register_entity(entity_hash, entity);
  switch (result) {
    case EntityRegistrationResult::ADDED:
      ESP_LOGV(TAG, "Registered cover '%s' hash=0x%08" PRIX32 " (%u/%u)", entity->get_name().c_str(), entity_hash,
               static_cast<unsigned>(this->entities_.size()), static_cast<unsigned>(MAX_ENTITIES));
      return true;
    case EntityRegistrationResult::ALREADY_REGISTERED:
      ESP_LOGV(TAG, "Cover hash=0x%08" PRIX32 " already registered", entity_hash);
      return true;
    case EntityRegistrationResult::INVALID_ENTITY:
      ESP_LOGE(TAG, "Cannot register a null cover for hash 0x%08" PRIX32, entity_hash);
      break;
    case EntityRegistrationResult::HASH_COLLISION:
      ESP_LOGE(TAG, "Cover entity hash collision for 0x%08" PRIX32, entity_hash);
      break;
    case EntityRegistrationResult::CAPACITY_EXCEEDED:
      ESP_LOGE(TAG, "Cover registry is full (maximum %u entities)", static_cast<unsigned>(MAX_ENTITIES));
      break;
  }
  return false;
}

void CoverHandler::handle_intent(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::INTENT_REQUEST) {
    ESP_LOGV(TAG, "Rejected %s passed to handle_intent", synchrocast_message_type_to_string(packet.msg_type));
    return;
  }
  this->dispatch_(packet, false);
}

void CoverHandler::handle_state_broadcast(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::STATE_BROADCAST) {
    ESP_LOGV(TAG, "Rejected %s passed to handle_state_broadcast",
             synchrocast_message_type_to_string(packet.msg_type));
    return;
  }
  this->dispatch_(packet, true);
}

void CoverHandler::dispatch_(const SynchrocastPacket &packet, bool state_broadcast) {
  if (packet.domain != SynchrocastDomain::COVER) {
    ESP_LOGV(TAG, "Rejected domain=%s", synchrocast_domain_to_string(packet.domain));
    return;
  }

  auto *entity = this->entities_.find(packet.entity_hash);
  if (entity == nullptr) {
    ESP_LOGV(TAG, "No cover for hash=0x%08" PRIX32 " intent=%s", packet.entity_hash,
             synchrocast_intent_to_string(packet.intent));
    return;
  }

  if (state_broadcast && packet.intent == SynchrocastIntent::TOGGLE) {
    ESP_LOGV(TAG, "Rejected relative TOGGLE state for cover hash=0x%08" PRIX32, packet.entity_hash);
    return;
  }

  switch (packet.intent) {
    case SynchrocastIntent::OPEN:
      ESP_LOGV(TAG, "Apply %s OPEN to '%s' hash=0x%08" PRIX32,
               synchrocast_message_type_to_string(packet.msg_type), entity->get_name().c_str(), packet.entity_hash);
      entity->make_call().set_command_open().perform();
      break;
    case SynchrocastIntent::CLOSE:
      ESP_LOGV(TAG, "Apply %s CLOSE to '%s' hash=0x%08" PRIX32,
               synchrocast_message_type_to_string(packet.msg_type), entity->get_name().c_str(), packet.entity_hash);
      entity->make_call().set_command_close().perform();
      break;
    case SynchrocastIntent::STOP:
      ESP_LOGV(TAG, "Apply %s STOP to '%s' hash=0x%08" PRIX32,
               synchrocast_message_type_to_string(packet.msg_type), entity->get_name().c_str(), packet.entity_hash);
      entity->make_call().set_command_stop().perform();
      break;
    case SynchrocastIntent::TOGGLE:
      ESP_LOGV(TAG, "Apply INTENT_REQUEST TOGGLE to '%s' hash=0x%08" PRIX32, entity->get_name().c_str(),
               packet.entity_hash);
      entity->make_call().set_command_toggle().perform();
      break;
    case SynchrocastIntent::SET_POSITION: {
      float position;
      if (!read_finite_float_payload(packet, position) || position < 0.0f || position > 1.0f) {
        ESP_LOGV(TAG, "Rejected SET_POSITION payload_len=%u for cover hash=0x%08" PRIX32,
                 static_cast<unsigned>(packet.payload_len), packet.entity_hash);
        return;
      }
      ESP_LOGV(TAG, "Apply %s SET_POSITION=%.3f to '%s' hash=0x%08" PRIX32,
               synchrocast_message_type_to_string(packet.msg_type), position, entity->get_name().c_str(),
               packet.entity_hash);
      entity->make_call().set_position(position).perform();
      break;
    }
    default:
      ESP_LOGV(TAG, "Unsupported cover intent=%s hash=0x%08" PRIX32,
               synchrocast_intent_to_string(packet.intent), packet.entity_hash);
      break;
  }
}

}  // namespace synchrocast
}  // namespace esphome
