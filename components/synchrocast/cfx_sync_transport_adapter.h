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

  bool on_shared_transport_packet(cfx_sync::CFXSyncReceivePath path,
                                  const cfx_sync::CFXSyncSource &source,
                                  const uint8_t *data, size_t size) override;

 protected:
  SynchrocastTransportPacketSink *sink_{nullptr};
};

}  // namespace synchrocast
}  // namespace esphome
#endif
