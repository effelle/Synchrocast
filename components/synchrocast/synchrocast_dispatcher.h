// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "synchrocast_types.h"

#include "esphome/core/component.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace synchrocast {

enum class SynchrocastEnqueueResult : uint8_t {
  QUEUED,
  COALESCED,
  QUEUE_FULL,
  INVALID_PACKET,
};

struct SynchrocastDispatcherStats {
  uint32_t received{0};
  uint32_t queued{0};
  uint32_t coalesced{0};
  uint32_t dropped{0};
  uint32_t invalid{0};
  uint32_t dispatched{0};
  uint32_t heartbeats{0};
  uint32_t no_handler{0};
};

class SynchrocastDispatcher final : public Component {
 public:
  static constexpr uint8_t QUEUE_CAPACITY = 16;
  static constexpr uint8_t MAX_PACKETS_PER_LOOP = 4;
  static constexpr size_t DOMAIN_SLOT_COUNT = static_cast<size_t>(SynchrocastDomain::BINARY_SENSOR) + 1;

  bool register_handler(SynchrocastDomainHandler *handler);

  // Called by a decrypted transport callback. This method only validates and
  // copies into fixed storage; ESPHome entities are touched later in loop().
  SynchrocastEnqueueResult enqueue_packet(const SynchrocastPacket &packet);

  void loop() override;
  void dump_config() override;

  uint8_t queue_depth();
  SynchrocastDispatcherStats get_stats();
  void log_stats();

 protected:
  static bool is_valid_packet_(const SynchrocastPacket &packet);
  bool pop_packet_(SynchrocastPacket &packet, uint8_t &remaining, uint32_t &dropped_total);
  void maybe_log_queue_drops_(uint32_t dropped_total);

  std::array<SynchrocastPacket, QUEUE_CAPACITY> queue_{};
  std::array<SynchrocastDomainHandler *, DOMAIN_SLOT_COUNT> handlers_{};
  uint8_t queue_head_{0};
  uint8_t queue_count_{0};
  uint8_t handler_count_{0};
  SynchrocastDispatcherStats stats_{};
  uint32_t last_drop_log_ms_{0};
  uint32_t last_reported_drops_{0};
};

}  // namespace synchrocast
}  // namespace esphome
