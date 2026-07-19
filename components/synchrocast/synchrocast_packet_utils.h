// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "synchrocast_types.h"

#include <cmath>

namespace esphome {
namespace synchrocast {

inline bool read_finite_float_payload(const SynchrocastPacket &packet, float &value) {
  if (packet.payload_len != sizeof(float)) {
    return false;
  }
  value = packet.payload.float_val;
  return std::isfinite(value);
}

}  // namespace synchrocast
}  // namespace esphome
