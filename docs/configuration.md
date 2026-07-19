# Configuration Reference

This page explains the planned `synchrocast:` YAML block. Begin with
[Getting Started](getting_started.md) if you have not yet tested one leader and
one follower.

> The YAML integration is scheduled for Step 3 and is not present in the current
> skeleton. This page defines the user-facing configuration that implementation
> must follow.

## Basic Block

Every Synchrocast block needs a role, group, and key:

```yaml
synchrocast:
  id: living_room_sync
  role: leader
  group: living_room
  key: !secret synchrocast_key
```

- `role` describes what this device does in the group.
- `group` separates one synchronized room or system from another.
- `key` protects the group. Every member must use the same value.
- `id` gives the block a readable ESPHome ID and is strongly recommended.

## Adding Local Entities

List an entity under the option that matches its ESPHome domain:

```yaml
synchrocast:
  id: room_sync
  role: leader
  group: living_room
  key: !secret synchrocast_key

  lights:
    - room_light
  covers:
    - room_blind
  fans:
    - room_fan
  climates:
    - room_thermostat
  locks:
    - front_door_lock
  media_players:
    - room_speaker
  valves:
    - irrigation_valve
  switches:
    - circulation_pump
  sensors:
    - room_temperature
  binary_sensors:
    - window_open
```

Each item must be the ID of an entity already declared elsewhere in the same
ESPHome YAML file.

Use the same entity ID on devices that represent the same logical entity. For
example, two devices synchronizing a fan should both use `id: bedroom_fan`.
Visible Home Assistant names may be different.

## Roles

### Leader

The leader is the source of truth for a group:

```yaml
synchrocast:
  id: bedroom_sync
  role: leader
  group: bedroom
  key: !secret synchrocast_key
  fans:
    - bedroom_fan
```

Use one leader per group.

### Follower

A follower receives leader state:

```yaml
synchrocast:
  id: bedroom_sync
  role: follower
  group: bedroom
  key: !secret synchrocast_key
  fans:
    - bedroom_fan
```

A follower can list several local entities. Each one is matched by its ESPHome
ID and domain.

### Controller

A controller sends local input to a remote target and does not own a local
synchronized target entity:

```yaml
binary_sensor:
  - platform: gpio
    id: bedroom_fan_button
    name: "Bedroom Fan Button"
    pin:
      number: GPIO10
      mode:
        input: true
        pullup: true
      inverted: true

synchrocast:
  id: bedroom_controller
  role: controller
  group: bedroom
  key: !secret synchrocast_key

  controls:
    - input: bedroom_fan_button
      target: bedroom_fan
      intent: toggle
```

`target` is the remote entity ID. It must match the ID registered on the leader.

### Satellite

A satellite owns local synchronized entities and can also send local input:

```yaml
synchrocast:
  id: bedroom_satellite
  role: satellite
  group: bedroom
  key: !secret synchrocast_key

  fans:
    - bedroom_fan

  controls:
    - input: bedroom_fan_button
      target: bedroom_fan
      intent: toggle
```

Use `satellite` instead of `controller` whenever the device has a local entity
that belongs to the synchronization group.

## Control Mappings

A control mapping connects one local input to one remote target:

```yaml
controls:
  - input: open_button
    target: garage_door
    intent: open

  - input: close_button
    target: garage_door
    intent: close

  - input: stop_button
    target: garage_door
    intent: stop
```

Use an intent that is valid for the target domain. See
[Domains and Capabilities](domains.md) for the common mappings.

## Multiple Entities in One Domain

Use a YAML list when one device has several entities of the same type:

```yaml
synchrocast:
  id: garden_sync
  role: follower
  group: garden
  key: !secret synchrocast_key
  valves:
    - front_irrigation
    - back_irrigation
    - greenhouse_irrigation
```

The current fixed registry allows up to 16 local entities per domain on one
device. This limit prevents unbounded memory growth.

## Multiple Groups on One Device

Declare a list of blocks when one device joins independent groups:

```yaml
synchrocast:
  - id: garage_sync
    role: leader
    group: garage
    key: !secret synchrocast_key
    covers:
      - garage_door

  - id: garden_sync
    role: leader
    group: garden
    key: !secret synchrocast_key
    valves:
      - irrigation_valve
```

Each block owns its role, group, key, and entity lists.

## Transport Options

Most users should keep the default transport:

```yaml
synchrocast:
  id: room_sync
  role: follower
  group: living_room
  key: !secret synchrocast_key
  transport: auto
```

`auto` uses ESP-NOW on ESP32 and UDP fallback where needed. Advanced users can
request `espnow` or `udp` explicitly when diagnosing a transport problem.

## All Options

| Option | Required | Planned default | Meaning |
| --- | --- | --- | --- |
| `id` | Recommended | Generated | Readable ID for this Synchrocast block. |
| `role` | Yes | - | `leader`, `follower`, `controller`, or `satellite`. |
| `group` | Yes | - | Devices with the same group communicate together. |
| `key` | Yes | - | Shared private passphrase. Minimum eight characters. |
| `lights` | No | Empty | Local Light IDs. |
| `covers` | No | Empty | Local Cover IDs. |
| `fans` | No | Empty | Local Fan IDs. |
| `climates` | No | Empty | Local Climate IDs. |
| `locks` | No | Empty | Local Lock IDs. |
| `media_players` | No | Empty | Local Media Player IDs. |
| `valves` | No | Empty | Local Valve IDs. |
| `switches` | No | Empty | Local Switch IDs. |
| `sensors` | No | Empty | Local numeric Sensor IDs. |
| `binary_sensors` | No | Empty | Local Binary Sensor IDs. |
| `controls` | Controller or satellite | Empty | Local input, remote target, and intent mappings. |
| `heartbeat` | No | `30s` | Regular leader state refresh interval. |
| `transport` | No | `auto` | `auto`, `espnow`, or `udp`. |
| `udp_port` | No | `45678` | UDP fallback port. |
| `fallback_channel` | No | `6` | ESP-NOW offline fallback channel. |

The defaults in this table follow the inherited transport design and remain
subject to validation when the Step 3 schema is implemented.
