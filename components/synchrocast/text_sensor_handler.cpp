// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_TEXT_SENSOR

#include "text_sensor_handler.h"

#include "synchrocast_component.h"
#include "synchrocast_packet_codec.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#if defined(USE_TEXT_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
#include "esphome/core/controller_registry.h"
#endif

#include <cinttypes>
#include <cstring>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.text_sensor";

void SynchrocastTextSensor::mark_unavailable() {
  if (!this->has_state()) {
    return;
  }
  this->state.clear();
  this->set_has_state(false);
#if defined(USE_TEXT_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_text_sensor_update(this);
#endif
}

bool TextSensorHandler::hash_in_use_(uint32_t entity_hash) const {
  for (uint8_t i = 0; i < this->publisher_count_; i++) {
    if (this->publishers_[i].entity_hash == entity_hash) {
      return true;
    }
  }
  for (uint8_t i = 0; i < this->receiver_count_; i++) {
    if (this->receivers_[i].entity_hash == entity_hash) {
      return true;
    }
  }
  return false;
}

bool TextSensorHandler::register_publisher(
    uint32_t entity_hash, text_sensor::TextSensor *source) {
  if (source == nullptr || entity_hash == 0 ||
      this->publisher_count_ >= MAX_ENTITIES ||
      this->hash_in_use_(entity_hash)) {
    ESP_LOGE(TAG, "Could not register text publisher hash=0x%08" PRIX32,
             entity_hash);
    return false;
  }
  auto &publisher = this->publishers_[this->publisher_count_++];
  publisher.source = source;
  publisher.entity_hash = entity_hash;
  ESP_LOGV(TAG,
           "Registered publisher '%s' share=0x%08" PRIX32
           " max_utf8=%u",
           source->get_name().c_str(), entity_hash,
           static_cast<unsigned>(SYNCHROCAST_MAX_PAYLOAD_SIZE));
  return true;
}

bool TextSensorHandler::register_receiver(uint32_t entity_hash,
                                          SynchrocastTextSensor *entity) {
  if (entity == nullptr || entity_hash == 0 ||
      this->receiver_count_ >= MAX_ENTITIES ||
      this->hash_in_use_(entity_hash)) {
    ESP_LOGE(TAG, "Could not register text receiver hash=0x%08" PRIX32,
             entity_hash);
    return false;
  }
  auto &receiver = this->receivers_[this->receiver_count_++];
  receiver.entity = entity;
  receiver.entity_hash = entity_hash;
  ESP_LOGV(TAG, "Registered interest '%s' share=0x%08" PRIX32,
           entity->get_name().c_str(), entity_hash);
  return true;
}

bool TextSensorHandler::accepts_state_broadcast(
    uint32_t entity_hash) const {
  for (uint8_t i = 0; i < this->receiver_count_; i++) {
    if (this->receivers_[i].entity_hash == entity_hash) {
      return true;
    }
  }
  return false;
}

void TextSensorHandler::handle_intent(const SynchrocastPacket &packet) {
  ESP_LOGV(TAG, "Rejected command for read-only text sensor hash=0x%08" PRIX32,
           packet.entity_hash);
}

TextSensorHandler::Receiver *TextSensorHandler::find_receiver_(
    uint32_t entity_hash) {
  for (uint8_t i = 0; i < this->receiver_count_; i++) {
    if (this->receivers_[i].entity_hash == entity_hash) {
      return &this->receivers_[i];
    }
  }
  return nullptr;
}

void TextSensorHandler::handle_state_broadcast(
    const SynchrocastPacket &packet) {
  if (packet.domain != SynchrocastDomain::TEXT_SENSOR ||
      packet.msg_type != SynchrocastMessageType::STATE_BROADCAST) {
    return;
  }
  auto *receiver = this->find_receiver_(packet.entity_hash);
  if (receiver == nullptr) {
    ESP_LOGV(TAG, "No text receiver for hash=0x%08" PRIX32,
             packet.entity_hash);
    return;
  }
  if (receiver->owner_boot_id != 0 &&
      receiver->owner_boot_id != packet.source_boot_id) {
    const uint32_t now = millis();
    if (receiver->last_conflict_log_ms == 0 ||
        now - receiver->last_conflict_log_ms >= 60000) {
      ESP_LOGW(TAG,
               "Ignoring competing publisher for hash=0x%08" PRIX32
               " owner=0x%08" PRIX32 " contender=0x%08" PRIX32,
               packet.entity_hash, receiver->owner_boot_id,
               packet.source_boot_id);
      receiver->last_conflict_log_ms = now;
    }
    return;
  }
  receiver->owner_boot_id = packet.source_boot_id;
  receiver->last_received_ms = millis();
  receiver->seen = true;

  if (packet.intent == SynchrocastIntent::NONE && packet.payload_len == 0) {
    receiver->entity->mark_unavailable();
    ESP_LOGV(TAG, "Remote text sensor unavailable hash=0x%08" PRIX32,
             packet.entity_hash);
    return;
  }
  if (packet.intent != SynchrocastIntent::SET_VALUE ||
      !SynchrocastPacketCodec::is_valid_utf8(packet.payload.raw_bytes,
                                              packet.payload_len)) {
    ESP_LOGV(TAG, "Rejected text payload hash=0x%08" PRIX32,
             packet.entity_hash);
    return;
  }
  receiver->entity->publish_state(
      reinterpret_cast<const char *>(packet.payload.raw_bytes),
      packet.payload_len);
  ESP_LOGV(TAG, "Published remote text bytes=%u hash=0x%08" PRIX32,
           static_cast<unsigned>(packet.payload_len), packet.entity_hash);
}

void TextSensorHandler::observe_(Publisher &publisher) {
  const auto &state = publisher.source->state;
  const bool has_state = publisher.source->has_state();
  const bool within_limit = state.size() <= SYNCHROCAST_MAX_PAYLOAD_SIZE;
  const bool valid_utf8 =
      within_limit && SynchrocastPacketCodec::is_valid_utf8(
                          reinterpret_cast<const uint8_t *>(state.data()),
                          state.size());
  const bool available = has_state && within_limit && valid_utf8;

  if (has_state && !available) {
    if (!publisher.invalid_logged) {
      ESP_LOGW(TAG,
               "Text state rejected for hash=0x%08" PRIX32
               ": bytes=%u limit=%u valid_utf8=%s; no truncation was used",
               publisher.entity_hash, static_cast<unsigned>(state.size()),
               static_cast<unsigned>(SYNCHROCAST_MAX_PAYLOAD_SIZE),
               valid_utf8 ? "yes" : "no");
      publisher.invalid_logged = true;
    }
  } else {
    publisher.invalid_logged = false;
  }

  const uint8_t length = available ? static_cast<uint8_t>(state.size()) : 0;
  publisher.current_available = available;
  publisher.current_length = length;

  const bool same_as_sent =
      publisher.has_sent && publisher.last_sent_available == available &&
      (!available ||
       (publisher.last_sent_length == length &&
        (length == 0 ||
         memcmp(publisher.last_sent_value.data(),
                state.data(), length) == 0)));
  publisher.pending = !same_as_sent;
}

void TextSensorHandler::maybe_send_(Publisher &publisher, uint32_t now) {
  const bool refresh_due =
      publisher.has_sent &&
      now - publisher.last_sent_ms >= STATE_REFRESH_INTERVAL_MS;
  if (!publisher.pending && !refresh_due) {
    return;
  }

  SynchrocastPacket packet;
  packet.msg_type = SynchrocastMessageType::STATE_BROADCAST;
  packet.domain = SynchrocastDomain::TEXT_SENSOR;
  packet.entity_hash = publisher.entity_hash;
  if (publisher.current_available) {
    packet.intent = SynchrocastIntent::SET_VALUE;
    packet.payload_len = publisher.current_length;
    if (publisher.current_length != 0) {
      memcpy(packet.payload.raw_bytes, publisher.source->state.data(),
             publisher.current_length);
    }
  }
  if (this->parent_ == nullptr || !this->parent_->send_packet(packet)) {
    return;
  }
  publisher.has_sent = true;
  publisher.last_sent_available = publisher.current_available;
  publisher.last_sent_length = packet.payload_len;
  if (packet.payload_len != 0) {
    memcpy(publisher.last_sent_value.data(), packet.payload.raw_bytes,
           packet.payload_len);
  }
  publisher.last_sent_ms = now;
  publisher.pending = false;
  ESP_LOGV(TAG, "Broadcast text %s bytes=%u hash=0x%08" PRIX32,
           publisher.current_available ? "state" : "unavailable",
           static_cast<unsigned>(publisher.current_length),
           publisher.entity_hash);
}

void TextSensorHandler::expire_receivers_(uint32_t now) {
  for (uint8_t i = 0; i < this->receiver_count_; i++) {
    auto &receiver = this->receivers_[i];
    if (!receiver.seen || now - receiver.last_received_ms <
                              RECEIVER_STALE_AFTER_MS) {
      continue;
    }
    receiver.entity->mark_unavailable();
    receiver.seen = false;
    receiver.owner_boot_id = 0;
    receiver.last_conflict_log_ms = 0;
    ESP_LOGV(TAG, "Text receiver stale hash=0x%08" PRIX32,
             receiver.entity_hash);
  }
}

void TextSensorHandler::loop() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < this->publisher_count_; i++) {
    this->observe_(this->publishers_[i]);
    this->maybe_send_(this->publishers_[i], now);
  }
  this->expire_receivers_(now);
}

void TextSensorHandler::dump_config() {
  ESP_LOGCONFIG(TAG,
                "  Text Sensor: publishers=%u receivers=%u max_utf8=%u bytes",
                static_cast<unsigned>(this->publisher_count_),
                static_cast<unsigned>(this->receiver_count_),
                static_cast<unsigned>(SYNCHROCAST_MAX_PAYLOAD_SIZE));
}

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_TEXT_SENSOR
