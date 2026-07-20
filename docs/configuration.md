# Configuration Reference

This page lists the options accepted by the current `stage` branch. If this is
your first Synchrocast setup, begin with [Getting Started](getting_started.md).

## Basic Block

Every Synchrocast block needs a role, group, and key:

```yaml
synchrocast:
  id: living_room_sync
  role: follower
  group: living_room
  key: !secret synchrocast_key
```

- `role` describes what this device may send and receive.
- `group` separates one synchronized room or system from another.
- `key` authenticates every Synchrocast packet. All members of one group need
  the same key. Authentication detects forged or modified packets; it is not
  encryption.
- `id` gives the block a readable local ESPHome ID and is recommended.

The key must contain at least 8 characters and fit in 64 UTF-8 bytes; the group
must also fit in 64 UTF-8 bytes. Keep the key in `secrets.yaml` rather than
copying it into files you publish.

## Roles

| Role | Receives state | Publishes observed state | Sends commands |
| --- | --- | --- | --- |
| `leader` | Yes | Yes | No |
| `follower` | Yes | No | No |
| `controller` | Yes | No | Planned |
| `satellite` | Yes | Yes | Planned |

Use one authoritative publisher for a given group, domain, and `sync_id`.
Choose `satellite` for a device that both publishes its own sensors and receives
other values. The current `controls:` mapping is not implemented and is rejected
instead of being silently ignored.

## Numeric Sensors

`publish` points to an existing ESPHome sensor. `receive` creates a native
ESPHome sensor on this device:

```yaml
synchrocast:
  role: satellite
  group: utility_room
  key: !secret synchrocast_key

  sensors:
    publish:
      - source: local_temperature
        sync_id: utility.temperature
        min_interval: 1s
        refresh_interval: 30s
        delta: 0.1

    receive:
      - sync_id: utility.power
        id: remote_power
        name: "Remote Power"
        unit_of_measurement: W
        device_class: power
        state_class: measurement
        accuracy_decimals: 1
        stale_after: 2min
```

The receiving entry accepts the normal ESPHome Sensor options, including
filters and automations. Do not declare another template sensor for it.

## Binary Sensors

```yaml
synchrocast:
  role: follower
  group: utility_room
  key: !secret synchrocast_key

  binary_sensors:
    receive:
      - sync_id: pump.running
        id: remote_pump_running
        name: "Pump Running"
        device_class: running
        stale_after: 2min
```

A publisher uses the same shape as a numeric publisher, without `delta`:

```yaml
binary_sensors:
  publish:
    - source: local_pump_running
      sync_id: pump.running
      min_interval: 100ms
      refresh_interval: 30s
```

## Text Sensors

```yaml
synchrocast:
  role: follower
  group: inverter
  key: !secret synchrocast_key

  text_sensors:
    receive:
      - sync_id: inverter.status
        id: remote_inverter_status
        name: "Inverter Status"
        stale_after: 3min
```

Text publishers use `source`, `sync_id`, `min_interval`, and
`refresh_interval`. Text must be valid UTF-8 and no longer than 64 bytes.
Synchrocast never cuts an oversized message because silent truncation could
change its meaning; it sends unavailable instead.

See [Synchronizing Sensor Values](sensors.md) for complete publisher/receiver
examples and explanations of availability, energy totals, and automations.

## Publisher Options

| Option | Required | Default | Meaning |
| --- | --- | --- | --- |
| `source` | Yes | - | ID of the existing local Sensor, Binary Sensor, or Text Sensor. |
| `sync_id` | Yes | - | Stable network identity shared with the receiver. |
| `min_interval` | No | Numeric `250ms`; binary `50ms`; text `1s` | Fastest allowed send rate. |
| `refresh_interval` | No | `60s` | Periodic repeat of the latest absolute state. Must be longer than `min_interval`. |
| `delta` | Numeric only | `0` | Minimum numeric change for an immediate update. |

`sync_id` is 1-64 lowercase characters. It must start with a letter or number
and may then contain letters, numbers, dots, underscores, or hyphens. For
example, `grid.phase_2.voltage` is valid.

Synchrocast publishes the source's final ESPHome state after its filters. A
non-finite numeric state is treated as unavailable. `delta` controls immediate
traffic only; the periodic refresh still sends the newest value.

## Receiver Options

Every receiver needs `sync_id`. Add `id` when automations or lambdas will refer
to the entity, and add `name` when it should be visible through the configured
ESPHome API:

| Option | Required | Default | Meaning |
| --- | --- | --- | --- |
| `sync_id` | Yes | - | Network identity shared with the publisher. |
| `id` | Recommended | Generated | Local ESPHome entity ID. |
| `name` | No | Internal entity | Visible name on this receiving device. |
| `stale_after` | No | `3min` | No-refresh timeout before the entity becomes unavailable. |

The normal options for that ESPHome entity type also work. Configure units,
device class, state class, icons, filters, accuracy, and automations locally.
These presentation details are not copied over the network.

## Cover, Fan, and Valve Receivers

The current branch also registers bounded receive handlers for Cover, Fan, and
Valve entities:

```yaml
synchrocast:
  role: follower
  group: garden
  key: !secret synchrocast_key
  covers:
    - patio_awning
  fans:
    - ventilation_fan
  valves:
    - irrigation_valve
```

Each item is an entity already declared elsewhere in the same YAML. These
handlers can apply decoded absolute state and supported intents, but automatic
outbound observation for these actuator domains is not implemented yet. Do not
describe this list as a working leader-side publisher.

For these actuator handlers, matching currently derives from the local ESPHome
entity ID. Use the same ID on the devices that represent the same actuator.
This differs from observational values, which always use explicit `sync_id`.

## Multiple Values and Groups

One block can contain up to 16 publishers and 16 receivers in each
observational domain. Use lists rather than another block for the same group:

```yaml
synchrocast:
  role: satellite
  group: utility_room
  key: !secret synchrocast_key
  sensors:
    publish:
      - source: local_voltage
        sync_id: utility.voltage
      - source: local_power
        sync_id: utility.power
    receive:
      - sync_id: utility.temperature
        id: remote_temperature
        name: "Remote Temperature"
```

Use a YAML list of Synchrocast blocks only when the device joins separate
groups. At most eight blocks are allowed on one device:

```yaml
synchrocast:
  - id: garage_sync
    role: follower
    group: garage
    key: !secret garage_sync_key

  - id: garden_sync
    role: follower
    group: garden
    key: !secret garden_sync_key
```

The same group cannot be declared twice on one device. Keep all of that group's
domains in one block.

## Transport Options

Most users should keep the default:

```yaml
synchrocast:
  role: follower
  group: living_room
  key: !secret synchrocast_key
  transport: auto
```

The standalone Synchrocast backend is still pending. For live traffic on the
current `stage` branch, a valid `cfx_sync:` block must be configured on the same
device. CFX remains the only ESP-NOW/UDP owner, while Synchrocast authenticates
and decodes its own distinct `SCST` frames.

Advanced users may request `espnow` or `udp`, but that transport must already be
active in CFX. Attached UDP inherits CFX port `39580`; Synchrocast does not open
another socket or manage a competing radio peer table. See
[Using Synchrocast with ChimeraFX](chimerafx.md).

## Base Options

| Option | Required | Default | Meaning |
| --- | --- | --- | --- |
| `id` | Recommended | Generated | Local ID for this Synchrocast block. |
| `role` | Yes | - | `leader`, `follower`, `controller`, or `satellite`. |
| `group` | Yes | - | Communication group; up to 64 UTF-8 bytes. |
| `key` | Yes | - | Packet-authentication passphrase; at least 8 characters and at most 64 UTF-8 bytes. |
| `covers` | No | Empty | Existing local Cover receivers. |
| `fans` | No | Empty | Existing local Fan receivers. |
| `valves` | No | Empty | Existing local Valve receivers. |
| `sensors` | No | Empty | Numeric `publish` and `receive` lists. |
| `binary_sensors` | No | Empty | On/off `publish` and `receive` lists. |
| `text_sensors` | No | Empty | UTF-8 text `publish` and `receive` lists. |
| `heartbeat` | No | `30s` | Authenticated presence packet interval; minimum `10s`. |
| `transport` | No | `auto` | `auto`, `espnow`, or `udp`. |
| `udp_port` | No | Attached `39580` | Optional check of the CFX-owned UDP port. Rejected in standalone mode for now. |

Light, Climate, Lock, Media Player, Switch, and control mappings remain planned
and are currently rejected instead of being silently ignored.
