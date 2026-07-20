// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "synchrocast_dispatcher.h"

#include "synchrocast_log.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>

#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#elif defined(USE_HOST)
#include <mutex>
#endif

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.dispatcher";

namespace {

#ifdef USE_ESP32
portMUX_TYPE queue_mux = portMUX_INITIALIZER_UNLOCKED;
#elif defined(USE_HOST)
std::mutex queue_mutex;
#endif

class QueueLockGuard {
 public:
  QueueLockGuard() {
#ifdef USE_ESP32
    portENTER_CRITICAL(&queue_mux);
#elif defined(USE_HOST)
    queue_mutex.lock();
#endif
  }

  ~QueueLockGuard() {
#ifdef USE_ESP32
    portEXIT_CRITICAL(&queue_mux);
#elif defined(USE_HOST)
    queue_mutex.unlock();
#endif
  }
};

}  // namespace

bool SynchrocastDispatcher::register_handler(SynchrocastDomainHandler *handler) {
  if (handler == nullptr) {
    ESP_LOGE(TAG, "Cannot register a null domain handler");
    return false;
  }

  const auto domain = handler->get_domain();
  const size_t slot = static_cast<size_t>(domain);
  if (domain == SynchrocastDomain::UNKNOWN || slot >= this->handlers_.size()) {
    ESP_LOGE(TAG, "Cannot register handler for invalid domain %u", static_cast<unsigned>(domain));
    return false;
  }

  if (this->handlers_[slot] == handler) {
    ESP_LOGV(TAG, "Handler already registered for %s", synchrocast_domain_to_string(domain));
    return true;
  }
  if (this->handlers_[slot] != nullptr) {
    ESP_LOGE(TAG, "A different handler is already registered for %s", synchrocast_domain_to_string(domain));
    return false;
  }

  this->handlers_[slot] = handler;
  this->handler_count_++;
  ESP_LOGV(TAG, "Registered %s handler (%u total)", synchrocast_domain_to_string(domain),
           static_cast<unsigned>(this->handler_count_));
  return true;
}

SynchrocastEnqueueResult SynchrocastDispatcher::enqueue_packet(const SynchrocastPacket &packet) {
  QueueLockGuard lock;
  this->stats_.received++;

  if (!is_valid_packet_(packet)) {
    this->stats_.invalid++;
    return SynchrocastEnqueueResult::INVALID_PACKET;
  }

  if (packet.msg_type == SynchrocastMessageType::STATE_BROADCAST) {
    const size_t slot = static_cast<size_t>(packet.domain);
    auto *handler =
        slot < this->handlers_.size() ? this->handlers_[slot] : nullptr;
    if (handler == nullptr ||
        !handler->accepts_state_broadcast(packet.entity_hash)) {
      this->stats_.filtered++;
      return SynchrocastEnqueueResult::FILTERED;
    }
  }

  // Replace only the newest pending packet for the same entity, and only when
  // it is also a state packet. Encountering a newer intent stops coalescing so
  // per-entity command order remains intact.
  if (packet.msg_type == SynchrocastMessageType::STATE_BROADCAST) {
    for (uint8_t offset = this->queue_count_; offset > 0; offset--) {
      const uint8_t index = (this->queue_head_ + offset - 1) % QUEUE_CAPACITY;
      auto &pending = this->queue_[index];
      if (pending.domain != packet.domain || pending.entity_hash != packet.entity_hash) {
        continue;
      }
      if (pending.msg_type == SynchrocastMessageType::STATE_BROADCAST) {
        pending = packet;
        this->stats_.coalesced++;
        return SynchrocastEnqueueResult::COALESCED;
      }
      break;
    }
  }

  if (this->queue_count_ >= QUEUE_CAPACITY) {
    this->stats_.dropped++;
    return SynchrocastEnqueueResult::QUEUE_FULL;
  }

  const uint8_t tail = (this->queue_head_ + this->queue_count_) % QUEUE_CAPACITY;
  this->queue_[tail] = packet;
  this->queue_count_++;
  this->stats_.queued++;
  return SynchrocastEnqueueResult::QUEUED;
}

void SynchrocastDispatcher::loop() {
  uint8_t processed = 0;
  uint8_t remaining = 0;
  uint32_t dropped_total = this->last_reported_drops_;
  SynchrocastPacket packet;

  while (processed < MAX_PACKETS_PER_LOOP && this->pop_packet_(packet, remaining, dropped_total)) {
    processed++;

    if (packet.msg_type == SynchrocastMessageType::HEARTBEAT) {
      this->stats_.heartbeats++;
      ESP_LOGV(TAG, "Dequeued HEARTBEAT queue=%u", static_cast<unsigned>(remaining));
      continue;
    }

    const size_t slot = static_cast<size_t>(packet.domain);
    auto *handler = slot < this->handlers_.size() ? this->handlers_[slot] : nullptr;
    if (handler == nullptr) {
      this->stats_.no_handler++;
      ESP_LOGV(TAG, "No handler: type=%s domain=%s entity=0x%08" PRIX32 " intent=%s queue=%u",
               synchrocast_message_type_to_string(packet.msg_type), synchrocast_domain_to_string(packet.domain),
               packet.entity_hash, synchrocast_intent_to_string(packet.intent), static_cast<unsigned>(remaining));
      continue;
    }

    ESP_LOGV(TAG, "Dispatch type=%s domain=%s entity=0x%08" PRIX32 " intent=%s payload=%u queue=%u",
             synchrocast_message_type_to_string(packet.msg_type), synchrocast_domain_to_string(packet.domain),
             packet.entity_hash, synchrocast_intent_to_string(packet.intent),
             static_cast<unsigned>(packet.payload_len), static_cast<unsigned>(remaining));

    if (packet.msg_type == SynchrocastMessageType::INTENT_REQUEST) {
      handler->handle_intent(packet);
    } else {
      handler->handle_state_broadcast(packet);
    }
    this->stats_.dispatched++;
  }

  for (auto *handler : this->handlers_) {
    if (handler != nullptr) {
      handler->loop();
    }
  }
  this->maybe_log_queue_drops_(dropped_total);
}

void SynchrocastDispatcher::dump_config() {
  ESP_LOGCONFIG(TAG, "Synchrocast Dispatcher:");
  ESP_LOGCONFIG(TAG, "  Registered handlers: %u", static_cast<unsigned>(this->handler_count_));
  ESP_LOGCONFIG(TAG, "  Queue: %u packets, %u bytes", static_cast<unsigned>(QUEUE_CAPACITY),
                static_cast<unsigned>(sizeof(this->queue_)));
  ESP_LOGCONFIG(TAG, "  Per-loop dispatch budget: %u packets", static_cast<unsigned>(MAX_PACKETS_PER_LOOP));
  for (auto *handler : this->handlers_) {
    if (handler != nullptr) {
      handler->dump_config();
    }
  }
}

uint8_t SynchrocastDispatcher::queue_depth() {
  QueueLockGuard lock;
  return this->queue_count_;
}

SynchrocastDispatcherStats SynchrocastDispatcher::get_stats() {
  QueueLockGuard lock;
  return this->stats_;
}

void SynchrocastDispatcher::log_stats() {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  const auto stats = this->get_stats();
  ESP_LOGV(TAG,
           "Stats received=%" PRIu32 " queued=%" PRIu32 " coalesced=%" PRIu32 " filtered=%" PRIu32
           " dropped=%" PRIu32
           " invalid=%" PRIu32 " dispatched=%" PRIu32 " heartbeat=%" PRIu32 " no_handler=%" PRIu32,
           stats.received, stats.queued, stats.coalesced, stats.filtered,
           stats.dropped, stats.invalid, stats.dispatched, stats.heartbeats,
           stats.no_handler);
#endif
}

void SynchrocastDispatcher::on_transport_recovered() {
  for (auto *handler : this->handlers_) {
    if (handler != nullptr) {
      handler->on_transport_recovered();
    }
  }
}

bool SynchrocastDispatcher::is_valid_packet_(const SynchrocastPacket &packet) {
  const auto type = static_cast<uint8_t>(packet.msg_type);
  if (type > static_cast<uint8_t>(SynchrocastMessageType::INTENT_REQUEST) ||
      packet.payload_len > sizeof(packet.payload.raw_bytes)) {
    return false;
  }

  if (packet.msg_type == SynchrocastMessageType::HEARTBEAT) {
    return packet.payload_len == 0;
  }

  const auto domain = static_cast<uint8_t>(packet.domain);
  const auto intent = static_cast<uint8_t>(packet.intent);
  return domain > static_cast<uint8_t>(SynchrocastDomain::UNKNOWN) && domain < DOMAIN_SLOT_COUNT &&
         intent <=
             static_cast<uint8_t>(SynchrocastIntent::CANONICAL_STATE);
}

bool SynchrocastDispatcher::pop_packet_(SynchrocastPacket &packet, uint8_t &remaining, uint32_t &dropped_total) {
  QueueLockGuard lock;
  dropped_total = this->stats_.dropped;
  if (this->queue_count_ == 0) {
    remaining = 0;
    return false;
  }

  packet = this->queue_[this->queue_head_];
  this->queue_head_ = (this->queue_head_ + 1) % QUEUE_CAPACITY;
  this->queue_count_--;
  remaining = this->queue_count_;
  return true;
}

void SynchrocastDispatcher::maybe_log_queue_drops_(uint32_t dropped_total) {
  if (dropped_total == this->last_reported_drops_) {
    return;
  }

  const uint32_t now = millis();
  if (this->last_drop_log_ms_ != 0 && now - this->last_drop_log_ms_ < 5000) {
    return;
  }

  ESP_LOGW(TAG, "Packet queue full; dropped=%" PRIu32 " capacity=%u", dropped_total,
           static_cast<unsigned>(QUEUE_CAPACITY));
  this->last_reported_drops_ = dropped_total;
  this->last_drop_log_ms_ = now;
}

}  // namespace synchrocast
}  // namespace esphome
