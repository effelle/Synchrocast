# Synchrocast

Synchrocast is an ultra-low-latency state-synchronization framework for ESPHome
devices. It lets entities on different nodes share state and user commands
through one small, domain-neutral vocabulary.

The project is designed to be a good ESPHome citizen: fixed memory on the packet
path, bounded work in every loop, no entity calls from network tasks, quiet
normal logging, and detailed diagnostics only when verbose logging is enabled.

> **Development status:** Synchrocast is an early skeleton and is not ready for
> normal installation yet. The fixed-memory dispatcher and Cover, Fan, and Valve
> handlers are implemented. The ESPHome YAML layer, transport integration, and
> remaining domains are still in development.

The current C++ skeleton is compatibility-checked against ESPHome `2026.7.0`.

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

The topology supports four roles:

| Role | Purpose |
| --- | --- |
| `leader` | Owns the main state for a synchronization group. |
| `follower` | Copies state from the leader. |
| `controller` | Sends user input without owning a synchronized entity. |
| `satellite` | Owns local synchronized entities and can send local changes back. |

## Repository Installation

The project currently targets the `stage` branch. When the YAML integration is
available, an ESPHome device will add the repository with:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
```

The complete setup, role selection, and copy-paste examples are in the
[Getting Started guide](docs/getting_started.md).

## Documentation

- [Documentation home](docs/index.md)
- [Getting started](docs/getting_started.md)
- [Configuration reference](docs/configuration.md)
- [Domains and capabilities](docs/domains.md)
- [Troubleshooting and verbose logs](docs/troubleshooting.md)
- [Architecture and resource budget](docs/development.md)

## Good Citizen by Design

The current application layer follows these rules:

- A decrypted packet is copied into a fixed 16-slot ring buffer.
- Each compact packet occupies 24 bytes; the queue payload storage is 384 bytes.
- At most four packets are dispatched per ESPHome loop iteration.
- Repeated pending state for the same entity is coalesced without crossing a
  newer intent for that entity.
- Domain handlers and entity registries use fixed storage and perform no
  packet-path heap allocation.
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
    synchrocast.dispatcher: VERBOSE
    synchrocast.cover: VERBOSE
    synchrocast.fan: VERBOSE
    synchrocast.valve: VERBOSE
```

See [Troubleshooting and verbose logs](docs/troubleshooting.md) for example log
lines and what each field means.

## License

Synchrocast is released under the [MIT License](LICENSE).
