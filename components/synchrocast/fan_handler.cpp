// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_FAN

#include "fan_handler.h"

#include "synchrocast_component.h"
#include "synchrocast_log.h"
#include "synchrocast_packet_utils.h"
#include "synchrocast_state.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>
#include <cstring>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.fan";

bool FanHandler::register_entity(uint32_t entity_hash, fan::Fan *entity) {
  const auto result = this->entities_.register_entity(entity_hash, entity);
  switch (result) {
    case EntityRegistrationResult::ADDED: {
      size_t index;
      this->entities_.find(entity_hash, index);
      const auto traits = entity->get_traits();
      const int speed_count = traits.supported_speed_count();
      this->speed_counts_[index] =
          speed_count <= 0
              ? 0
              : static_cast<uint8_t>(speed_count > 255 ? 255 : speed_count);
      this->supports_oscillation_[index] = traits.supports_oscillation();
      this->supports_direction_[index] = traits.supports_direction();
      this->supports_preset_[index] = traits.supports_preset_modes();
      ESP_LOGV(TAG,
               "Registered fan '%s' hash=0x%08" PRIX32
               " speeds=%u oscillation=%s direction=%s preset=%s (%u/%u)",
               entity->get_name().c_str(), entity_hash,
               static_cast<unsigned>(this->speed_counts_[index]),
               this->supports_oscillation_[index] ? "yes" : "no",
               this->supports_direction_[index] ? "yes" : "no",
               this->supports_preset_[index] ? "yes" : "no",
               static_cast<unsigned>(this->entities_.size()),
               static_cast<unsigned>(MAX_ENTITIES));
      return true;
    }
    case EntityRegistrationResult::ALREADY_REGISTERED:
      return true;
    case EntityRegistrationResult::INVALID_ENTITY:
      ESP_LOGE(TAG, "Cannot register null fan hash=0x%08" PRIX32,
               entity_hash);
      break;
    case EntityRegistrationResult::HASH_COLLISION:
      ESP_LOGE(TAG, "Fan hash collision 0x%08" PRIX32, entity_hash);
      break;
    case EntityRegistrationResult::CAPACITY_EXCEEDED:
      ESP_LOGE(TAG, "Fan registry full (maximum %u)",
               static_cast<unsigned>(MAX_ENTITIES));
      break;
  }
  return false;
}

bool FanHandler::accepts_state_broadcast(uint32_t entity_hash) const {
  return this->parent_ != nullptr &&
         this->parent_->applies_canonical_state() &&
         this->entities_.find(entity_hash) != nullptr;
}

void FanHandler::handle_intent(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::INTENT_REQUEST ||
      this->parent_ == nullptr ||
      !this->parent_->accepts_intent_requests()) {
    return;
  }
  size_t index;
  auto *entity = this->entities_.find(packet.entity_hash, index);
  if (packet.domain == SynchrocastDomain::FAN && entity != nullptr) {
    this->dispatch_intent_(packet, entity, index);
  }
}

void FanHandler::handle_state_broadcast(const SynchrocastPacket &packet) {
  if (packet.msg_type != SynchrocastMessageType::STATE_BROADCAST ||
      packet.domain != SynchrocastDomain::FAN || this->parent_ == nullptr ||
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
    this->dispatch_intent_(packet, entity, index);
  }
}

void FanHandler::dispatch_intent_(const SynchrocastPacket &packet,
                                  fan::Fan *entity, size_t index) {
  switch (packet.intent) {
    case SynchrocastIntent::TURN_ON:
      entity->turn_on().perform();
      break;
    case SynchrocastIntent::TURN_OFF:
      entity->turn_off().perform();
      break;
    case SynchrocastIntent::TOGGLE:
      entity->toggle().perform();
      break;
    case SynchrocastIntent::SET_SPEED: {
      float speed_value;
      const int speed_count = this->speed_counts_[index];
      if (read_finite_float_payload(packet, speed_value) && speed_count > 0 &&
          speed_value >= 1.0f &&
          speed_value <= static_cast<float>(speed_count) &&
          speed_value == static_cast<float>(static_cast<int>(speed_value))) {
        entity->make_call()
            .set_state(true)
            .set_speed(static_cast<int>(speed_value))
            .perform();
      }
      break;
    }
    default:
      ESP_LOGV(TAG, "Unsupported fan intent=%s hash=0x%08" PRIX32,
               synchrocast_intent_to_string(packet.intent),
               packet.entity_hash);
      break;
  }
}

void FanHandler::apply_canonical_state_(const SynchrocastPacket &packet,
                                        fan::Fan *entity, size_t index) {
  CanonicalStateReader reader(packet.payload.raw_bytes, packet.payload_len);
  CanonicalStateField field;
  auto call = entity->make_call();
  bool apply = false;
  while (reader.next(field)) {
    switch (static_cast<FanStateField>(field.id)) {
      case FanStateField::POWER: {
        bool power;
        if (canonical_read_bool(field, power)) {
          call.set_state(power);
          apply = true;
        }
        break;
      }
      case FanStateField::SPEED_PERCENT: {
        uint8_t percent;
        const int count = this->speed_counts_[index];
        if (!canonical_read_u8(field, percent) || percent > 100) {
          ESP_LOGV(TAG, "Ignored invalid fan speed percent hash=0x%08" PRIX32,
                   packet.entity_hash);
        } else if (count > 0 && percent > 0) {
          int speed = (static_cast<int>(percent) * count + 50) / 100;
          if (speed < 1) {
            speed = 1;
          } else if (speed > count) {
            speed = count;
          }
          call.set_speed(speed);
          apply = true;
        } else if (count == 0) {
          ESP_LOGV(TAG, "Ignored unsupported fan speed hash=0x%08" PRIX32,
                   packet.entity_hash);
        }
        break;
      }
      case FanStateField::OSCILLATING: {
        bool oscillating;
        if (canonical_read_bool(field, oscillating) &&
            this->supports_oscillation_[index]) {
          call.set_oscillating(oscillating);
          apply = true;
        } else if (!this->supports_oscillation_[index]) {
          ESP_LOGV(TAG,
                   "Ignored unsupported fan oscillation hash=0x%08" PRIX32,
                   packet.entity_hash);
        }
        break;
      }
      case FanStateField::DIRECTION: {
        uint8_t direction;
        if (canonical_read_u8(field, direction) && direction <= 1 &&
            this->supports_direction_[index]) {
          call.set_direction(direction == 0 ? fan::FanDirection::FORWARD
                                            : fan::FanDirection::REVERSE);
          apply = true;
        } else if (!this->supports_direction_[index]) {
          ESP_LOGV(TAG,
                   "Ignored unsupported fan direction hash=0x%08" PRIX32,
                   packet.entity_hash);
        }
        break;
      }
      case FanStateField::PRESET: {
        if (!this->supports_preset_[index]) {
          ESP_LOGV(TAG, "Ignored unsupported fan preset hash=0x%08" PRIX32,
                   packet.entity_hash);
          break;
        }
        bool supported = field.length == 0;
        if (field.length <= MAX_PRESET_BYTES && !supported) {
          for (const char *preset :
               entity->get_traits().supported_preset_modes()) {
            const size_t length = strlen(preset);
            if (length == field.length &&
                memcmp(preset, field.data, field.length) == 0) {
              supported = true;
              break;
            }
          }
        }
        if (supported) {
          call.set_preset_mode(reinterpret_cast<const char *>(field.data),
                               field.length);
          apply = true;
        } else {
          ESP_LOGV(TAG,
                   "Ignored unknown fan preset hash=0x%08" PRIX32,
                   packet.entity_hash);
        }
        break;
      }
      default:
        ESP_LOGV(TAG, "Skipped unknown fan field=%u hash=0x%08" PRIX32,
                 static_cast<unsigned>(field.id), packet.entity_hash);
        break;
    }
  }
  if (apply) {
    call.perform();
    ESP_LOGV(TAG, "Applied canonical fan state hash=0x%08" PRIX32,
             packet.entity_hash);
  }
}

void FanHandler::observe_(fan::Fan *entity, PublishedState &state,
                          size_t index) {
  const int count = this->speed_counts_[index];
  const uint8_t speed_percent =
      count > 0 && entity->speed > 0
          ? static_cast<uint8_t>(
                (entity->speed * 100 + count / 2) / count > 100
                    ? 100
                    : (entity->speed * 100 + count / 2) / count)
          : 0;
  const uint8_t direction = static_cast<uint8_t>(entity->direction);
  const auto preset = entity->get_preset_mode();
  const bool preset_valid = preset.size() <= MAX_PRESET_BYTES;
  const uint8_t preset_length =
      preset_valid ? static_cast<uint8_t>(preset.size()) : 0;
  const bool same_preset =
      state.preset_valid == preset_valid &&
      state.preset_length == preset_length &&
      (preset_length == 0 ||
       memcmp(state.preset.data(), preset.c_str(), preset_length) == 0);
  if (state.observed && state.power == entity->state &&
      state.speed_percent == speed_percent &&
      state.oscillating == entity->oscillating &&
      state.direction == direction && same_preset) {
    return;
  }
  state.power = entity->state;
  state.speed_percent = speed_percent;
  state.oscillating = entity->oscillating;
  state.direction = direction;
  state.preset_valid = preset_valid;
  state.preset_length = preset_length;
  if (preset_length != 0) {
    memcpy(state.preset.data(), preset.c_str(), preset_length);
  }
  state.preset[preset_length] = '\0';
  state.observed = true;
  state.pending = true;
  if (!preset_valid) {
    ESP_LOGV(TAG, "Fan preset omitted: %u bytes exceeds %u",
             static_cast<unsigned>(preset.size()),
             static_cast<unsigned>(MAX_PRESET_BYTES));
  }
}

void FanHandler::maybe_send_(uint32_t entity_hash, fan::Fan *entity,
                             PublishedState &state, size_t index,
                             uint32_t now) {
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
  packet.domain = SynchrocastDomain::FAN;
  packet.intent = SynchrocastIntent::CANONICAL_STATE;
  packet.entity_hash = entity_hash;
  CanonicalStateWriter writer(packet);
  if (!writer.add_bool(static_cast<uint8_t>(FanStateField::POWER),
                       state.power) ||
      (this->speed_counts_[index] != 0 &&
       !writer.add_u8(static_cast<uint8_t>(FanStateField::SPEED_PERCENT),
                      state.speed_percent)) ||
      (this->supports_oscillation_[index] &&
       !writer.add_bool(static_cast<uint8_t>(FanStateField::OSCILLATING),
                        state.oscillating)) ||
      (this->supports_direction_[index] &&
       !writer.add_u8(static_cast<uint8_t>(FanStateField::DIRECTION),
                      state.direction)) ||
      (this->supports_preset_[index] && state.preset_valid &&
       !writer.add_string(static_cast<uint8_t>(FanStateField::PRESET),
                          state.preset.data(), state.preset_length))) {
    ESP_LOGV(TAG, "Could not encode canonical fan '%s'",
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
           "Broadcast canonical fan power=%s speed=%u%% oscillating=%s hash=0x%08" PRIX32,
           state.power ? "ON" : "OFF",
           static_cast<unsigned>(state.speed_percent),
           state.oscillating ? "yes" : "no", entity_hash);
}

void FanHandler::loop() {
  if (this->parent_ == nullptr ||
      !this->parent_->publishes_canonical_state()) {
    return;
  }
  const uint32_t now = millis();
  for (size_t i = 0; i < this->entities_.size(); i++) {
    auto *entity = this->entities_.entity_at(i);
    this->observe_(entity, this->published_[i], i);
    this->maybe_send_(this->entities_.hash_at(i), entity,
                      this->published_[i], i, now);
  }
}

void FanHandler::on_transport_recovered() {
  const uint32_t now = millis();
  for (size_t i = 0; i < this->entities_.size(); i++) {
    this->published_[i].pending = true;
    this->published_[i].last_send_attempt_ms = 0;
    this->published_[i].send_not_before_ms =
        now + (this->entities_.hash_at(i) %
               (RECOVERY_JITTER_SPREAD_MS + 1));
  }
}

void FanHandler::dump_config() {
  ESP_LOGCONFIG(TAG, "  Fan: entities=%u canonical_state=yes",
                static_cast<unsigned>(this->entities_.size()));
}

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_FAN
