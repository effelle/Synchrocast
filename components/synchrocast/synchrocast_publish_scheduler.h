// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace esphome {
namespace synchrocast {

static constexpr uint32_t SYNCHROCAST_STATE_REFRESH_INTERVAL_MS = 60000;
static constexpr uint32_t SYNCHROCAST_STATE_RETRY_INTERVAL_MS = 500;
static constexpr uint32_t SYNCHROCAST_RECOVERY_JITTER_SPREAD_MS = 750;

// Operate on the timing fields already stored by each domain. These helpers add
// no per-entity object, allocation, virtual dispatch, or structure padding.
template<typename State>
inline bool synchrocast_publisher_ready(const State &state, uint32_t now) {
  const bool refresh_due =
      state.has_sent &&
      now - state.last_sent_ms >= SYNCHROCAST_STATE_REFRESH_INTERVAL_MS;
  if (!state.pending && !refresh_due) {
    return false;
  }
  if (state.send_not_before_ms != 0 &&
      static_cast<int32_t>(now - state.send_not_before_ms) < 0) {
    return false;
  }
  return state.last_send_attempt_ms == 0 ||
         now - state.last_send_attempt_ms >=
             SYNCHROCAST_STATE_RETRY_INTERVAL_MS;
}

template<typename State>
inline void synchrocast_publisher_attempted(State &state, uint32_t now) {
  state.last_send_attempt_ms = now;
}

template<typename State>
inline void synchrocast_publisher_sent(State &state, uint32_t now) {
  state.has_sent = true;
  state.last_sent_ms = now;
  state.send_not_before_ms = 0;
  state.pending = false;
}

template<typename State>
inline void synchrocast_queue_publisher_refresh(State &state, uint32_t now,
                                                uint32_t identity) {
  state.pending = true;
  state.last_send_attempt_ms = 0;
  state.send_not_before_ms =
      now + (identity % (SYNCHROCAST_RECOVERY_JITTER_SPREAD_MS + 1));
}

}  // namespace synchrocast
}  // namespace esphome
