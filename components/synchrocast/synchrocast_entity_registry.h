// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace synchrocast {

enum class EntityRegistrationResult : uint8_t {
  ADDED,
  ALREADY_REGISTERED,
  INVALID_ENTITY,
  HASH_COLLISION,
  CAPACITY_EXCEEDED,
};

// Entity registration happens during ESPHome setup; packet dispatch only performs
// a bounded linear lookup on the main loop. No heap allocation occurs here.
template<typename EntityT, size_t Capacity> class SynchrocastEntityRegistry {
 public:
  EntityRegistrationResult register_entity(uint32_t entity_hash, EntityT *entity) {
    if (entity == nullptr) {
      return EntityRegistrationResult::INVALID_ENTITY;
    }

    for (size_t i = 0; i < this->size_; i++) {
      if (this->entries_[i].entity_hash != entity_hash) {
        continue;
      }
      return this->entries_[i].entity == entity ? EntityRegistrationResult::ALREADY_REGISTERED
                                                 : EntityRegistrationResult::HASH_COLLISION;
    }

    if (this->size_ >= Capacity) {
      return EntityRegistrationResult::CAPACITY_EXCEEDED;
    }

    this->entries_[this->size_++] = {entity_hash, entity};
    return EntityRegistrationResult::ADDED;
  }

  EntityT *find(uint32_t entity_hash) const {
    size_t ignored_index;
    return this->find(entity_hash, ignored_index);
  }

  EntityT *find(uint32_t entity_hash, size_t &index) const {
    for (size_t i = 0; i < this->size_; i++) {
      if (this->entries_[i].entity_hash == entity_hash) {
        index = i;
        return this->entries_[i].entity;
      }
    }
    index = Capacity;
    return nullptr;
  }

  size_t size() const { return this->size_; }
  static constexpr size_t capacity() { return Capacity; }
  EntityT *entity_at(size_t index) const {
    return index < this->size_ ? this->entries_[index].entity : nullptr;
  }
  uint32_t hash_at(size_t index) const {
    return index < this->size_ ? this->entries_[index].entity_hash : 0;
  }

 protected:
  struct Entry {
    uint32_t entity_hash{0};
    EntityT *entity{nullptr};
  };

  std::array<Entry, Capacity> entries_{};
  size_t size_{0};
};

}  // namespace synchrocast
}  // namespace esphome
