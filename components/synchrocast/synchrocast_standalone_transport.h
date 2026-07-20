// Copyright (c) 2026 Federico Leoni
// SPDX-License-Identifier: MIT

#pragma once

#include "esphome/core/defines.h"

#ifdef USE_SYNCHROCAST_STANDALONE_TRANSPORT

#include "synchrocast_transport.h"

#ifdef USE_ESPNOW
#include "esphome/components/espnow/espnow_component.h"
#include "esphome/core/version.h"
#include <esp_err.h>
#endif

#if defined(USE_ESP8266)
#include <WiFiUdp.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace synchrocast {

static constexpr uint16_t SYNCHROCAST_DEFAULT_UDP_PORT = 39581;

#ifdef USE_ESPNOW
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 7, 0)
using SynchrocastESPNowPacketSize = uint16_t;
#else
using SynchrocastESPNowPacketSize = uint8_t;
#endif
#endif

class SynchrocastStandaloneTransport final
    : public SynchrocastTransportBackend
#ifdef USE_ESPNOW
    , public espnow::ESPNowReceivedPacketHandler,
      public espnow::ESPNowUnknownPeerHandler,
      public espnow::ESPNowBroadcastHandler
#endif
{
 public:
  static constexpr size_t MAX_SINKS = 8;
  static constexpr uint8_t MAX_UDP_PACKETS_PER_LOOP = 4;

  ~SynchrocastStandaloneTransport() override;

#ifdef USE_ESPNOW
  void set_espnow(espnow::ESPNowComponent *espnow) {
    this->espnow_ = espnow;
  }
#endif

  bool configure(SynchrocastRequestedTransport requested, uint16_t udp_port);
  bool attach(SynchrocastTransportPacketSink *sink) override;
  void detach(SynchrocastTransportPacketSink *sink) override;
  bool send_broadcast(SynchrocastTransportKind transport,
                      const uint8_t *data, size_t size) override;
  bool send_to(const SynchrocastTransportSource &destination,
               const uint8_t *data, size_t size) override;
  SynchrocastTransportBackendStatus status() const override;
  void loop() override;

#ifdef USE_ESPNOW
  bool on_receive(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                  SynchrocastESPNowPacketSize size) override;
  bool on_unknown_peer(const espnow::ESPNowRecvInfo &info,
                       const uint8_t *data,
                       SynchrocastESPNowPacketSize size) override;
  bool on_broadcast(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                    SynchrocastESPNowPacketSize size) override;
#endif

 protected:
  bool dispatch_(const SynchrocastTransportSource &source,
                 const uint8_t *data, size_t size);
  SynchrocastTransportKind resolve_kind_(
      SynchrocastRequestedTransport requested) const;

#ifdef USE_ESPNOW
  bool begin_espnow_();
  bool send_espnow_(const uint8_t *mac, const uint8_t *data, size_t size);
#endif
  bool begin_udp_(uint16_t port);
  void close_udp_();
  void poll_udp_();
  bool send_udp_(uint32_t address, uint16_t port, const uint8_t *data,
                 size_t size);
  bool send_udp_broadcast_(const uint8_t *data, size_t size);

  std::array<SynchrocastTransportPacketSink *, MAX_SINKS> sinks_{};
  uint8_t sink_count_{0};
  SynchrocastTransportKind kind_{SynchrocastTransportKind::NONE};
  uint16_t udp_port_{0};
  uint32_t last_udp_poll_ms_{static_cast<uint32_t>(-1)};
  bool configured_{false};
  bool ready_{false};

#ifdef USE_ESPNOW
  espnow::ESPNowComponent *espnow_{nullptr};
  bool espnow_registered_{false};
#endif

#if defined(USE_ESP8266)
  WiFiUDP udp_;
#else
  int socket_fd_{-1};
#endif
};

SynchrocastStandaloneTransport &global_synchrocast_standalone_transport();

}  // namespace synchrocast
}  // namespace esphome

#endif  // USE_SYNCHROCAST_STANDALONE_TRANSPORT
