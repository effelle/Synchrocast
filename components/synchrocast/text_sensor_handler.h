// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_TEXT_SENSOR

#include "synchrocast_types.h"

#include "esphome/components/text_sensor/text_sensor.h"

#include <array>

namespace esphome {
namespace synchrocast {

class SynchrocastComponent;

class SynchrocastTextSensor final : public text_sensor::TextSensor {
 public:
  void mark_unavailable();
};

class TextSensorHandler final : public SynchrocastDomainHandler {
 public:
  static constexpr size_t MAX_ENTITIES = 16;
  static constexpr uint32_t STATE_REFRESH_INTERVAL_MS = 60000;
  static constexpr uint32_t RECEIVER_STALE_AFTER_MS = 180000;

  void set_parent(SynchrocastComponent *parent) { this->parent_ = parent; }
  bool register_publisher(uint32_t entity_hash,
                          text_sensor::TextSensor *source);
  bool register_receiver(uint32_t entity_hash,
                         SynchrocastTextSensor *entity);

  SynchrocastDomain get_domain() const override {
    return SynchrocastDomain::TEXT_SENSOR;
  }
  bool accepts_state_broadcast(uint32_t entity_hash) const override;
  void handle_intent(const SynchrocastPacket &packet) override;
  void handle_state_broadcast(const SynchrocastPacket &packet) override;
  void loop() override;
  void dump_config() override;

 protected:
  struct Publisher {
    text_sensor::TextSensor *source{nullptr};
    uint32_t entity_hash{0};
    uint32_t last_sent_ms{0};
    std::array<uint8_t, SYNCHROCAST_MAX_PAYLOAD_SIZE> last_sent_value{};
    uint8_t current_length{0};
    uint8_t last_sent_length{0};
    bool current_available{false};
    bool has_sent{false};
    bool last_sent_available{false};
    bool pending{false};
    bool invalid_logged{false};
  };

  struct Receiver {
    SynchrocastTextSensor *entity{nullptr};
    uint32_t entity_hash{0};
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

#endif  // USE_SYNCHROCAST_TEXT_SENSOR
