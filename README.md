# Synchrocast

Synchrocast is an ultra-low-latency state-synchronization framework for ESPHome
devices. It lets entities on different nodes share state and user commands
through one small, domain-neutral vocabulary.

The project is designed to be a good ESPHome citizen: fixed memory on the packet
path, bounded work in every loop, no entity calls from network tasks, quiet
normal logging, and detailed diagnostics only when verbose logging is enabled.

> **Development status:** Synchrocast is still a `stage` project. Its
> authenticated application protocol, fixed-memory dispatcher, Cover/Fan/Valve
> canonical state handlers, native Sensor/Binary Sensor/Text Sensor leader sources and read-only
> entities, standalone ESP-NOW/UDP transport, and optional `cfx_sync` transport
> arbitration are implemented. The remaining actuator domains are still in
> development.

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
| `satellite` | Mirrors configured actuator state and may read selected sensors; local command mappings are still planned. |

## Repository Installation

The project currently targets the `stage` branch. Add the repository with:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
```

The complete setup, role selection, and copy-paste examples are in the
[Getting Started guide](docs/getting_started.md).

ChimeraFX is not required. If the same device also uses ChimeraFX lights or
Magic Buttons, add both repositories. Synchrocast detects the active CFX
transport automatically so the components do not compete for radio/socket
resources:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
  - source: github://effelle/ChimeraFX@stage
    refresh: always
```

See [Using Synchrocast with ChimeraFX](docs/chimerafx.md) for a complete
two-component example and an explanation of which component owns the network.

## Smallest Sensor Share

On the device that owns an existing ESPHome sensor:

```yaml
synchrocast:
  role: leader
  group: power_grid
  key: !secret synchrocast_key
  sensors:
    meter_phase_2: phase_2_voltage
```

On the receiving device:

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key
  sensors: meter_phase_2
```

`meter_phase_2` is a custom share key, not a sensor type or device class. The
leader maps it to its local `phase_2_voltage` sensor. Each follower that lists
the share key gets a normal read-only ESPHome sensor; devices that omit it drop
that broadcast before it reaches the packet queue. No template sensor is
required. The share key is also its local ESPHome ID, so device automations can
read `id(meter_phase_2).state` directly. See the
[sensor guide](docs/sensors.md) for the complete model.

## Documentation

- [Documentation home](docs/index.md)
- [Getting started](docs/getting_started.md)
- [Sensor and cover four-device example](docs/example_sensor_cover.md)
- [Configuration reference](docs/configuration.md)
- [Domains and capabilities](docs/domains.md)
- [Synchronizing numeric, binary, and text sensors](docs/sensors.md)
- [Troubleshooting and verbose logs](docs/troubleshooting.md)
- [Architecture and resource budget](docs/development.md)

## Good Citizen by Design

The current application layer follows these rules:

- Authenticated frames are explicitly serialized, bounded to 140 bytes, and
  protected by a truncated HMAC-SHA256 tag.
- A decoded packet is copied into a fixed 16-slot ring buffer. A sensor-only
  build uses a 32-byte packet (512-byte queue); Cover/Valve use 48-byte packets,
  Text Sensor uses 80-byte packets, and Fan uses 112-byte packets for bounded
  optional preset state.
- At most four packets are dispatched per ESPHome loop iteration.
- Repeated pending state for the same entity is coalesced without crossing a
  newer intent for that entity.
- Domain handlers and entity registries use fixed storage and perform no
  packet-path heap allocation.
- Sensor sources retain only the latest coalesced state and use a fixed internal
  recovery refresh; there are no user-facing Synchrocast timing controls.
- Duplicate packets and cross-transport copies are suppressed by authenticated
  boot/session sequence numbers.
- Synchrocast owns ESP-NOW/UDP normally. If `cfx_sync` is configured,
  Synchrocast attaches as a bounded raw-packet consumer instead of starting a
  second transport.
- Standalone ESP-NOW detects router/channel loss, moves to internal fallback
  channel 6 after a grace period, rearms after channel changes, and republishes
  current semantic state with bounded jitter after recovery.
- When ChimeraFX owns the physical transport, Synchrocast observes its recovery
  and performs the same semantic state refresh without competing for the radio.
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
    synchrocast.transport: VERBOSE
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
