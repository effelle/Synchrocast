# Synchrocast

Synchrocast is an ultra-low-latency state-synchronization framework for ESPHome
devices. It lets entities on different nodes share state and user commands
through one small, domain-neutral vocabulary.

The project is designed to be a good ESPHome citizen: fixed memory on the packet
path, bounded work in every loop, no entity calls from network tasks, quiet
normal logging, and detailed diagnostics only when verbose logging is enabled.

> **Development status:** Synchrocast is still a `stage` project. Its
> authenticated application protocol, fixed-memory dispatcher, Cover/Fan/Valve
> receivers, native Sensor/Binary Sensor/Text Sensor publisher and receiver
> entities, and automatic `cfx_sync` transport arbitration are implemented.
> Live traffic currently requires a configured ChimeraFX `cfx_sync` owner; the
> standalone Synchrocast ESP-NOW/UDP backend and the remaining actuator domains
> are still in development.

The current implementation is compatibility-checked against ESPHome `2026.7.0`.

## Supported Scope

Synchrocast is being built for:

- Light
- Cover
- Fan
- Climate
- Lock
- Media Player
- Valve
- Switch
- Sensor
- Binary Sensor
- Text Sensor

The topology uses four roles:

| Role | Purpose |
| --- | --- |
| `leader` | Owns and publishes the main state for a synchronization group. |
| `follower` | Receives state and exposes it as local ESPHome entities. |
| `controller` | Reserved for command-only devices; control mappings are still planned. |
| `satellite` | Publishes local state while also receiving state; local command mappings are still planned. |

## Repository Installation

The project currently targets the `stage` branch. Add the repository with:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
```

The complete setup, role selection, and copy-paste examples are in the
[Getting Started guide](docs/getting_started.md).

If the same device also configures ChimeraFX `cfx_sync`, add both repositories.
Synchrocast detects the active CFX transport automatically; no shared transport
ID is required:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
  - source: github://effelle/ChimeraFX@stage
    refresh: always
```

See [Using Synchrocast with ChimeraFX](docs/chimerafx.md) for a complete
two-component example and an explanation of which component owns the network.

## Smallest Sensor Mapping

On the device that owns an existing ESPHome sensor:

```yaml
synchrocast:
  role: leader
  group: power_grid
  key: !secret synchrocast_key
  sensors:
    publish:
      - source: phase_2_voltage
        sync_id: grid.phase_2.voltage
```

On the receiving device:

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key
  sensors:
    receive:
      - sync_id: grid.phase_2.voltage
        id: remote_phase_2_voltage
        name: "Phase 2 Voltage"
        unit_of_measurement: V
        device_class: voltage
        state_class: measurement
```

Only `group`, key, and `sync_id` must match. Synchrocast creates the receiving
sensor; no template is required. Live traffic on the current `stage` branch
also requires a valid `cfx_sync:` transport on each device. The
[sensor guide](docs/sensors.md) explains filters, update limits, availability,
binary values, text values, and energy totals.

## Documentation

- [Documentation home](docs/index.md)
- [Getting started](docs/getting_started.md)
- [Configuration reference](docs/configuration.md)
- [Domains and capabilities](docs/domains.md)
- [Synchronizing numeric, binary, and text sensors](docs/sensors.md)
- [Troubleshooting and verbose logs](docs/troubleshooting.md)
- [Architecture and resource budget](docs/development.md)

## Good Citizen by Design

The current application layer follows these rules:

- Authenticated frames are explicitly serialized, bounded to 108 bytes, and
  protected by a truncated HMAC-SHA256 tag.
- A decoded packet is copied into a fixed 16-slot ring buffer. A build without
  Text Sensor uses a 32-byte packet (512-byte queue); enabling Text Sensor uses
  an 80-byte packet (1,280-byte queue) for its bounded 64-byte UTF-8 value.
- At most four packets are dispatched per ESPHome loop iteration.
- Repeated pending state for the same entity is coalesced without crossing a
  newer intent for that entity.
- Domain handlers and entity registries use fixed storage and perform no
  packet-path heap allocation.
- Sensor publishers retain only the latest coalesced state and use explicit
  minimum and refresh intervals.
- Duplicate packets and cross-transport copies are suppressed by authenticated
  boot/session sequence numbers.
- A configured `cfx_sync` instance remains the only ESP-NOW/UDP owner;
  Synchrocast attaches as a bounded raw-packet consumer.
- Attached UDP sends can use fixed byte buffers without conversion to a dynamic
  packet container.
- ESPHome entities are accessed only from the main loop.
- Normal packet rejections are verbose diagnostics, not warning-level log spam.
- Queue-overflow warnings are rate-limited.

## Development Logs

Verbose logs are intentionally descriptive so upcoming device simulations can
show the complete application-layer path:

```yaml
logger:
  level: VERBOSE
  logs:
    synchrocast: VERBOSE
    synchrocast.dispatcher: VERBOSE
    synchrocast.cover: VERBOSE
    synchrocast.fan: VERBOSE
    synchrocast.valve: VERBOSE
    synchrocast.sensor: VERBOSE
    synchrocast.binary_sensor: VERBOSE
    synchrocast.text_sensor: VERBOSE
```

See [Troubleshooting and verbose logs](docs/troubleshooting.md) for example log
lines and what each field means.

## License

Synchrocast is released under the [MIT License](LICENSE).
