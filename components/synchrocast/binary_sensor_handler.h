// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_BINARY_SENSOR

#include "synchrocast_types.h"

#include "esphome/components/binary_sensor/binary_sensor.h"

#include <array>

namespace esphome {
namespace synchrocast {

class SynchrocastComponent;

class SynchrocastBinarySensor final : public binary_sensor::BinarySensor {
 public:
  void mark_unavailable() { this->invalidate_state(); }
};

class BinarySensorHandler final : public SynchrocastDomainHandler {
 public:
  static constexpr size_t MAX_ENTITIES = 16;

  void set_parent(SynchrocastComponent *parent) { this->parent_ = parent; }
  bool register_publisher(uint32_t entity_hash,
                          binary_sensor::BinarySensor *source,
                          uint32_t min_interval_ms,
                          uint32_t refresh_interval_ms);
  bool register_receiver(uint32_t entity_hash,
                         SynchrocastBinarySensor *entity,
                         uint32_t stale_after_ms);

  SynchrocastDomain get_domain() const override {
    return SynchrocastDomain::BINARY_SENSOR;
  }
  void handle_intent(const SynchrocastPacket &packet) override;
  void handle_state_broadcast(const SynchrocastPacket &packet) override;
  void loop() override;
  void dump_config() override;

 protected:
  struct Publisher {
    binary_sensor::BinarySensor *source{nullptr};
    uint32_t entity_hash{0};
    uint32_t min_interval_ms{0};
    uint32_t refresh_interval_ms{0};
    uint32_t last_attempt_ms{0};
    uint32_t last_sent_ms{0};
    bool observed{false};
    bool current_available{false};
    bool current_value{false};
    bool has_sent{false};
    bool last_sent_available{false};
    bool last_sent_value{false};
    bool pending{false};
  };

  struct Receiver {
    SynchrocastBinarySensor *entity{nullptr};
    uint32_t entity_hash{0};
    uint32_t stale_after_ms{0};
    uint32_t owner_boot_id{0};
    uint32_t last_received_ms{0};
    uint32_t last_conflict_log_ms{0};
    bool seen{false};
  };

  bool hash_in_use_(uint32_t entity_hash) const;
  Receiver *find_receiver_(uint32_t entity_hash);
  void observe_(Publisher &publisher);
  void maybe_send_(Publisher &publisher, uint32_t now);
  void expire_receivers_(uint32_t now);

  SynchrocastComponent *parent_{nullptr};
  std::array<Publisher, MAX_ENTITIES> publishers_{};
  std::array<Receiver, MAX_ENTITIES> receivers_{};
  uint8_t publisher_count_{0};
  uint8_t receiver_count_{0};
};

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_BINARY_SENSOR
