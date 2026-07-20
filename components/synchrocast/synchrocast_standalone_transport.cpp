// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_STANDALONE_TRANSPORT

#include "synchrocast_standalone_transport.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#if defined(USE_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <unistd.h>
#endif

#include <cstring>

namespace esphome {
namespace synchrocast {

static const char *const TAG = "synchrocast.transport";
static constexpr size_t UDP_RX_BUFFER_SIZE = SYNCHROCAST_TRANSPORT_MTU + 1;
#ifdef USE_ESPNOW
static constexpr uint8_t ESPNOW_BROADCAST_ADDRESS[6] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#endif

SynchrocastStandaloneTransport &global_synchrocast_standalone_transport() {
  static SynchrocastStandaloneTransport transport;
  return transport;
}

SynchrocastStandaloneTransport::~SynchrocastStandaloneTransport() {
  this->close_udp_();
}

SynchrocastTransportKind SynchrocastStandaloneTransport::resolve_kind_(
    SynchrocastRequestedTransport requested) const {
  if (requested == SynchrocastRequestedTransport::ESPNOW) {
    return SynchrocastTransportKind::ESPNOW;
  }
  if (requested == SynchrocastRequestedTransport::UDP) {
    return SynchrocastTransportKind::UDP;
  }
#if defined(USE_ESP32) && defined(USE_ESPNOW)
  return SynchrocastTransportKind::ESPNOW;
#else
  return SynchrocastTransportKind::UDP;
#endif
}

bool SynchrocastStandaloneTransport::configure(
    SynchrocastRequestedTransport requested, uint16_t udp_port) {
  const auto kind = this->resolve_kind_(requested);
  const uint16_t resolved_port =
      kind == SynchrocastTransportKind::UDP
          ? (udp_port == 0 ? SYNCHROCAST_DEFAULT_UDP_PORT : udp_port)
          : 0;
  if (this->configured_) {
    return this->kind_ == kind && this->udp_port_ == resolved_port &&
           this->ready_;
  }

  this->configured_ = true;
  this->kind_ = kind;
  this->udp_port_ = resolved_port;
  if (kind == SynchrocastTransportKind::ESPNOW) {
#ifdef USE_ESPNOW
    this->ready_ = this->begin_espnow_();
#else
    this->ready_ = false;
#endif
  } else if (kind == SynchrocastTransportKind::UDP) {
    this->ready_ = this->begin_udp_(resolved_port);
  }
  return this->ready_;
}

bool SynchrocastStandaloneTransport::attach(
    SynchrocastTransportPacketSink *sink) {
  if (!this->ready_ || sink == nullptr) {
    return false;
  }
  for (uint8_t i = 0; i < this->sink_count_; i++) {
    if (this->sinks_[i] == sink) {
      return true;
    }
  }
  if (this->sink_count_ >= MAX_SINKS) {
    ESP_LOGE(TAG, "Maximum standalone Synchrocast group count reached");
    return false;
  }
  this->sinks_[this->sink_count_++] = sink;
  return true;
}

void SynchrocastStandaloneTransport::detach(
    SynchrocastTransportPacketSink *sink) {
  if (sink == nullptr) {
    return;
  }
  for (uint8_t i = 0; i < this->sink_count_; i++) {
    if (this->sinks_[i] != sink) {
      continue;
    }
    for (uint8_t j = i + 1; j < this->sink_count_; j++) {
      this->sinks_[j - 1] = this->sinks_[j];
    }
    this->sinks_[--this->sink_count_] = nullptr;
    return;
  }
}

SynchrocastTransportBackendStatus
SynchrocastStandaloneTransport::status() const {
  SynchrocastTransportBackendStatus result;
  result.owner_present = this->sink_count_ != 0;
  result.api_version = 1;
  if (!this->ready_) {
    return result;
  }
  if (this->kind_ == SynchrocastTransportKind::ESPNOW) {
    result.active_transports = SYNCHROCAST_TRANSPORT_ESPNOW;
  } else if (this->kind_ == SynchrocastTransportKind::UDP) {
    result.active_transports = SYNCHROCAST_TRANSPORT_UDP;
    result.udp_port = this->udp_port_;
  }
  return result;
}

bool SynchrocastStandaloneTransport::dispatch_(
    const SynchrocastTransportSource &source, const uint8_t *data,
    size_t size) {
  if (data == nullptr || size == 0 || size > SYNCHROCAST_TRANSPORT_MTU) {
    return false;
  }
  for (uint8_t i = 0; i < this->sink_count_; i++) {
    auto *sink = this->sinks_[i];
    if (sink != nullptr && sink->on_transport_packet(source, data, size)) {
      return true;
    }
  }
  return false;
}

void SynchrocastStandaloneTransport::loop() {
  if (this->kind_ == SynchrocastTransportKind::UDP) {
    const uint32_t now = millis();
    if (this->last_udp_poll_ms_ == now) {
      return;
    }
    this->last_udp_poll_ms_ = now;
    this->poll_udp_();
  }
}

bool SynchrocastStandaloneTransport::send_broadcast(
    SynchrocastTransportKind transport, const uint8_t *data, size_t size) {
  if (!this->ready_ || transport != this->kind_ || data == nullptr ||
      size == 0 || size > SYNCHROCAST_TRANSPORT_MTU) {
    return false;
  }
  if (transport == SynchrocastTransportKind::UDP) {
    return this->send_udp_broadcast_(data, size);
  }
#ifdef USE_ESPNOW
  return this->send_espnow_(ESPNOW_BROADCAST_ADDRESS, data, size);
#else
  return false;
#endif
}

bool SynchrocastStandaloneTransport::send_to(
    const SynchrocastTransportSource &destination, const uint8_t *data,
    size_t size) {
  if (!this->ready_ || !destination.identity_valid ||
      destination.transport != this->kind_ || data == nullptr || size == 0 ||
      size > SYNCHROCAST_TRANSPORT_MTU) {
    return false;
  }
  if (destination.transport == SynchrocastTransportKind::UDP) {
    return this->send_udp_(destination.ipv4, destination.port, data, size);
  }
#ifdef USE_ESPNOW
  const esp_err_t peer_result =
      this->espnow_->add_peer(destination.mac.data());
  if (peer_result != ESP_OK && peer_result != ESP_ERR_ESPNOW_EXIST) {
    return false;
  }
  return this->send_espnow_(destination.mac.data(), data, size);
#else
  return false;
#endif
}

#ifdef USE_ESPNOW
bool SynchrocastStandaloneTransport::begin_espnow_() {
  if (this->espnow_ == nullptr) {
    ESP_LOGE(TAG, "Standalone ESP-NOW component is unavailable");
    return false;
  }
  if (!this->espnow_registered_) {
    this->espnow_->register_receive_handler(this);
    this->espnow_->register_unknown_peer_handler(this);
    this->espnow_->register_broadcast_handler(this);
    this->espnow_registered_ = true;
  }
  const esp_err_t result = this->espnow_->add_peer(ESPNOW_BROADCAST_ADDRESS);
  if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
    ESP_LOGV(TAG, "ESP-NOW broadcast peer add skipped: %s",
             esp_err_to_name(result));
  }
  ESP_LOGI(TAG, "Standalone ESP-NOW transport ready");
  return true;
}

bool SynchrocastStandaloneTransport::send_espnow_(
    const uint8_t *mac, const uint8_t *data, size_t size) {
  if (this->espnow_ == nullptr) {
    return false;
  }
  return this->espnow_->send(
             mac, data, size, [](esp_err_t result) {
               if (result != ESP_OK) {
                 ESP_LOGV(TAG, "Standalone ESP-NOW send failed: %s",
                          esp_err_to_name(result));
               }
             }) == ESP_OK;
}

bool SynchrocastStandaloneTransport::on_receive(
    const espnow::ESPNowRecvInfo &info, const uint8_t *data,
    SynchrocastESPNowPacketSize size) {
  SynchrocastTransportSource source;
  source.transport = SynchrocastTransportKind::ESPNOW;
  source.identity_valid = true;
  memcpy(source.mac.data(), info.src_addr, source.mac.size());
  return this->dispatch_(source, data, size);
}

bool SynchrocastStandaloneTransport::on_unknown_peer(
    const espnow::ESPNowRecvInfo &info, const uint8_t *data,
    SynchrocastESPNowPacketSize size) {
  SynchrocastTransportSource source;
  source.transport = SynchrocastTransportKind::ESPNOW;
  source.receive_path = SynchrocastReceivePath::UNKNOWN_PEER;
  source.identity_valid = true;
  memcpy(source.mac.data(), info.src_addr, source.mac.size());
  return this->dispatch_(source, data, size);
}

bool SynchrocastStandaloneTransport::on_broadcast(
    const espnow::ESPNowRecvInfo &info, const uint8_t *data,
    SynchrocastESPNowPacketSize size) {
  return this->on_receive(info, data, size);
}
#endif

void SynchrocastStandaloneTransport::close_udp_() {
#if defined(USE_ESP8266)
  this->udp_.stop();
#else
  if (this->socket_fd_ >= 0) {
    ::close(this->socket_fd_);
    this->socket_fd_ = -1;
  }
#endif
  if (this->kind_ == SynchrocastTransportKind::UDP) {
    this->ready_ = false;
  }
}

bool SynchrocastStandaloneTransport::begin_udp_(uint16_t port) {
  this->close_udp_();
#if defined(USE_ESP8266)
  if (!this->udp_.begin(port)) {
    ESP_LOGW(TAG, "Failed to bind standalone UDP port %u",
             static_cast<unsigned>(port));
    return false;
  }
#else
  this->socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (this->socket_fd_ < 0) {
    ESP_LOGW(TAG, "Failed to create standalone UDP socket: errno=%d", errno);
    return false;
  }
  int enabled = 1;
  ::setsockopt(this->socket_fd_, SOL_SOCKET, SO_REUSEADDR, &enabled,
               sizeof(enabled));
  if (::setsockopt(this->socket_fd_, SOL_SOCKET, SO_BROADCAST, &enabled,
                   sizeof(enabled)) < 0) {
    this->close_udp_();
    return false;
  }
  const int flags = ::fcntl(this->socket_fd_, F_GETFL, 0);
  if (flags >= 0) {
    ::fcntl(this->socket_fd_, F_SETFL, flags | O_NONBLOCK);
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = INADDR_ANY;
  if (::bind(this->socket_fd_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0) {
    ESP_LOGW(TAG, "Failed to bind standalone UDP port %u: errno=%d",
             static_cast<unsigned>(port), errno);
    this->close_udp_();
    return false;
  }
#endif
  ESP_LOGI(TAG, "Standalone UDP transport listening on port %u",
           static_cast<unsigned>(port));
  return true;
}

void SynchrocastStandaloneTransport::poll_udp_() {
  if (!this->ready_) {
    return;
  }
  uint8_t buffer[UDP_RX_BUFFER_SIZE];
  for (uint8_t processed = 0; processed < MAX_UDP_PACKETS_PER_LOOP;
       processed++) {
#if defined(USE_ESP8266)
    const int packet_size = this->udp_.parsePacket();
    if (packet_size <= 0) {
      return;
    }
    if (packet_size > static_cast<int>(SYNCHROCAST_TRANSPORT_MTU)) {
      this->udp_.flush();
      continue;
    }
    const int received = this->udp_.read(buffer, sizeof(buffer));
    if (received <= 0) {
      continue;
    }
    SynchrocastTransportSource source;
    source.transport = SynchrocastTransportKind::UDP;
    source.identity_valid = true;
    source.ipv4 = static_cast<uint32_t>(this->udp_.remoteIP());
    source.port = this->udp_.remotePort();
    this->dispatch_(source, buffer, static_cast<size_t>(received));
#else
    sockaddr_in address{};
    socklen_t address_length = sizeof(address);
    const ssize_t received =
        ::recvfrom(this->socket_fd_, buffer, sizeof(buffer), 0,
                   reinterpret_cast<sockaddr *>(&address), &address_length);
    if (received < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGV(TAG, "Standalone UDP receive failed: errno=%d", errno);
      }
      return;
    }
    if (received == 0) {
      return;
    }
    if (received > static_cast<ssize_t>(SYNCHROCAST_TRANSPORT_MTU)) {
      continue;
    }
    SynchrocastTransportSource source;
    source.transport = SynchrocastTransportKind::UDP;
    source.identity_valid = true;
    source.ipv4 = address.sin_addr.s_addr;
    source.port = ntohs(address.sin_port);
    this->dispatch_(source, buffer, static_cast<size_t>(received));
#endif
  }
}

bool SynchrocastStandaloneTransport::send_udp_(
    uint32_t address, uint16_t port, const uint8_t *data, size_t size) {
  if (!this->ready_ || address == 0 || port == 0 || data == nullptr ||
      size == 0 || size > SYNCHROCAST_TRANSPORT_MTU) {
    return false;
  }
#if defined(USE_ESP8266)
  const IPAddress destination(address);
  if (!this->udp_.beginPacket(destination, port)) {
    return false;
  }
  const size_t written = this->udp_.write(data, size);
  return written == size && this->udp_.endPacket() == 1;
#else
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(port);
  destination.sin_addr.s_addr = address;
  const ssize_t sent =
      ::sendto(this->socket_fd_, data, size, 0,
               reinterpret_cast<const sockaddr *>(&destination),
               sizeof(destination));
  return sent == static_cast<ssize_t>(size);
#endif
}

bool SynchrocastStandaloneTransport::send_udp_broadcast_(
    const uint8_t *data, size_t size) {
  bool sent = false;
#if defined(USE_ESP8266)
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress local = WiFi.localIP();
    const IPAddress mask = WiFi.subnetMask();
    const uint32_t subnet =
        static_cast<uint32_t>(local) | ~static_cast<uint32_t>(mask);
    if (subnet != 0 && subnet != 0xFFFFFFFFUL) {
      sent = this->send_udp_(subnet, this->udp_port_, data, size);
    }
  }
  const IPAddress global(255, 255, 255, 255);
  if (this->udp_.beginPacket(global, this->udp_port_)) {
    const size_t written = this->udp_.write(data, size);
    sent = (written == size && this->udp_.endPacket() == 1) || sent;
  }
#else
  for (netif *interface = netif_list; interface != nullptr;
       interface = interface->next) {
    if (!netif_is_up(interface)) {
      continue;
    }
    const ip4_addr_t *ip = netif_ip4_addr(interface);
    const ip4_addr_t *mask = netif_ip4_netmask(interface);
    if (ip == nullptr || mask == nullptr || ip4_addr_isany_val(*ip)) {
      continue;
    }
    const uint32_t subnet = ip->addr | ~mask->addr;
    if (subnet != 0 && subnet != INADDR_BROADCAST) {
      sent = this->send_udp_(subnet, this->udp_port_, data, size) || sent;
    }
  }
  sent = this->send_udp_(INADDR_BROADCAST, this->udp_port_, data, size) || sent;
#endif
  return sent;
}

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_STANDALONE_TRANSPORT
