// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "cfx_sync_transport_adapter.h"

#ifdef USE_SYNCHROCAST_CFX_SYNC_BRIDGE

#include "esphome/core/log.h"

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.cfx_transport";
static constexpr uint8_t ESPNOW_BROADCAST_ADDRESS[6] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool CFXSyncTransportAdapter::attach(
    SynchrocastTransportPacketSink *sink) {
  if (sink == nullptr || (this->sink_ != nullptr && this->sink_ != sink)) {
    return false;
  }
  this->sink_ = sink;
  return cfx_sync::global_cfx_sync_bus()
      .register_shared_transport_consumer(this);
}

void CFXSyncTransportAdapter::detach(
    SynchrocastTransportPacketSink *sink) {
  if (this->sink_ == nullptr ||
      (sink != nullptr && this->sink_ != sink)) {
    return;
  }
  cfx_sync::global_cfx_sync_bus().unregister_shared_transport_consumer(this);
  this->sink_ = nullptr;
}

bool CFXSyncTransportAdapter::send_broadcast(
    SynchrocastTransportKind transport, const uint8_t *data, size_t size) {
  auto &bus = cfx_sync::global_cfx_sync_bus();
  if (transport == SynchrocastTransportKind::UDP) {
    return bus.send_udp(data, size);
  }
#ifdef USE_ESPNOW
  if (transport == SynchrocastTransportKind::ESPNOW) {
    return bus.send_espnow(
               ESPNOW_BROADCAST_ADDRESS, data, size,
               [](esp_err_t result) {
                 if (result != ESP_OK) {
                   ESP_LOGV(TAG, "Shared ESP-NOW broadcast failed: %s",
                            esp_err_to_name(result));
                 }
               }) == ESP_OK;
  }
#endif
  return false;
}

bool CFXSyncTransportAdapter::send_to(
    const SynchrocastTransportSource &destination, const uint8_t *data,
    size_t size) {
  if (!destination.identity_valid) {
    return false;
  }
  auto &bus = cfx_sync::global_cfx_sync_bus();
  if (destination.transport == SynchrocastTransportKind::UDP) {
    return bus.send_udp_to(destination.ipv4, destination.port, data, size);
  }
#ifdef USE_ESPNOW
  if (destination.transport == SynchrocastTransportKind::ESPNOW) {
    if (!bus.add_espnow_peer(destination.mac.data())) {
      ESP_LOGV(TAG, "Shared ESP-NOW unicast peer registration failed");
      return false;
    }
    return bus.send_espnow(
               destination.mac.data(), data, size,
               [](esp_err_t result) {
                 if (result != ESP_OK) {
                   ESP_LOGV(TAG, "Shared ESP-NOW unicast failed: %s",
                            esp_err_to_name(result));
                 }
               }) == ESP_OK;
  }
#endif
  return false;
}

SynchrocastTransportBackendStatus CFXSyncTransportAdapter::status() const {
  const auto &bus = cfx_sync::global_cfx_sync_bus();
  SynchrocastTransportBackendStatus status;
  status.owner_present = bus.has_active_group();
  status.api_version = cfx_sync::CFX_SYNC_SHARED_TRANSPORT_API_VERSION;
  status.recovery_generation = bus.recovery_generation();
  if (bus.is_espnow_ready()) {
    status.active_transports |= SYNCHROCAST_TRANSPORT_ESPNOW;
  }
  if (bus.is_udp_ready()) {
    status.active_transports |= SYNCHROCAST_TRANSPORT_UDP;
    status.udp_port = bus.udp_port();
  }
  return status;
}

bool CFXSyncTransportAdapter::on_shared_transport_packet(
    cfx_sync::CFXSyncReceivePath path,
    const cfx_sync::CFXSyncSource &source, const uint8_t *data, size_t size) {
  if (this->sink_ == nullptr) {
    return false;
  }

  SynchrocastTransportSource converted;
  converted.receive_path =
      path == cfx_sync::CFXSyncReceivePath::UNKNOWN_PEER
          ? SynchrocastReceivePath::UNKNOWN_PEER
          : SynchrocastReceivePath::NORMAL;
  converted.identity_valid = source.identity_valid;
  converted.mac = source.mac;
  converted.ipv4 = source.ipv4;
  converted.port = source.port;
  converted.transport =
      source.transport == cfx_sync::CFXSyncTransportKind::ESPNOW
          ? SynchrocastTransportKind::ESPNOW
          : SynchrocastTransportKind::UDP;
  return this->sink_->on_transport_packet(converted, data, size);
}

}  // namespace synchrocast
}  // namespace esphome
#endif
