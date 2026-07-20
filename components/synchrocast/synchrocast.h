// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "esphome/core/defines.h"

#include "synchrocast_component.h"
#ifdef USE_SYNCHROCAST_COVER
#include "cover_handler.h"
#endif
#ifdef USE_SYNCHROCAST_FAN
#include "fan_handler.h"
#endif
#include "synchrocast_dispatcher.h"
#include "synchrocast_transport.h"
#include "synchrocast_transport_runtime.h"
#include "synchrocast_types.h"
#ifdef USE_SYNCHROCAST_VALVE
#include "valve_handler.h"
#endif
