// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_SENSOR

#include "synchrocast_types.h"

#include "esphome/components/sensor/sensor.h"

#include <array>

namespace esphome {
namespace synchrocast {

class SynchrocastComponent;

class SynchrocastSensor final : public sensor::Sensor {
 public:
  void mark_unavailable();
};

class SensorHandler final : public SynchrocastDomainHandler {
 public:
  static constexpr size_t MAX_ENTITIES = 16;
  static constexpr uint32_t STATE_REFRESH_INTERVAL_MS = 60000;
  static constexpr uint32_t STATE_RETRY_INTERVAL_MS = 500;
  static constexpr uint32_t RECOVERY_JITTER_SPREAD_MS = 750;
  static constexpr uint32_t RECEIVER_STALE_AFTER_MS = 180000;

  void set_parent(SynchrocastComponent *parent) { this->parent_ = parent; }
  bool register_publisher(uint32_t entity_hash, sensor::Sensor *source);
  bool register_receiver(uint32_t entity_hash, SynchrocastSensor *entity);

  SynchrocastDomain get_domain() const override {
    return SynchrocastDomain::SENSOR;
  }
  bool accepts_state_broadcast(uint32_t entity_hash) const override;
  void handle_intent(const SynchrocastPacket &packet) override;
  void handle_state_broadcast(const SynchrocastPacket &packet) override;
  void on_transport_recovered() override;
  void loop() override;
  void dump_config() override;

 protected:
  struct Publisher {
    sensor::Sensor *source{nullptr};
    uint32_t entity_hash{0};
    uint32_t last_sent_ms{0};
    uint32_t last_send_attempt_ms{0};
    uint32_t send_not_before_ms{0};
    float current_value{0.0f};
    float last_sent_value{0.0f};
    bool observed{false};
    bool current_available{false};
    bool has_sent{false};
    bool last_sent_available{false};
    bool pending{false};
  };

  struct Receiver {
    SynchrocastSensor *entity{nullptr};
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

#endif  // USE_SYNCHROCAST_SENSOR
