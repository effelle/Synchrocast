// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "synchrocast_entity_registry.h"
#include "synchrocast_types.h"

#include "esphome/components/valve/valve.h"

namespace esphome {
namespace synchrocast {

class ValveHandler final : public SynchrocastDomainHandler {
 public:
  static constexpr size_t MAX_ENTITIES = 16;

  SynchrocastDomain get_domain() const override { return SynchrocastDomain::VALVE; }
  bool register_entity(uint32_t entity_hash, valve::Valve *entity);

  void handle_intent(const SynchrocastPacket &packet) override;
  void handle_state_broadcast(const SynchrocastPacket &packet) override;

 protected:
  void dispatch_(const SynchrocastPacket &packet, bool state_broadcast);

  SynchrocastEntityRegistry<valve::Valve, MAX_ENTITIES> entities_;
};

}  // namespace synchrocast
}  // namespace esphome
