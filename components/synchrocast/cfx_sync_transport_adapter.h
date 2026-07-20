// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "synchrocast_transport.h"

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_CFX_SYNC_BRIDGE
#include "esphome/components/cfx_sync/cfx_sync_bus.h"

namespace esphome {
namespace synchrocast {

static_assert(cfx_sync::CFX_SYNC_SHARED_TRANSPORT_API_VERSION == 1,
              "Unsupported cfx_sync shared transport API");
static_assert(SYNCHROCAST_TRANSPORT_MTU <=
                  cfx_sync::CFX_SYNC_SHARED_TRANSPORT_MTU,
              "Synchrocast frame exceeds the cfx_sync shared MTU");

class CFXSyncTransportAdapter final
    : public SynchrocastTransportBackend,
      public cfx_sync::CFXSyncSharedTransportConsumer {
 public:
  bool attach(SynchrocastTransportPacketSink *sink) override;
  void detach(SynchrocastTransportPacketSink *sink) override;
  bool send_broadcast(SynchrocastTransportKind transport,
                      const uint8_t *data, size_t size) override;
  bool send_to(const SynchrocastTransportSource &destination,
               const uint8_t *data, size_t size) override;
  SynchrocastTransportBackendStatus status() const override;
  void loop() override;

  bool on_shared_transport_packet(cfx_sync::CFXSyncReceivePath path,
                                  const cfx_sync::CFXSyncSource &source,
                                  const uint8_t *data, size_t size) override;

 protected:
  SynchrocastTransportPacketSink *sink_{nullptr};
#if defined(USE_ESP32) && defined(USE_ESPNOW)
  static constexpr uint32_t CHANNEL_STABLE_MS = 1500;
  uint32_t recovery_generation_{0};
  uint32_t pending_channel_since_ms_{0};
  uint8_t observed_channel_{0};
  uint8_t pending_channel_{0};
  bool channel_seen_{false};
#endif
};

}  // namespace synchrocast
}  // namespace esphome
#endif
