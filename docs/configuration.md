# Configuration Reference

This page explains the `synchrocast:` YAML block. Begin with
[Getting Started](getting_started.md) if you have not yet tested one leader and
one follower.

> The current schema registers Cover, Fan, and Valve entities and configures
> transport ownership. Other domains and control mappings shown in the roadmap
> are not accepted yet. Live network synchronization still awaits the
> authenticated Synchrocast wire codec.

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
- `key` reserves the private passphrase for the authenticated wire codec. It is
  validated now, but no live authentication occurs until that codec is added.
  Every future group member will need the same value.
- `id` gives the block a readable ESPHome ID and is strongly recommended.

## Adding Local Entities

The current implementation accepts Cover, Fan, and Valve IDs:

```yaml
synchrocast:
  id: room_sync
  role: leader
  group: living_room
  key: !secret synchrocast_key

  covers:
    - room_blind
  fans:
    - room_fan
  valves:
    - irrigation_valve
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

### Controller (control mappings are planned)

A controller will send local input to a remote target without owning a local
synchronized target entity. The role is accepted now, but the `controls`
option is not implemented yet. This is the complete configuration currently
accepted for that role:

```yaml
synchrocast:
  id: bedroom_controller
  role: controller
  group: bedroom
  key: !secret synchrocast_key
```

It prepares the topology only; it does not send button commands in the current
skeleton.

### Satellite (local control input is planned)

A satellite can register a local supported entity now. Sending local button
input is planned for the later control-mapping milestone:

```yaml
synchrocast:
  id: bedroom_satellite
  role: satellite
  group: bedroom
  key: !secret synchrocast_key

  fans:
    - bedroom_fan
```

Use `satellite` instead of `controller` whenever the device has a local entity
that belongs to the synchronization group.

## Control Mappings (Planned)

A control mapping will connect one local input to one remote target. The
planned vocabulary includes `open`, `close`, `stop`, and `toggle`, but the
`controls:` option is deliberately rejected by the current schema. This keeps
ESPHome from accepting a configuration that cannot work yet.

When this milestone is implemented, the guide will include tested copy-paste
examples and the target will match the ESPHome ID registered on the leader.

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

Each block owns its role, group, key, and entity lists. At most eight
`synchrocast:` blocks are allowed on one device so attached consumer
registration remains bounded.

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

The current skeleton records this request for transport arbitration. If
`cfx_sync` is configured on the same device, `auto` accepts whichever CFX
transport is active. The standalone ESP-NOW/UDP backend is not implemented yet,
so a Synchrocast-only device does not exchange network packets at this stage.

Advanced users can request `espnow` or `udp` explicitly to verify that an
attached CFX owner provides that transport. Most users should keep `auto`.

## Using ChimeraFX and Synchrocast Together

Add both repositories normally and configure both `cfx_sync:` and
`synchrocast:`. Repository order does not matter.

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
  - source: github://effelle/ChimeraFX@stage
    refresh: always

# ChimeraFX lights and Magic Buttons use this specialized profile.
cfx_sync:
  role: follower
  group: living_room_lights
  key: !secret cfx_sync_key
  lights:
    - room_light

# The fan uses the general Synchrocast domain handler.
synchrocast:
  id: room_fan_sync
  role: follower
  group: living_room_fan
  key: !secret synchrocast_key
  fans:
    - room_fan
```

When `cfx_sync:` is configured, it remains the sole ESP-NOW/UDP owner.
Synchrocast attaches automatically and does not start another socket, change the
radio channel, restart ESP-NOW, or manage a competing peer table.

- Keep `transport: auto` unless you are diagnosing a problem.
- An explicit Synchrocast transport must already be active in `cfx_sync`.
- Attached UDP inherits CFX port `39580`.
- Do not configure another attached UDP port.
- Merely adding the ChimeraFX repository does not activate sharing;
  `cfx_sync:` must be present in the device configuration.

The full beginner-oriented walkthrough is in
[Using Synchrocast with ChimeraFX](chimerafx.md).

## All Options

| Option | Required | Default | Meaning |
| --- | --- | --- | --- |
| `id` | Recommended | Generated | Readable ID for this Synchrocast block. |
| `role` | Yes | - | `leader`, `follower`, `controller`, or `satellite`. |
| `group` | Yes | - | Devices with the same group communicate together. |
| `key` | Yes | - | Passphrase reserved for the upcoming authenticated codec. Validated now; not used for live packets yet. Minimum eight characters. |
| `covers` | No | Empty | Local Cover IDs. Implemented. |
| `fans` | No | Empty | Local Fan IDs. Implemented. |
| `valves` | No | Empty | Local Valve IDs. Implemented. |
| `heartbeat` | No | `30s` | Reserved leader refresh interval. Stored and reported now; no heartbeat packet is emitted yet. |
| `transport` | No | `auto` | `auto`, `espnow`, or `udp`. |
| `udp_port` | No | Attached `39580` | Optional validation of the UDP port inherited from CFX. Rejected for standalone mode until that backend exists. |

Light, Climate, Lock, Media Player, Switch, Sensor, Binary Sensor, and control
mapping options remain planned and are currently rejected instead of being
silently ignored.
