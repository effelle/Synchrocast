// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "synchrocast_transport.h"

namespace esphome {
namespace synchrocast {

class SynchrocastTransportRuntime {
 public:
  void set_sink(SynchrocastTransportPacketSink *sink) { this->sink_ = sink; }
  void set_standalone_backend(SynchrocastTransportBackend *backend) {
    this->standalone_backend_ = backend;
  }
  void set_cfx_sync_backend(SynchrocastTransportBackend *backend) {
    this->cfx_sync_backend_ = backend;
  }

  bool configure(SynchrocastTransportOwner owner,
                 SynchrocastRequestedTransport requested_transport,
                 uint16_t requested_udp_port);
  void refresh();
  bool send_broadcast(SynchrocastTransportKind transport,
                      const uint8_t *data, size_t size);
  bool send_to(const SynchrocastTransportSource &destination,
               const uint8_t *data, size_t size);

  SynchrocastTransportStatus status() const { return this->status_; }

 protected:
  bool backend_matches_request_(
      const SynchrocastTransportBackendStatus &backend_status) const;
  bool can_send_(SynchrocastTransportKind transport, const uint8_t *data,
                 size_t size) const;
  void block_();

  SynchrocastTransportPacketSink *sink_{nullptr};
  SynchrocastTransportBackend *standalone_backend_{nullptr};
  SynchrocastTransportBackend *cfx_sync_backend_{nullptr};
  SynchrocastTransportBackend *active_backend_{nullptr};
  SynchrocastTransportStatus status_{};
  SynchrocastRequestedTransport requested_transport_{
      SynchrocastRequestedTransport::AUTO};
  uint16_t requested_udp_port_{0};
  bool configured_{false};
};

}  // namespace synchrocast
}  // namespace esphome
