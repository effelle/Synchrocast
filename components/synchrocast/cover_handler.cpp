// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_COVER

#include "cover_handler.h"

#include "synchrocast_component.h"
#include "synchrocast_log.h"
#include "synchrocast_packet_utils.h"
#include "synchrocast_state.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>
#include <cmath>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.cover";

bool CoverHandler::register_entity(uint32_t entity_hash,
                                   cover::Cover *entity) {
  const auto result = this->entities_.register_entity(entity_hash, entity);
  switch (result) {
    case EntityRegistrationResult::ADDED: {
      size_t index;
      this->entities_.find(entity_hash, index);
      const auto traits = entity->get_traits();
      this->supports_position_[index] = traits.get_supports_position();
      this->supports_tilt_[index] = traits.get_supports_tilt();
      ESP_LOGV(TAG,
               "Registered cover '%s' hash=0x%08" PRIX32
               " position=%s tilt=%s (%u/%u)",
               entity->get_name().c_str(), entity_hash,
               this->supports_position_[index] ? "yes" : "no",
               this->supports_tilt_[index] ? "yes" : "no",
               static_cast<unsigned>(this->entities_.size()),
               static_cast<unsigned>(MAX_ENTITIES));
      return true;
    }
    case EntityRegistrationResult::ALREADY_REGISTERED:
      return true;
    case EntityRegistrationResult::INVALID_ENTITY:
      ESP_LOGE(TAG, "Cannot register null cover hash=0x%08" PRIX32,
               entity_hash);
      break;
    case EntityRegistrationResult::HASH_COLLISION:
      ESP_LOGE(TAG, "Cover hash collision 0x%08" PRIX32, entity_hash);
      break;
    case EntityRegistrationResult::CAPACITY_EXCEEDED:
      ESP_LOGE(TAG, "Cover registry full (maximum %u)",
               static_cast<unsigned>(MAX_ENTITIES));
      break;
  }
  return false;
}

bool CoverHandler::accepts_state_broadcast(uint32_t entity_hash) const {
  return this->parent_ != nullptr &&
         this->parent_->applies_canonical_state() &&
         this->entities_.find(entity_hash) != nullptr;
}

void CoverHandler::handle_intent(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::INTENT_REQUEST ||
      this->parent_ == nullptr ||
      !this->parent_->accepts_intent_requests()) {
    return;
  }
  auto *entity = this->entities_.find(packet.entity_hash);
  if (packet.domain != SynchrocastDomain::COVER || entity == nullptr) {
    return;
  }
  this->dispatch_intent_(packet, entity);
}

void CoverHandler::handle_state_broadcast(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::STATE_BROADCAST ||
      packet.domain != SynchrocastDomain::COVER || this->parent_ == nullptr ||
      !this->parent_->applies_canonical_state()) {
    return;
  }
  size_t index;
  auto *entity = this->entities_.find(packet.entity_hash, index);
  if (entity == nullptr) {
    return;
  }
  if (packet.intent == SynchrocastIntent::CANONICAL_STATE) {
    this->apply_canonical_state_(packet, entity, index);
  } else if (packet.intent != SynchrocastIntent::TOGGLE) {
    this->dispatch_intent_(packet, entity);
  }
}

void CoverHandler::dispatch_intent_(const SynchrocastPacket &packet,
                                    cover::Cover *entity) {
  auto call = entity->make_call();
  switch (packet.intent) {
    case SynchrocastIntent::OPEN:
      call.set_command_open().perform();
      break;
    case SynchrocastIntent::CLOSE:
      call.set_command_close().perform();
      break;
    case SynchrocastIntent::STOP:
      call.set_command_stop().perform();
      break;
    case SynchrocastIntent::TOGGLE:
      call.set_command_toggle().perform();
      break;
    case SynchrocastIntent::SET_POSITION: {
      float position;
      if (read_finite_float_payload(packet, position) && position >= 0.0f &&
          position <= 1.0f) {
        call.set_position(position).perform();
      }
      break;
    }
    default:
      ESP_LOGV(TAG, "Unsupported cover intent=%s hash=0x%08" PRIX32,
               synchrocast_intent_to_string(packet.intent),
               packet.entity_hash);
      break;
  }
}

void CoverHandler::apply_canonical_state_(const SynchrocastPacket &packet,
                                          cover::Cover *entity,
                                          size_t index) {
  CanonicalStateReader reader(packet.payload.raw_bytes, packet.payload_len);
  CanonicalStateField field;
  auto call = entity->make_call();
  bool apply = false;
  while (reader.next(field)) {
    switch (static_cast<CoverStateField>(field.id)) {
      case CoverStateField::POSITION: {
        float position;
        if (!canonical_read_float(field, position) || position < 0.0f ||
            position > 1.0f) {
          ESP_LOGV(TAG, "Ignored invalid cover position hash=0x%08" PRIX32,
                   packet.entity_hash);
        } else if (this->supports_position_[index] &&
                   std::fabs(entity->position - position) > 0.001f) {
          call.set_position(position);
          apply = true;
        } else if (!this->supports_position_[index] && position == 0.0f &&
                   entity->position != 0.0f) {
          call.set_command_close();
          apply = true;
        } else if (!this->supports_position_[index] && position == 1.0f &&
                   entity->position != 1.0f) {
          call.set_command_open();
          apply = true;
        } else if (!this->supports_position_[index] && position != 0.0f &&
                   position != 1.0f) {
          ESP_LOGV(TAG,
                   "Ignored unsupported intermediate cover position %.3f hash=0x%08" PRIX32,
                   position, packet.entity_hash);
        }
        break;
      }
      case CoverStateField::TILT: {
        float tilt;
        if (!canonical_read_float(field, tilt) || tilt < 0.0f ||
            tilt > 1.0f) {
          ESP_LOGV(TAG, "Ignored invalid cover tilt hash=0x%08" PRIX32,
                   packet.entity_hash);
        } else if (this->supports_tilt_[index] &&
                   std::fabs(entity->tilt - tilt) > 0.001f) {
          call.set_tilt(tilt);
          apply = true;
        } else {
          ESP_LOGV(TAG, "Ignored unsupported cover tilt hash=0x%08" PRIX32,
                   packet.entity_hash);
        }
        break;
      }
      case CoverStateField::OPERATION: {
        uint8_t operation;
        if (!canonical_read_u8(field, operation)) {
          ESP_LOGV(TAG, "Ignored invalid cover operation hash=0x%08" PRIX32,
                   packet.entity_hash);
        }
        break;
      }
      default:
        ESP_LOGV(TAG, "Skipped unknown cover field=%u hash=0x%08" PRIX32,
                 static_cast<unsigned>(field.id), packet.entity_hash);
        break;
    }
  }
  if (apply) {
    call.perform();
    ESP_LOGV(TAG, "Applied canonical cover state hash=0x%08" PRIX32,
             packet.entity_hash);
  } else {
    ESP_LOGV(TAG, "Refreshed matching canonical cover state hash=0x%08" PRIX32,
             packet.entity_hash);
  }
}

void CoverHandler::observe_(cover::Cover *entity, PublishedState &state,
                            size_t index) {
  const float position = entity->position;
  const float tilt = this->supports_tilt_[index] ? entity->tilt : 0.0f;
  const uint8_t operation = static_cast<uint8_t>(entity->current_operation);
  if (state.observed && state.position == position && state.tilt == tilt &&
      state.operation == operation) {
    return;
  }
  state.position = position;
  state.tilt = tilt;
  state.operation = operation;
  state.observed = true;
  state.pending = true;
}

void CoverHandler::maybe_send_(uint32_t entity_hash, cover::Cover *entity,
                               PublishedState &state, size_t index,
                               uint32_t now) {
  if (!synchrocast_publisher_ready(state, now)) {
    return;
  }
  synchrocast_publisher_attempted(state, now);
  SynchrocastPacket packet;
  packet.msg_type = SynchrocastMessageType::STATE_BROADCAST;
  packet.domain = SynchrocastDomain::COVER;
  packet.intent = SynchrocastIntent::CANONICAL_STATE;
  packet.entity_hash = entity_hash;
  CanonicalStateWriter writer(packet);
  if (!writer.add_float(static_cast<uint8_t>(CoverStateField::POSITION),
                        state.position) ||
      (this->supports_tilt_[index] &&
       !writer.add_float(static_cast<uint8_t>(CoverStateField::TILT),
                         state.tilt)) ||
      !writer.add_u8(static_cast<uint8_t>(CoverStateField::OPERATION),
                     state.operation)) {
    ESP_LOGV(TAG, "Could not encode canonical cover '%s'",
             entity->get_name().c_str());
    return;
  }
  if (this->parent_ == nullptr || !this->parent_->send_packet(packet)) {
    return;
  }
  synchrocast_publisher_sent(state, now);
  ESP_LOGV(TAG,
           "Broadcast canonical cover position=%.3f tilt=%.3f operation=%u hash=0x%08" PRIX32,
           state.position, state.tilt, static_cast<unsigned>(state.operation),
           entity_hash);
}

void CoverHandler::loop() {
  if (this->parent_ == nullptr ||
      !this->parent_->publishes_canonical_state()) {
    return;
  }
  const uint32_t now = millis();
  if (this->entities_.size() == 0) {
    return;
  }
  const size_t i = this->publisher_cursor_;
  this->publisher_cursor_ =
      (this->publisher_cursor_ + 1) % this->entities_.size();
  auto *entity = this->entities_.entity_at(i);
  this->observe_(entity, this->published_[i], i);
  this->maybe_send_(this->entities_.hash_at(i), entity,
                    this->published_[i], i, now);
}

void CoverHandler::queue_refresh_(const char *reason) {
  const uint32_t now = millis();
  for (size_t i = 0; i < this->entities_.size(); i++) {
    synchrocast_queue_publisher_refresh(
        this->published_[i], now, this->entities_.hash_at(i));
  }
  ESP_LOGV(TAG, "%s queued %u cover state refreshes", reason,
           static_cast<unsigned>(this->entities_.size()));
}

void CoverHandler::on_transport_recovered() {
  this->queue_refresh_("Transport recovery");
}

void CoverHandler::on_state_request() {
  this->queue_refresh_("STATE_REQUEST");
}

void CoverHandler::dump_config() {
  ESP_LOGCONFIG(TAG, "  Cover: entities=%u canonical_state=yes",
                static_cast<unsigned>(this->entities_.size()));
}

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_COVER
