// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "synchrocast_transport_runtime.h"

namespace esphome {
namespace synchrocast {

bool SynchrocastTransportRuntime::configure(
    SynchrocastTransportOwner owner,
    SynchrocastRequestedTransport requested_transport,
    uint16_t requested_udp_port) {
  if (this->configured_) {
    return this->status_.owner == owner &&
           this->requested_transport_ == requested_transport &&
           this->requested_udp_port_ == requested_udp_port &&
           this->status_.state != SynchrocastTransportState::BLOCKED;
  }

  this->configured_ = true;
  this->status_.owner = owner;
  this->requested_transport_ = requested_transport;
  this->requested_udp_port_ = requested_udp_port;

  if (this->sink_ == nullptr) {
    this->block_();
    return false;
  }

  if (owner == SynchrocastTransportOwner::CFX_SYNC) {
    this->active_backend_ = this->cfx_sync_backend_;
    this->status_.state =
        SynchrocastTransportState::WAITING_FOR_CFX_SYNC;
  } else if (owner == SynchrocastTransportOwner::SYNCHROCAST) {
    this->active_backend_ = this->standalone_backend_;
    this->status_.state = this->active_backend_ == nullptr
                              ? SynchrocastTransportState::STANDALONE_PENDING
                              : SynchrocastTransportState::STANDALONE_ACTIVE;
  } else {
    this->block_();
    return false;
  }

  // The standalone transport codec is intentionally a separate implementation
  // step. Selecting standalone ownership remains valid without a backend so
  // decoded packets can still be injected into the application dispatcher.
  if (this->active_backend_ == nullptr) {
    if (owner == SynchrocastTransportOwner::CFX_SYNC) {
      this->block_();
      return false;
    }
    return true;
  }

  if (!this->active_backend_->attach(this->sink_)) {
    this->block_();
    return false;
  }

  this->refresh();
  return this->status_.state != SynchrocastTransportState::BLOCKED;
}

void SynchrocastTransportRuntime::refresh() {
  if (!this->configured_ || this->active_backend_ == nullptr ||
      this->status_.state == SynchrocastTransportState::BLOCKED) {
    return;
  }

  const auto backend_status = this->active_backend_->status();
  this->status_.active_transports = backend_status.active_transports;
  this->status_.udp_port = backend_status.udp_port;

  if (this->status_.owner == SynchrocastTransportOwner::CFX_SYNC) {
    if (!backend_status.owner_present ||
        backend_status.active_transports == 0) {
      this->status_.state =
          SynchrocastTransportState::WAITING_FOR_CFX_SYNC;
      return;
    }
    if (!this->backend_matches_request_(backend_status)) {
      this->block_();
      return;
    }
    this->status_.state =
        SynchrocastTransportState::ATTACHED_TO_CFX_SYNC;
    return;
  }

  if (!this->backend_matches_request_(backend_status)) {
    this->block_();
    return;
  }
  this->status_.state = SynchrocastTransportState::STANDALONE_ACTIVE;
}

bool SynchrocastTransportRuntime::send_broadcast(
    SynchrocastTransportKind transport, const uint8_t *data, size_t size) {
  if (!this->can_send_(transport, data, size)) {
    return false;
  }
  return this->active_backend_->send_broadcast(transport, data, size);
}

bool SynchrocastTransportRuntime::send_to(
    const SynchrocastTransportSource &destination, const uint8_t *data,
    size_t size) {
  if (!destination.identity_valid ||
      !this->can_send_(destination.transport, data, size)) {
    return false;
  }
  return this->active_backend_->send_to(destination, data, size);
}

bool SynchrocastTransportRuntime::backend_matches_request_(
    const SynchrocastTransportBackendStatus &backend_status) const {
  if (this->requested_transport_ == SynchrocastRequestedTransport::ESPNOW &&
      (backend_status.active_transports & SYNCHROCAST_TRANSPORT_ESPNOW) == 0) {
    return false;
  }
  if (this->requested_transport_ == SynchrocastRequestedTransport::UDP &&
      (backend_status.active_transports & SYNCHROCAST_TRANSPORT_UDP) == 0) {
    return false;
  }
  if (this->requested_udp_port_ != 0) {
    if ((backend_status.active_transports & SYNCHROCAST_TRANSPORT_UDP) == 0 ||
        backend_status.udp_port != this->requested_udp_port_) {
      return false;
    }
  }
  return true;
}

bool SynchrocastTransportRuntime::can_send_(
    SynchrocastTransportKind transport, const uint8_t *data,
    size_t size) const {
  if (this->active_backend_ == nullptr || data == nullptr || size == 0 ||
      size > SYNCHROCAST_TRANSPORT_MTU) {
    return false;
  }
  if (this->status_.state !=
          SynchrocastTransportState::STANDALONE_ACTIVE &&
      this->status_.state !=
          SynchrocastTransportState::ATTACHED_TO_CFX_SYNC) {
    return false;
  }
  const uint8_t required =
      transport == SynchrocastTransportKind::ESPNOW
          ? SYNCHROCAST_TRANSPORT_ESPNOW
          : transport == SynchrocastTransportKind::UDP
                ? SYNCHROCAST_TRANSPORT_UDP
                : 0;
  return required != 0 &&
         (this->status_.active_transports & required) != 0;
}

void SynchrocastTransportRuntime::block_() {
  if (this->active_backend_ != nullptr && this->sink_ != nullptr) {
    this->active_backend_->detach(this->sink_);
  }
  this->status_.state = SynchrocastTransportState::BLOCKED;
  this->status_.active_transports = 0;
  this->status_.udp_port = 0;
}

const char *synchrocast_transport_owner_to_string(
    SynchrocastTransportOwner owner) {
  switch (owner) {
    case SynchrocastTransportOwner::SYNCHROCAST:
      return "Synchrocast";
    case SynchrocastTransportOwner::CFX_SYNC:
      return "cfx_sync";
    default:
      return "none";
  }
}

const char *synchrocast_transport_state_to_string(
    SynchrocastTransportState state) {
  switch (state) {
    case SynchrocastTransportState::STANDALONE_PENDING:
      return "standalone backend pending";
    case SynchrocastTransportState::STANDALONE_ACTIVE:
      return "standalone active";
    case SynchrocastTransportState::WAITING_FOR_CFX_SYNC:
      return "waiting for cfx_sync";
    case SynchrocastTransportState::ATTACHED_TO_CFX_SYNC:
      return "attached to cfx_sync";
    case SynchrocastTransportState::BLOCKED:
      return "blocked";
    default:
      return "unconfigured";
  }
}

}  // namespace synchrocast
}  // namespace esphome
