// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "synchrocast_packet_codec.h"

#include <cmath>
#include <cstring>

namespace esphome {
namespace synchrocast {

static constexpr uint8_t SYNCHROCAST_MAGIC[4] = {'S', 'C', 'S', 'T'};

void SynchrocastPacketCodec::write_u16_(uint8_t *output, uint16_t value) {
  output[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
  output[1] = static_cast<uint8_t>(value & 0xFF);
}

void SynchrocastPacketCodec::write_u32_(uint8_t *output, uint32_t value) {
  output[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  output[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  output[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  output[3] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t SynchrocastPacketCodec::read_u16_(const uint8_t *data) {
  return (static_cast<uint16_t>(data[0]) << 8) |
         static_cast<uint16_t>(data[1]);
}

uint32_t SynchrocastPacketCodec::read_u32_(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

void SynchrocastPacketCodec::calculate_tag_(
    const uint8_t *data, size_t size, const std::array<uint8_t, 32> &key,
    uint8_t *tag) {
  hmac_sha256::HmacSHA256 hmac;
  hmac.init(key.data(), key.size());
  hmac.add(data, size);
  hmac.calculate();

  uint8_t digest[32];
  hmac.get_bytes(digest);
  memcpy(tag, digest, AUTH_TAG_SIZE);
}

bool SynchrocastPacketCodec::tags_equal_(const uint8_t *left,
                                         const uint8_t *right, size_t size) {
  uint8_t difference = 0;
  for (size_t i = 0; i < size; i++) {
    difference |= left[i] ^ right[i];
  }
  return difference == 0;
}

bool SynchrocastPacketCodec::uses_u32_payload_(
    const SynchrocastPacket &packet) {
  if (packet.payload_len != sizeof(uint32_t)) {
    return false;
  }
  switch (packet.intent) {
    case SynchrocastIntent::SET_POSITION:
    case SynchrocastIntent::SET_SPEED:
    case SynchrocastIntent::SET_VOLUME:
    case SynchrocastIntent::SET_TEMP:
    case SynchrocastIntent::SET_VALUE:
    case SynchrocastIntent::SET_MODE:
    case SynchrocastIntent::SET_OPTION:
      return true;
    default:
      return packet.domain == SynchrocastDomain::SENSOR;
  }
}

bool SynchrocastPacketCodec::is_valid_packet_(
    const SynchrocastPacket &packet) {
  if (packet.payload_len > SYNCHROCAST_MAX_PAYLOAD_SIZE) {
    return false;
  }
  if (packet.source_role > SynchrocastRole::SATELLITE) {
    return false;
  }

  if (packet.msg_type == SynchrocastMessageType::HEARTBEAT) {
    return packet.domain == SynchrocastDomain::UNKNOWN &&
           packet.entity_hash == 0 && packet.intent == SynchrocastIntent::NONE &&
           packet.payload_len == 0;
  }

  if (packet.msg_type != SynchrocastMessageType::STATE_BROADCAST &&
      packet.msg_type != SynchrocastMessageType::INTENT_REQUEST) {
    return false;
  }
  if (packet.domain == SynchrocastDomain::UNKNOWN ||
      packet.domain > SynchrocastDomain::TEXT_SENSOR ||
      packet.entity_hash == 0) {
    return false;
  }

  const bool unavailable = packet.intent == SynchrocastIntent::NONE &&
                           packet.payload_len == 0;
  if (packet.domain == SynchrocastDomain::SENSOR) {
    return packet.msg_type == SynchrocastMessageType::STATE_BROADCAST &&
           (unavailable ||
            (packet.intent == SynchrocastIntent::SET_VALUE &&
             packet.payload_len == sizeof(float) &&
             std::isfinite(packet.payload.float_val)));
  }
  if (packet.domain == SynchrocastDomain::BINARY_SENSOR) {
    return packet.msg_type == SynchrocastMessageType::STATE_BROADCAST &&
           (unavailable ||
            (packet.intent == SynchrocastIntent::SET_VALUE &&
             packet.payload_len == 1 &&
             (packet.payload.raw_bytes[0] == 0 ||
              packet.payload.raw_bytes[0] == 1)));
  }
  if (packet.domain == SynchrocastDomain::TEXT_SENSOR) {
    return packet.msg_type == SynchrocastMessageType::STATE_BROADCAST &&
           (unavailable ||
            (packet.intent == SynchrocastIntent::SET_VALUE &&
             is_valid_utf8(packet.payload.raw_bytes, packet.payload_len)));
  }
  return true;
}

bool SynchrocastPacketCodec::encode(
    const SynchrocastPacket &packet, uint32_t group_hash, uint32_t boot_id,
    uint32_t sequence, const std::array<uint8_t, 32> &key,
    std::array<uint8_t, MAX_FRAME_SIZE> &output, size_t &output_size) {
  output_size = 0;
  if (boot_id == 0 || sequence == 0 || !is_valid_packet_(packet)) {
    return false;
  }

  memcpy(output.data(), SYNCHROCAST_MAGIC, sizeof(SYNCHROCAST_MAGIC));
  output[4] = VERSION;
  output[5] = static_cast<uint8_t>(packet.msg_type);
  output[6] = static_cast<uint8_t>(packet.domain);
  output[7] = static_cast<uint8_t>(packet.intent);
  output[8] = static_cast<uint8_t>(packet.source_role);
  output[9] = 0;  // Reserved flags.
  write_u16_(output.data() + 10, packet.payload_len);
  write_u32_(output.data() + 12, group_hash);
  write_u32_(output.data() + 16, boot_id);
  write_u32_(output.data() + 20, sequence);
  write_u32_(output.data() + 24, packet.entity_hash);

  uint8_t *wire_payload = output.data() + HEADER_SIZE;
  if (uses_u32_payload_(packet)) {
    uint32_t value;
    memcpy(&value, packet.payload.raw_bytes, sizeof(value));
    write_u32_(wire_payload, value);
  } else if (packet.payload_len != 0) {
    memcpy(wire_payload, packet.payload.raw_bytes, packet.payload_len);
  }

  const size_t authenticated_size = HEADER_SIZE + packet.payload_len;
  calculate_tag_(output.data(), authenticated_size, key,
                 output.data() + authenticated_size);
  output_size = authenticated_size + AUTH_TAG_SIZE;
  return true;
}

SynchrocastDecodeResult SynchrocastPacketCodec::decode(
    const uint8_t *data, size_t size, uint32_t expected_group_hash,
    const std::array<uint8_t, 32> &key, SynchrocastPacket &packet) {
  if (data == nullptr || size < sizeof(SYNCHROCAST_MAGIC)) {
    return SynchrocastDecodeResult::NOT_SYNCHROCAST;
  }
  if (memcmp(data, SYNCHROCAST_MAGIC, sizeof(SYNCHROCAST_MAGIC)) != 0) {
    return SynchrocastDecodeResult::NOT_SYNCHROCAST;
  }
  if (size < HEADER_SIZE + AUTH_TAG_SIZE || data[9] != 0) {
    return SynchrocastDecodeResult::MALFORMED;
  }
  if (data[4] != VERSION) {
    return SynchrocastDecodeResult::UNSUPPORTED_VERSION;
  }

  const uint16_t payload_size = read_u16_(data + 10);
  if (payload_size > SYNCHROCAST_WIRE_MAX_PAYLOAD_SIZE ||
      HEADER_SIZE + payload_size + AUTH_TAG_SIZE != size) {
    return SynchrocastDecodeResult::MALFORMED;
  }
  if (read_u32_(data + 12) != expected_group_hash) {
    return SynchrocastDecodeResult::WRONG_GROUP;
  }

  const size_t authenticated_size = HEADER_SIZE + payload_size;
  uint8_t expected_tag[AUTH_TAG_SIZE];
  calculate_tag_(data, authenticated_size, key, expected_tag);
  if (!tags_equal_(expected_tag, data + authenticated_size,
                   AUTH_TAG_SIZE)) {
    return SynchrocastDecodeResult::BAD_AUTH;
  }

  const uint8_t raw_type = data[5];
  const uint8_t raw_domain = data[6];
  const uint8_t raw_intent = data[7];
  const uint8_t raw_role = data[8];
  if (raw_type > static_cast<uint8_t>(SynchrocastMessageType::INTENT_REQUEST) ||
      raw_domain > static_cast<uint8_t>(SynchrocastDomain::TEXT_SENSOR) ||
      raw_intent > static_cast<uint8_t>(SynchrocastIntent::SET_OPTION) ||
      raw_role > static_cast<uint8_t>(SynchrocastRole::SATELLITE)) {
    return SynchrocastDecodeResult::UNSUPPORTED_TYPE;
  }
#ifndef USE_SYNCHROCAST_TEXT_SENSOR
  if (raw_domain == static_cast<uint8_t>(SynchrocastDomain::TEXT_SENSOR)) {
    return SynchrocastDecodeResult::UNSUPPORTED_TYPE;
  }
#endif
  if (payload_size > SYNCHROCAST_MAX_PAYLOAD_SIZE) {
    return SynchrocastDecodeResult::UNSUPPORTED_TYPE;
  }

  packet = {};
  packet.msg_type = static_cast<SynchrocastMessageType>(raw_type);
  packet.domain = static_cast<SynchrocastDomain>(raw_domain);
  packet.intent = static_cast<SynchrocastIntent>(raw_intent);
  packet.source_role = static_cast<SynchrocastRole>(raw_role);
  packet.source_boot_id = read_u32_(data + 16);
  packet.entity_hash = read_u32_(data + 24);
  packet.payload_len = static_cast<uint8_t>(payload_size);
  const uint32_t sequence = read_u32_(data + 20);
  if (packet.source_boot_id == 0 || sequence == 0) {
    return SynchrocastDecodeResult::MALFORMED;
  }

  const uint8_t *wire_payload = data + HEADER_SIZE;
  if (uses_u32_payload_(packet)) {
    const uint32_t value = read_u32_(wire_payload);
    memcpy(packet.payload.raw_bytes, &value, sizeof(value));
  } else if (payload_size != 0) {
    memcpy(packet.payload.raw_bytes, wire_payload, payload_size);
  }

  return is_valid_packet_(packet) ? SynchrocastDecodeResult::OK
                                  : SynchrocastDecodeResult::MALFORMED;
}

bool SynchrocastPacketCodec::is_valid_utf8(const uint8_t *data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    const uint8_t first = data[offset++];
    if (first <= 0x7F) {
      continue;
    }
    if (first >= 0xC2 && first <= 0xDF) {
      if (offset >= size || (data[offset] & 0xC0) != 0x80) {
        return false;
      }
      offset++;
      continue;
    }
    if (first >= 0xE0 && first <= 0xEF) {
      if (offset + 1 >= size) {
        return false;
      }
      const uint8_t second = data[offset];
      const uint8_t third = data[offset + 1];
      if ((third & 0xC0) != 0x80 ||
          (first == 0xE0 && (second < 0xA0 || second > 0xBF)) ||
          (first == 0xED && (second < 0x80 || second > 0x9F)) ||
          (first != 0xE0 && first != 0xED &&
           (second & 0xC0) != 0x80)) {
        return false;
      }
      offset += 2;
      continue;
    }
    if (first >= 0xF0 && first <= 0xF4) {
      if (offset + 2 >= size) {
        return false;
      }
      const uint8_t second = data[offset];
      if ((data[offset + 1] & 0xC0) != 0x80 ||
          (data[offset + 2] & 0xC0) != 0x80 ||
          (first == 0xF0 && (second < 0x90 || second > 0xBF)) ||
          (first == 0xF4 && (second < 0x80 || second > 0x8F)) ||
          (first != 0xF0 && first != 0xF4 &&
           (second & 0xC0) != 0x80)) {
        return false;
      }
      offset += 3;
      continue;
    }
    return false;
  }
  return true;
}

}  // namespace synchrocast
}  // namespace esphome
