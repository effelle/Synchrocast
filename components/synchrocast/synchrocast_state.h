// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "synchrocast_types.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace synchrocast {

// Canonical state uses bounded TLV fields: one-byte field ID, one-byte length,
// then the value. Unknown fields are forward-skippable by design.
class CanonicalStateWriter {
 public:
  explicit CanonicalStateWriter(SynchrocastPacket &packet) : packet_(packet) {
    this->packet_.payload_len = 0;
  }

  bool add_bool(uint8_t field, bool value) {
    const uint8_t encoded = value ? 1 : 0;
    return this->add_bytes(field, &encoded, sizeof(encoded));
  }
  bool add_u8(uint8_t field, uint8_t value) {
    return this->add_bytes(field, &value, sizeof(value));
  }
  bool add_u16(uint8_t field, uint16_t value) {
    const uint8_t encoded[2] = {static_cast<uint8_t>(value >> 8),
                                static_cast<uint8_t>(value)};
    return this->add_bytes(field, encoded, sizeof(encoded));
  }
  bool add_float(uint8_t field, float value) {
    if (!std::isfinite(value)) {
      return false;
    }
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    const uint8_t encoded[4] = {
        static_cast<uint8_t>(bits >> 24), static_cast<uint8_t>(bits >> 16),
        static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits)};
    return this->add_bytes(field, encoded, sizeof(encoded));
  }
  bool add_string(uint8_t field, const char *value, size_t length) {
    return length <= UINT8_MAX &&
           this->add_bytes(field, reinterpret_cast<const uint8_t *>(value),
                           static_cast<uint8_t>(length));
  }

 protected:
  bool add_bytes(uint8_t field, const uint8_t *value, uint8_t length) {
    const size_t required = static_cast<size_t>(length) + 2;
    if (field == 0 || value == nullptr ||
        this->packet_.payload_len + required > SYNCHROCAST_MAX_PAYLOAD_SIZE) {
      return false;
    }
    auto *output = this->packet_.payload.raw_bytes + this->packet_.payload_len;
    output[0] = field;
    output[1] = length;
    if (length != 0) {
      memcpy(output + 2, value, length);
    }
    this->packet_.payload_len += static_cast<uint8_t>(required);
    return true;
  }

  SynchrocastPacket &packet_;
};

struct CanonicalStateField {
  uint8_t id{0};
  const uint8_t *data{nullptr};
  uint8_t length{0};
};

class CanonicalStateReader {
 public:
  CanonicalStateReader(const uint8_t *data, size_t size)
      : data_(data), size_(size), valid_(data != nullptr && size != 0) {}

  bool next(CanonicalStateField &field) {
    if (!this->valid_ || this->offset_ == this->size_) {
      return false;
    }
    if (this->size_ - this->offset_ < 2) {
      this->valid_ = false;
      return false;
    }
    const uint8_t id = this->data_[this->offset_];
    const uint8_t length = this->data_[this->offset_ + 1];
    if (id == 0 || this->size_ - this->offset_ - 2 < length) {
      this->valid_ = false;
      return false;
    }
    field.id = id;
    field.length = length;
    field.data = this->data_ + this->offset_ + 2;
    this->offset_ += static_cast<size_t>(length) + 2;
    return true;
  }

  bool valid() const { return this->valid_ && this->offset_ == this->size_; }

 protected:
  const uint8_t *data_{nullptr};
  size_t size_{0};
  size_t offset_{0};
  bool valid_{false};
};

inline bool canonical_payload_is_valid(const uint8_t *data, size_t size) {
  CanonicalStateReader reader(data, size);
  CanonicalStateField field;
  while (reader.next(field)) {
  }
  return reader.valid();
}

inline bool canonical_read_bool(const CanonicalStateField &field,
                                bool &value) {
  if (field.length != 1 ||
      (field.data[0] != 0 && field.data[0] != 1)) {
    return false;
  }
  value = field.data[0] != 0;
  return true;
}

inline bool canonical_read_u8(const CanonicalStateField &field,
                              uint8_t &value) {
  if (field.length != 1) {
    return false;
  }
  value = field.data[0];
  return true;
}

inline bool canonical_read_u16(const CanonicalStateField &field,
                               uint16_t &value) {
  if (field.length != 2) {
    return false;
  }
  value = (static_cast<uint16_t>(field.data[0]) << 8) |
          static_cast<uint16_t>(field.data[1]);
  return true;
}

inline bool canonical_read_float(const CanonicalStateField &field,
                                 float &value) {
  if (field.length != 4) {
    return false;
  }
  const uint32_t bits = (static_cast<uint32_t>(field.data[0]) << 24) |
                        (static_cast<uint32_t>(field.data[1]) << 16) |
                        (static_cast<uint32_t>(field.data[2]) << 8) |
                        static_cast<uint32_t>(field.data[3]);
  memcpy(&value, &bits, sizeof(value));
  return std::isfinite(value);
}

enum class CoverStateField : uint8_t {
  POSITION = 1,
  TILT = 2,
  OPERATION = 3,
};

enum class FanStateField : uint8_t {
  POWER = 1,
  SPEED_PERCENT = 2,
  OSCILLATING = 3,
  DIRECTION = 4,
  PRESET = 5,
};

enum class ValveStateField : uint8_t {
  POSITION = 1,
  OPERATION = 2,
};

}  // namespace synchrocast
}  // namespace esphome
