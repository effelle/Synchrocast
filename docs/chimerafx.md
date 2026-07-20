# Using Synchrocast with ChimeraFX

This guide is for a device that needs both components. For example, ChimeraFX
can synchronize a light and provide Magic Button behavior while Synchrocast
receives a power sensor or prepares a Fan, Cover, or Valve receiver.

The components do not merge their configurations or packet formats. They share
only the already-running ESP-NOW/UDP transport:

- `cfx_sync` remains responsible for ChimeraFX lights and controls.
- Synchrocast remains responsible for its own groups, roles, entities, packet
  authentication, replay protection, and domain handling.
- CFX owns the radio/socket once, and Synchrocast uses that owner rather than
  starting a competing network stack.

This is the current live transport path on `stage`. A standalone Synchrocast
ESP-NOW/UDP backend is still pending.

## 1. Add Both Repositories

Put both sources in one `external_components:` list. Their order does not
matter:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@stage
    refresh: always

  - source: github://effelle/Synchrocast@stage
    refresh: always
```

Adding a repository makes its components available. It does not enable either
one until its YAML block is present.

## 2. Give Each Component Its Own Job

The following example assumes `room_light`, `room_fan`, and `local_power` are
already declared elsewhere in the same ESPHome file:

```yaml
# ChimeraFX owns specialized light synchronization.
cfx_sync:
  id: room_light_sync
  role: follower
  lights:
    - room_light
  group: living_room_lights
  key: !secret cfx_sync_key
  transport: auto

# Synchrocast publishes a measured value and registers a fan receiver.
synchrocast:
  id: room_general_sync
  role: satellite
  group: living_room_general
  key: !secret synchrocast_key
  transport: auto

  sensors:
    publish:
      - source: local_power
        sync_id: living_room.power
        min_interval: 1s
        refresh_interval: 30s
        delta: 1

  fans:
    - room_fan
```

No bridge ID or extra transport block is needed. Synchrocast detects the
configured `cfx_sync:` integration during ESPHome validation and attaches to
its bus.

The group names and keys above are intentionally different. They protect two
different protocols:

- every member of `living_room_lights` uses the same CFX group and CFX key;
- every member of `living_room_general` uses the same Synchrocast group and
  Synchrocast key.

The keys do not need to match. Separate private keys make the boundary easier
to understand and maintain.

> The Sensor, Binary Sensor, and Text Sensor publish/receive paths are live.
> Cover, Fan, and Valve currently have receive handlers, but their automatic
> outbound state observers are not implemented yet.

## 3. What Happens to a Packet

| Resource or responsibility | Owner |
| --- | --- |
| Start and manage ESP-NOW | `cfx_sync` |
| Open and poll the UDP socket | `cfx_sync` |
| Recognize, authenticate, and decode `CFXS` frames | `cfx_sync` |
| Recognize, authenticate, and decode `SCST` frames | Synchrocast |
| Reject duplicate Synchrocast frames | Synchrocast |
| Route a Synchrocast value to its local ESPHome entity | Synchrocast |

On receive, CFX first checks its own packet signature. A valid CFX packet, or a
damaged packet that clearly claims to be CFX, stays inside ChimeraFX. A frame
that is not CFX is offered to registered shared-transport consumers.
Synchrocast claims an `SCST` frame for one of its configured groups, verifies
the authentication tag and sequence, then queues the decoded value for the
ESPHome main loop.

On send, Synchrocast builds and authenticates its own `SCST` frame, then asks
the CFX-owned bus to broadcast those raw bytes. The frame never becomes a CFX
message. This separation is why both components can share one protocol carrier
without confusing their application behavior.

## 4. Transport Rules

For normal use, leave both components on `transport: auto`.

- On ESP32, CFX normally provides ESP-NOW. A CFX leader can also provide UDP.
- On ESP8266, CFX uses UDP.
- If Synchrocast explicitly requests `espnow` or `udp`, that transport must
  already be active in CFX.
- Attached UDP inherits CFX port `39580`.
- Synchrocast does not start or stop the shared radio, open a second socket,
  change the Wi-Fi channel, or maintain a competing peer table.
- If CFX is configured but not ready, Synchrocast waits. It never silently
  falls back to another owner.

Merely downloading the ChimeraFX repository is not enough. A valid `cfx_sync:`
block must be configured because that running component is the transport owner.

## 5. Focused Logs

Use verbose logs temporarily while setting up the pair:

```yaml
logger:
  level: VERBOSE
  logs:
    cfx_sync.bus: VERBOSE
    synchrocast: VERBOSE
    synchrocast.dispatcher: VERBOSE
    synchrocast.sensor: VERBOSE
    synchrocast.binary_sensor: VERBOSE
    synchrocast.text_sensor: VERBOSE
```

Useful startup states are:

- `attached to cfx_sync`: the selected CFX transport is active and shared.
- `waiting for cfx_sync`: CFX was detected but its transport is not ready.
- `blocked`: the explicit transport or UDP-port request conflicts with CFX.

During traffic, `Broadcast ...` confirms a Synchrocast publisher handed off a
frame. `Published remote ...` confirms a receiver authenticated and applied it.
A periodic Synchrocast stats line separates authentication, malformed-frame,
replay, role, queue, and send failures.

`Shared frame left unclaimed` no longer means that the Synchrocast codec is
missing. It means the raw frame did not belong to any registered consumer. An
occasional message can be another protocol on the same transport; repeated
messages should be investigated with the sender and authentication logs.

Remove verbose logging after testing. Normal operation is designed to stay
quiet and network-driven warnings are rate-limited.
