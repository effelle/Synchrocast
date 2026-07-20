// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_COVER

#include "synchrocast_entity_registry.h"
#include "synchrocast_types.h"

#include "esphome/components/cover/cover.h"

namespace esphome {
namespace synchrocast {

class CoverHandler final : public SynchrocastDomainHandler {
 public:
  static constexpr size_t MAX_ENTITIES = 16;

  SynchrocastDomain get_domain() const override { return SynchrocastDomain::COVER; }
  bool register_entity(uint32_t entity_hash, cover::Cover *entity);

  void handle_intent(const SynchrocastPacket &packet) override;
  void handle_state_broadcast(const SynchrocastPacket &packet) override;

 protected:
  void dispatch_(const SynchrocastPacket &packet, bool state_broadcast);

  SynchrocastEntityRegistry<cover::Cover, MAX_ENTITIES> entities_;
};

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_COVER
