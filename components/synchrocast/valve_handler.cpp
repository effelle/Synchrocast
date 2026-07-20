// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_VALVE

#include "valve_handler.h"

#include "synchrocast_component.h"
#include "synchrocast_log.h"
#include "synchrocast_packet_utils.h"
#include "synchrocast_state.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.valve";

bool ValveHandler::register_entity(uint32_t entity_hash,
                                   valve::Valve *entity) {
  const auto result = this->entities_.register_entity(entity_hash, entity);
  switch (result) {
    case EntityRegistrationResult::ADDED: {
      size_t index;
      this->entities_.find(entity_hash, index);
      this->supports_position_[index] =
          entity->get_traits().get_supports_position();
      ESP_LOGV(TAG,
               "Registered valve '%s' hash=0x%08" PRIX32
               " position=%s (%u/%u)",
               entity->get_name().c_str(), entity_hash,
               this->supports_position_[index] ? "yes" : "no",
               static_cast<unsigned>(this->entities_.size()),
               static_cast<unsigned>(MAX_ENTITIES));
      return true;
    }
    case EntityRegistrationResult::ALREADY_REGISTERED:
      return true;
    case EntityRegistrationResult::INVALID_ENTITY:
      ESP_LOGE(TAG, "Cannot register null valve hash=0x%08" PRIX32,
               entity_hash);
      break;
    case EntityRegistrationResult::HASH_COLLISION:
      ESP_LOGE(TAG, "Valve hash collision 0x%08" PRIX32, entity_hash);
      break;
    case EntityRegistrationResult::CAPACITY_EXCEEDED:
      ESP_LOGE(TAG, "Valve registry full (maximum %u)",
               static_cast<unsigned>(MAX_ENTITIES));
      break;
  }
  return false;
}

bool ValveHandler::accepts_state_broadcast(uint32_t entity_hash) const {
  return this->parent_ != nullptr &&
         this->parent_->applies_canonical_state() &&
         this->entities_.find(entity_hash) != nullptr;
}

void ValveHandler::handle_intent(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::INTENT_REQUEST ||
      this->parent_ == nullptr ||
      !this->parent_->accepts_intent_requests()) {
    return;
  }
  auto *entity = this->entities_.find(packet.entity_hash);
  if (packet.domain == SynchrocastDomain::VALVE && entity != nullptr) {
    this->dispatch_intent_(packet, entity);
  }
}

void ValveHandler::handle_state_broadcast(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::STATE_BROADCAST ||
      packet.domain != SynchrocastDomain::VALVE || this->parent_ == nullptr ||
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

void ValveHandler::dispatch_intent_(const SynchrocastPacket &packet,
                                    valve::Valve *entity) {
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
      ESP_LOGV(TAG, "Unsupported valve intent=%s hash=0x%08" PRIX32,
               synchrocast_intent_to_string(packet.intent),
               packet.entity_hash);
      break;
  }
}

void ValveHandler::apply_canonical_state_(const SynchrocastPacket &packet,
                                          valve::Valve *entity,
                                          size_t index) {
  CanonicalStateReader reader(packet.payload.raw_bytes, packet.payload_len);
  CanonicalStateField field;
  auto call = entity->make_call();
  bool apply = false;
  while (reader.next(field)) {
    switch (static_cast<ValveStateField>(field.id)) {
      case ValveStateField::POSITION: {
        float position;
        if (!canonical_read_float(field, position) || position < 0.0f ||
            position > 1.0f) {
          ESP_LOGV(TAG, "Ignored invalid valve position hash=0x%08" PRIX32,
                   packet.entity_hash);
        } else if (this->supports_position_[index]) {
          call.set_position(position);
          apply = true;
        } else if (position == 0.0f) {
          call.set_command_close();
          apply = true;
        } else if (position == 1.0f) {
          call.set_command_open();
          apply = true;
        } else {
          ESP_LOGV(TAG,
                   "Ignored unsupported intermediate valve position %.3f hash=0x%08" PRIX32,
                   position, packet.entity_hash);
        }
        break;
      }
      case ValveStateField::OPERATION: {
        uint8_t operation;
        if (!canonical_read_u8(field, operation)) {
          ESP_LOGV(TAG, "Ignored invalid valve operation hash=0x%08" PRIX32,
                   packet.entity_hash);
        }
        break;
      }
      default:
        ESP_LOGV(TAG, "Skipped unknown valve field=%u hash=0x%08" PRIX32,
                 static_cast<unsigned>(field.id), packet.entity_hash);
        break;
    }
  }
  if (apply) {
    call.perform();
    ESP_LOGV(TAG, "Applied canonical valve state hash=0x%08" PRIX32,
             packet.entity_hash);
  }
}

void ValveHandler::observe_(valve::Valve *entity, PublishedState &state) {
  const float position = entity->position;
  const uint8_t operation = static_cast<uint8_t>(entity->current_operation);
  if (state.observed && state.position == position &&
      state.operation == operation) {
    return;
  }
  state.position = position;
  state.operation = operation;
  state.observed = true;
  state.pending = true;
}

void ValveHandler::maybe_send_(uint32_t entity_hash, valve::Valve *entity,
                               PublishedState &state, uint32_t now) {
  const bool refresh_due =
      state.has_sent && now - state.last_sent_ms >= STATE_REFRESH_INTERVAL_MS;
  if ((!state.pending && !refresh_due) ||
      (state.send_not_before_ms != 0 &&
       static_cast<int32_t>(now - state.send_not_before_ms) < 0) ||
      (state.last_send_attempt_ms != 0 &&
       now - state.last_send_attempt_ms < STATE_RETRY_INTERVAL_MS)) {
    return;
  }
  state.last_send_attempt_ms = now;
  SynchrocastPacket packet;
  packet.msg_type = SynchrocastMessageType::STATE_BROADCAST;
  packet.domain = SynchrocastDomain::VALVE;
  packet.intent = SynchrocastIntent::CANONICAL_STATE;
  packet.entity_hash = entity_hash;
  CanonicalStateWriter writer(packet);
  if (!writer.add_float(static_cast<uint8_t>(ValveStateField::POSITION),
                        state.position) ||
      !writer.add_u8(static_cast<uint8_t>(ValveStateField::OPERATION),
                     state.operation)) {
    ESP_LOGV(TAG, "Could not encode canonical valve '%s'",
             entity->get_name().c_str());
    return;
  }
  if (this->parent_ == nullptr || !this->parent_->send_packet(packet)) {
    return;
  }
  state.pending = false;
  state.has_sent = true;
  state.last_sent_ms = now;
  state.send_not_before_ms = 0;
  ESP_LOGV(TAG,
           "Broadcast canonical valve position=%.3f operation=%u hash=0x%08" PRIX32,
           state.position, static_cast<unsigned>(state.operation), entity_hash);
}

void ValveHandler::loop() {
  if (this->parent_ == nullptr ||
      !this->parent_->publishes_canonical_state()) {
    return;
  }
  const uint32_t now = millis();
  for (size_t i = 0; i < this->entities_.size(); i++) {
    auto *entity = this->entities_.entity_at(i);
    this->observe_(entity, this->published_[i]);
    this->maybe_send_(this->entities_.hash_at(i), entity,
                      this->published_[i], now);
  }
}

void ValveHandler::on_transport_recovered() {
  const uint32_t now = millis();
  for (size_t i = 0; i < this->entities_.size(); i++) {
    this->published_[i].pending = true;
    this->published_[i].last_send_attempt_ms = 0;
    this->published_[i].send_not_before_ms =
        now + (this->entities_.hash_at(i) %
               (RECOVERY_JITTER_SPREAD_MS + 1));
  }
}

void ValveHandler::dump_config() {
  ESP_LOGCONFIG(TAG, "  Valve: entities=%u canonical_state=yes",
                static_cast<unsigned>(this->entities_.size()));
}

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_VALVE
