// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_FAN

#include "synchrocast_entity_registry.h"
#include "synchrocast_types.h"

#include "esphome/components/fan/fan.h"

#include <array>

namespace esphome {
namespace synchrocast {

class SynchrocastComponent;

class FanHandler final : public SynchrocastDomainHandler {
 public:
  static constexpr size_t MAX_ENTITIES = 16;
  static constexpr size_t MAX_PRESET_BYTES = 64;
  static constexpr uint32_t STATE_REFRESH_INTERVAL_MS = 60000;
  static constexpr uint32_t STATE_RETRY_INTERVAL_MS = 500;
  static constexpr uint32_t RECOVERY_JITTER_SPREAD_MS = 750;

  void set_parent(SynchrocastComponent *parent) { this->parent_ = parent; }
  SynchrocastDomain get_domain() const override {
    return SynchrocastDomain::FAN;
  }
  bool register_entity(uint32_t entity_hash, fan::Fan *entity);

  bool accepts_state_broadcast(uint32_t entity_hash) const override;
  void handle_intent(const SynchrocastPacket &packet) override;
  void handle_state_broadcast(const SynchrocastPacket &packet) override;
  void on_transport_recovered() override;
  void loop() override;
  void dump_config() override;

 protected:
  struct PublishedState {
    std::array<char, MAX_PRESET_BYTES + 1> preset{};
    uint32_t last_sent_ms{0};
    uint32_t last_send_attempt_ms{0};
    uint32_t send_not_before_ms{0};
    uint8_t speed_percent{0};
    uint8_t direction{0};
    uint8_t preset_length{0};
    bool power{false};
    bool oscillating{false};
    bool preset_valid{true};
    bool observed{false};
    bool pending{false};
    bool has_sent{false};
  };

  void dispatch_intent_(const SynchrocastPacket &packet, fan::Fan *entity,
                        size_t index);
  void apply_canonical_state_(const SynchrocastPacket &packet,
                              fan::Fan *entity, size_t index);
  void observe_(fan::Fan *entity, PublishedState &state, size_t index);
  void maybe_send_(uint32_t entity_hash, fan::Fan *entity,
                   PublishedState &state, size_t index, uint32_t now);

  SynchrocastComponent *parent_{nullptr};
  SynchrocastEntityRegistry<fan::Fan, MAX_ENTITIES> entities_;
  std::array<PublishedState, MAX_ENTITIES> published_{};
  std::array<uint8_t, MAX_ENTITIES> speed_counts_{};
  std::array<bool, MAX_ENTITIES> supports_oscillation_{};
  std::array<bool, MAX_ENTITIES> supports_direction_{};
  std::array<bool, MAX_ENTITIES> supports_preset_{};
};

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_FAN
