// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_SENSOR

#include "sensor_handler.h"

#include "synchrocast_component.h"
#include "synchrocast_log.h"
#include "synchrocast_packet_utils.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#if defined(USE_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
#include "esphome/core/controller_registry.h"
#endif

#include <cmath>
#include <cinttypes>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.sensor";

void SynchrocastSensor::mark_unavailable() {
  if (!this->has_state()) {
    return;
  }
  this->state = NAN;
  this->set_has_state(false);
#if defined(USE_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_sensor_update(this);
#endif
}

bool SensorHandler::hash_in_use_(uint32_t entity_hash) const {
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

bool SensorHandler::register_publisher(uint32_t entity_hash,
                                       sensor::Sensor *source) {
  if (source == nullptr || entity_hash == 0 ||
      this->publisher_count_ >= MAX_ENTITIES ||
      this->hash_in_use_(entity_hash)) {
    ESP_LOGE(TAG, "Could not register numeric publisher hash=0x%08" PRIX32,
             entity_hash);
    return false;
  }
  auto &publisher = this->publishers_[this->publisher_count_++];
  publisher.source = source;
  publisher.entity_hash = entity_hash;
  ESP_LOGV(TAG, "Registered publisher '%s' share=0x%08" PRIX32,
           source->get_name().c_str(), entity_hash);
  return true;
}

bool SensorHandler::register_receiver(uint32_t entity_hash,
                                      SynchrocastSensor *entity) {
  if (entity == nullptr || entity_hash == 0 ||
      this->receiver_count_ >= MAX_ENTITIES ||
      this->hash_in_use_(entity_hash)) {
    ESP_LOGE(TAG, "Could not register numeric receiver hash=0x%08" PRIX32,
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

bool SensorHandler::accepts_state_broadcast(uint32_t entity_hash) const {
  for (uint8_t i = 0; i < this->receiver_count_; i++) {
    if (this->receivers_[i].entity_hash == entity_hash) {
      return true;
    }
  }
  return false;
}

void SensorHandler::handle_intent(const SynchrocastPacket &packet) {
  ESP_LOGV(TAG, "Rejected command for read-only numeric sensor hash=0x%08" PRIX32,
           packet.entity_hash);
}

SensorHandler::Receiver *SensorHandler::find_receiver_(uint32_t entity_hash) {
  for (uint8_t i = 0; i < this->receiver_count_; i++) {
    if (this->receivers_[i].entity_hash == entity_hash) {
      return &this->receivers_[i];
    }
  }
  return nullptr;
}

void SensorHandler::handle_state_broadcast(const SynchrocastPacket &packet) {
  if (packet.domain != SynchrocastDomain::SENSOR ||
      packet.msg_type != SynchrocastMessageType::STATE_BROADCAST) {
    return;
  }
  auto *receiver = this->find_receiver_(packet.entity_hash);
  if (receiver == nullptr) {
    ESP_LOGV(TAG, "No numeric receiver for hash=0x%08" PRIX32,
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
    ESP_LOGV(TAG, "Remote numeric sensor unavailable hash=0x%08" PRIX32,
             packet.entity_hash);
    return;
  }

  float value;
  if (packet.intent != SynchrocastIntent::SET_VALUE ||
      !read_finite_float_payload(packet, value)) {
    ESP_LOGV(TAG, "Rejected numeric payload hash=0x%08" PRIX32,
             packet.entity_hash);
    return;
  }
  receiver->entity->publish_state(value);
  ESP_LOGV(TAG, "Published remote numeric value=%g hash=0x%08" PRIX32,
           value, packet.entity_hash);
}

void SensorHandler::observe_(Publisher &publisher) {
  const bool available = publisher.source->has_state() &&
                         std::isfinite(publisher.source->state);
  const float value = available ? publisher.source->state : 0.0f;
  if (publisher.observed && publisher.current_available == available &&
      (!available || publisher.current_value == value)) {
    return;
  }

  publisher.observed = true;
  publisher.current_available = available;
  publisher.current_value = value;
  publisher.pending = !publisher.has_sent ||
                      publisher.last_sent_available != available ||
                      (available && value != publisher.last_sent_value);
}

void SensorHandler::maybe_send_(Publisher &publisher, uint32_t now) {
  const bool refresh_due =
      publisher.has_sent &&
      now - publisher.last_sent_ms >= STATE_REFRESH_INTERVAL_MS;
  if (!publisher.pending && !refresh_due) {
    return;
  }

  SynchrocastPacket packet;
  packet.msg_type = SynchrocastMessageType::STATE_BROADCAST;
  packet.domain = SynchrocastDomain::SENSOR;
  packet.entity_hash = publisher.entity_hash;
  if (publisher.current_available) {
    packet.intent = SynchrocastIntent::SET_VALUE;
    packet.payload.float_val = publisher.current_value;
    packet.payload_len = sizeof(float);
  }

  if (this->parent_ == nullptr || !this->parent_->send_packet(packet)) {
    return;
  }
  publisher.has_sent = true;
  publisher.last_sent_available = publisher.current_available;
  publisher.last_sent_value = publisher.current_value;
  publisher.last_sent_ms = now;
  publisher.pending = false;
  ESP_LOGV(TAG, "Broadcast numeric %s value=%g hash=0x%08" PRIX32,
           publisher.current_available ? "state" : "unavailable",
           publisher.current_value, publisher.entity_hash);
}

void SensorHandler::expire_receivers_(uint32_t now) {
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
    ESP_LOGV(TAG, "Numeric receiver stale hash=0x%08" PRIX32,
             receiver.entity_hash);
  }
}

void SensorHandler::loop() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < this->publisher_count_; i++) {
    this->observe_(this->publishers_[i]);
    this->maybe_send_(this->publishers_[i], now);
  }
  this->expire_receivers_(now);
}

void SensorHandler::dump_config() {
  ESP_LOGCONFIG(TAG, "  Numeric Sensor: publishers=%u receivers=%u",
                static_cast<unsigned>(this->publisher_count_),
                static_cast<unsigned>(this->receiver_count_));
}

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_SENSOR
