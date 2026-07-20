// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "synchrocast_transport.h"
#include "synchrocast_types.h"

#include "esphome/components/hmac_sha256/hmac_sha256.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace synchrocast {

enum class SynchrocastDecodeResult : uint8_t {
  OK,
  NOT_SYNCHROCAST,
  WRONG_GROUP,
  MALFORMED,
  UNSUPPORTED_VERSION,
  UNSUPPORTED_TYPE,
  BAD_AUTH,
};

class SynchrocastPacketCodec final {
 public:
  static constexpr uint8_t VERSION = 1;
  static constexpr size_t HEADER_SIZE = 28;
  static constexpr size_t AUTH_TAG_SIZE = 16;
  static constexpr size_t MAX_FRAME_SIZE =
      HEADER_SIZE + SYNCHROCAST_WIRE_MAX_PAYLOAD_SIZE + AUTH_TAG_SIZE;

  static bool encode(const SynchrocastPacket &packet, uint32_t group_hash,
                     uint32_t boot_id, uint32_t sequence,
                     const std::array<uint8_t, 32> &key,
                     std::array<uint8_t, MAX_FRAME_SIZE> &output,
                     size_t &output_size);

  static SynchrocastDecodeResult decode(
      const uint8_t *data, size_t size, uint32_t expected_group_hash,
      const std::array<uint8_t, 32> &key, SynchrocastPacket &packet);

  static bool is_valid_utf8(const uint8_t *data, size_t size);

 protected:
  static bool is_valid_packet_(const SynchrocastPacket &packet);
  static bool uses_u32_payload_(const SynchrocastPacket &packet);
  static void write_u16_(uint8_t *output, uint16_t value);
  static void write_u32_(uint8_t *output, uint32_t value);
  static uint16_t read_u16_(const uint8_t *data);
  static uint32_t read_u32_(const uint8_t *data);
  static void calculate_tag_(const uint8_t *data, size_t size,
                             const std::array<uint8_t, 32> &key,
                             uint8_t *tag);
  static bool tags_equal_(const uint8_t *left, const uint8_t *right,
                          size_t size);
};

static_assert(SynchrocastPacketCodec::MAX_FRAME_SIZE <=
                  SYNCHROCAST_TRANSPORT_MTU,
              "Synchrocast authenticated frame exceeds transport MTU");

}  // namespace synchrocast
}  // namespace esphome
