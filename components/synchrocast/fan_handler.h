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

class FanHandler final : public SynchrocastDomainHandler {
 public:
  static constexpr size_t MAX_ENTITIES = 16;

  SynchrocastDomain get_domain() const override { return SynchrocastDomain::FAN; }
  bool register_entity(uint32_t entity_hash, fan::Fan *entity);

  void handle_intent(const SynchrocastPacket &packet) override;
  void handle_state_broadcast(const SynchrocastPacket &packet) override;

 protected:
  void dispatch_(const SynchrocastPacket &packet, bool state_broadcast);

  SynchrocastEntityRegistry<fan::Fan, MAX_ENTITIES> entities_;
  // Fan traits are static after setup. Cache the supported speed count so the
  // packet path does not copy FanTraits merely to validate a speed.
  std::array<uint8_t, MAX_ENTITIES> speed_counts_{};
};

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_FAN
