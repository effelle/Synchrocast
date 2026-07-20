// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_BINARY_SENSOR

#include "binary_sensor_handler.h"

#include "synchrocast_component.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.binary_sensor";

bool BinarySensorHandler::hash_in_use_(uint32_t entity_hash) const {
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

bool BinarySensorHandler::register_publisher(
    uint32_t entity_hash, binary_sensor::BinarySensor *source) {
  if (source == nullptr || entity_hash == 0 ||
      this->publisher_count_ >= MAX_ENTITIES ||
      this->hash_in_use_(entity_hash)) {
    ESP_LOGE(TAG, "Could not register binary publisher hash=0x%08" PRIX32,
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

bool BinarySensorHandler::register_receiver(
    uint32_t entity_hash, SynchrocastBinarySensor *entity) {
  if (entity == nullptr || entity_hash == 0 ||
      this->receiver_count_ >= MAX_ENTITIES ||
      this->hash_in_use_(entity_hash)) {
    ESP_LOGE(TAG, "Could not register binary receiver hash=0x%08" PRIX32,
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

bool BinarySensorHandler::accepts_state_broadcast(
    uint32_t entity_hash) const {
  for (uint8_t i = 0; i < this->receiver_count_; i++) {
    if (this->receivers_[i].entity_hash == entity_hash) {
      return true;
    }
  }
  return false;
}

void BinarySensorHandler::handle_intent(const SynchrocastPacket &packet) {
  ESP_LOGV(TAG, "Rejected command for read-only binary sensor hash=0x%08" PRIX32,
           packet.entity_hash);
}

BinarySensorHandler::Receiver *BinarySensorHandler::find_receiver_(
    uint32_t entity_hash) {
  for (uint8_t i = 0; i < this->receiver_count_; i++) {
    if (this->receivers_[i].entity_hash == entity_hash) {
      return &this->receivers_[i];
    }
  }
  return nullptr;
}

void BinarySensorHandler::handle_state_broadcast(
    const SynchrocastPacket &packet) {
  if (packet.domain != SynchrocastDomain::BINARY_SENSOR ||
      packet.msg_type != SynchrocastMessageType::STATE_BROADCAST) {
    return;
  }
  auto *receiver = this->find_receiver_(packet.entity_hash);
  if (receiver == nullptr) {
    ESP_LOGV(TAG, "No binary receiver for hash=0x%08" PRIX32,
             packet.entity_hash);
    return;
  }
  receiver->last_received_ms = millis();
  receiver->seen = true;

  if (packet.intent == SynchrocastIntent::NONE && packet.payload_len == 0) {
    if (receiver->entity->has_state()) {
      receiver->entity->mark_unavailable();
      ESP_LOGV(TAG, "Remote binary sensor unavailable hash=0x%08" PRIX32,
               packet.entity_hash);
    } else {
      ESP_LOGV(TAG, "Refreshed unchanged binary unavailability hash=0x%08" PRIX32,
               packet.entity_hash);
    }
    return;
  }
  if (packet.intent != SynchrocastIntent::SET_VALUE ||
      packet.payload_len != 1 ||
      (packet.payload.raw_bytes[0] != 0 &&
       packet.payload.raw_bytes[0] != 1)) {
    ESP_LOGV(TAG, "Rejected binary payload hash=0x%08" PRIX32,
             packet.entity_hash);
    return;
  }
  const bool value = packet.payload.raw_bytes[0] != 0;
  if (receiver->entity->has_state() && receiver->entity->state == value) {
    ESP_LOGV(TAG, "Refreshed unchanged binary value=%s hash=0x%08" PRIX32,
             value ? "ON" : "OFF", packet.entity_hash);
  } else {
    receiver->entity->publish_state(value);
    ESP_LOGV(TAG, "Published remote binary value=%s hash=0x%08" PRIX32,
             value ? "ON" : "OFF", packet.entity_hash);
  }
}

void BinarySensorHandler::observe_(Publisher &publisher) {
  const bool available = publisher.source->has_state();
  const bool value = available ? publisher.source->state : false;
  if (publisher.observed && publisher.current_available == available &&
      (!available || publisher.current_value == value)) {
    return;
  }
  publisher.observed = true;
  publisher.current_available = available;
  publisher.current_value = value;
  publisher.pending = !publisher.has_sent ||
                      publisher.last_sent_available != available ||
                      (available && publisher.last_sent_value != value);
}

void BinarySensorHandler::maybe_send_(Publisher &publisher, uint32_t now) {
  if (!synchrocast_publisher_ready(publisher, now)) {
    return;
  }
  synchrocast_publisher_attempted(publisher, now);

  SynchrocastPacket packet;
  packet.msg_type = SynchrocastMessageType::STATE_BROADCAST;
  packet.domain = SynchrocastDomain::BINARY_SENSOR;
  packet.entity_hash = publisher.entity_hash;
  if (publisher.current_available) {
    packet.intent = SynchrocastIntent::SET_VALUE;
    packet.payload.raw_bytes[0] = publisher.current_value ? 1 : 0;
    packet.payload_len = 1;
  }
  if (this->parent_ == nullptr || !this->parent_->send_packet(packet)) {
    return;
  }
  publisher.last_sent_available = publisher.current_available;
  publisher.last_sent_value = publisher.current_value;
  synchrocast_publisher_sent(publisher, now);
  ESP_LOGV(TAG, "Broadcast binary %s value=%s hash=0x%08" PRIX32,
           publisher.current_available ? "state" : "unavailable",
           publisher.current_value ? "ON" : "OFF", publisher.entity_hash);
}

void BinarySensorHandler::expire_next_receiver_(uint32_t now) {
  if (this->receiver_count_ == 0) {
    return;
  }
  auto &receiver = this->receivers_[this->receiver_cursor_];
  this->receiver_cursor_ =
      (this->receiver_cursor_ + 1) % this->receiver_count_;
  if (!receiver.seen ||
      now - receiver.last_received_ms < RECEIVER_STALE_AFTER_MS) {
    return;
  }
  receiver.entity->mark_unavailable();
  receiver.seen = false;
  ESP_LOGV(TAG, "Binary receiver stale hash=0x%08" PRIX32,
           receiver.entity_hash);
}

void BinarySensorHandler::loop() {
  const uint32_t now = millis();
  if (this->publisher_count_ != 0) {
    auto &publisher = this->publishers_[this->publisher_cursor_];
    this->publisher_cursor_ =
        (this->publisher_cursor_ + 1) % this->publisher_count_;
    this->observe_(publisher);
    this->maybe_send_(publisher, now);
  }
  this->expire_next_receiver_(now);
}

void BinarySensorHandler::queue_refresh_(const char *reason) {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < this->publisher_count_; i++) {
    synchrocast_queue_publisher_refresh(
        this->publishers_[i], now, this->publishers_[i].entity_hash);
  }
  ESP_LOGV(TAG, "%s queued %u binary state refreshes", reason,
           static_cast<unsigned>(this->publisher_count_));
}

void BinarySensorHandler::on_transport_recovered() {
  this->queue_refresh_("Transport recovery");
}

void BinarySensorHandler::on_state_request() {
  this->queue_refresh_("STATE_REQUEST");
}

void BinarySensorHandler::dump_config() {
  ESP_LOGCONFIG(TAG, "  Binary Sensor: publishers=%u receivers=%u",
                static_cast<unsigned>(this->publisher_count_),
                static_cast<unsigned>(this->receiver_count_));
}

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_BINARY_SENSOR
