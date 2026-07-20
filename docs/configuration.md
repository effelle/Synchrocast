# Configuration Reference

This page describes the YAML accepted by the current `stage` branch. Begin with
[Getting Started](getting_started.md) or the
[four-device example](example_sensor_cover.md) if this is your first setup.

## Basic Block

```yaml
synchrocast:
  role: follower
  group: living_room
  key: !secret synchrocast_key
```

- `role` controls the device's responsibility.
- `group` isolates one synchronized set of devices.
- `key` authenticates that group and must match on every member.
- `transport` normally stays omitted.

## Roles

| Role | Purpose |
| --- | --- |
| `leader` | Authoritative source of shared state. |
| `follower` | Read-only mirror of selected state. |
| `controller` | Reserved for command-oriented devices. |
| `satellite` | Mirrors actuator state and may read selected sensors. |

Sensor domains are immutable: only a leader maps share keys to local sources.
Followers, controllers, and satellites may only list the share keys they want
to read.

## Numeric Sensors

Leader syntax maps a custom share key to an existing local ESPHome sensor:

```yaml
synchrocast:
  role: leader
  group: power_grid
  key: !secret synchrocast_key
  sensors:
    meter_phase_2: phase_2_voltage
```

Receiver syntax lists the wanted share keys:

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key
  sensors: meter_phase_2
```

For several values:

```yaml
sensors:
  - meter_phase_1
  - meter_phase_2
  - meter_phase_3
```

Synchrocast creates a read-only local sensor for each listed share key. The
share key is also its local ESPHome ID, so `id(meter_phase_2)` works in device
automations. Its visible name is generated from the key, for example
`meter_phase_2` becomes `Meter Phase 2`.

## Binary Sensors

Leader:

```yaml
binary_sensors:
  pump_running: local_pump_running
```

Follower or satellite:

```yaml
binary_sensors: pump_running
```

## Text Sensors

Leader:

```yaml
text_sensors:
  inverter_status: local_inverter_status
```

Follower or satellite:

```yaml
text_sensors: inverter_status
```

Text is limited to 64 bytes of valid UTF-8. Invalid or oversized values are
reported as unavailable and are never silently truncated.

## Share Keys

A share key is an opaque identifier chosen by the user. It is not a domain,
unit, device class, or predefined sensor category.

Valid keys:

- contain 1-64 characters;
- begin with a lowercase letter or underscore;
- use lowercase letters, numbers, or underscores;
- are unique across observational values read by the same device, because a
  share key also becomes that value's local ESPHome ID.

The component converts the key to a compact hash for authenticated packets.
Hash collisions inside one configuration are rejected during validation.

## Sensor Timing

There are no Synchrocast timing, delta, publish, receive, or stale options.

The local leader sensor owns its ESPHome update interval and filters.
Synchrocast observes its final state, broadcasts changes, and performs bounded
internal refresh and availability recovery without adding YAML settings.

## Cover, Fan, and Valve Entities

Actuator domains keep their established entity-list model:

```yaml
synchrocast:
  role: satellite
  group: garden
  key: !secret synchrocast_key
  covers: patio_awning
  fans: ventilation_fan
  valves: irrigation_valve
```

Each value is the ID of an entity declared elsewhere in the same ESPHome file.
Use the same entity ID on devices that represent the same shared actuator.

## Multiple Values

A leader may share up to 16 values per observational domain:

```yaml
synchrocast:
  role: leader
  group: utility
  key: !secret synchrocast_key
  sensors:
    utility_voltage: local_voltage
    utility_current: local_current
    utility_power: local_power
```

A reader may choose any subset:

```yaml
synchrocast:
  role: follower
  group: utility
  key: !secret synchrocast_key
  sensors:
    - utility_voltage
    - utility_power
```

Only one Synchrocast block may use a given group on one device. Put all domains
for that group in the same block.

## Transport Options

```yaml
synchrocast:
  role: follower
  group: living_room
  key: !secret synchrocast_key
  transport: auto
```

| Value | Standalone behavior |
| --- | --- |
| `auto` | ESP-NOW on ESP32; UDP on ESP8266. |
| `espnow` | Explicit ESP-NOW; ESP32 only. |
| `udp` | Explicit UDP. Use this on every member of a mixed ESP32/ESP8266 group. |

Standalone Synchrocast UDP uses port `39581` by default. `udp_port` is accepted
only when UDP is active.

When the same device also configures `cfx_sync:`, Synchrocast attaches to its
already-running transport. Synchrocast still owns its protocol, group, key,
authentication, and domain filtering. ChimeraFX remains optional.

## Base Options

| Option | Required | Default | Meaning |
| --- | --- | --- | --- |
| `role` | Yes | - | `leader`, `follower`, `controller`, or `satellite`. |
| `group` | Yes | - | Authenticated synchronization group. |
| `key` | Yes | - | Shared secret containing 8-64 UTF-8 bytes. |
| `transport` | No | `auto` | `auto`, `espnow`, or `udp`. |
| `udp_port` | No | `39581` standalone | UDP port when Synchrocast owns UDP. |
| `heartbeat` | No | `30s` | Internal group liveness heartbeat. |
| `sensors` | No | Empty | Leader mapping or reader interest list. |
| `binary_sensors` | No | Empty | Leader mapping or reader interest list. |
| `text_sensors` | No | Empty | Leader mapping or reader interest list. |
| `covers` | No | Empty | Existing local Cover IDs. |
| `fans` | No | Empty | Existing local Fan IDs. |
| `valves` | No | Empty | Existing local Valve IDs. |
