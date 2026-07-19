// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "synchrocast_types.h"

namespace esphome {
namespace synchrocast {

const char *synchrocast_message_type_to_string(SynchrocastMessageType type);
const char *synchrocast_domain_to_string(SynchrocastDomain domain);
const char *synchrocast_intent_to_string(SynchrocastIntent intent);

}  // namespace synchrocast
}  // namespace esphome
