# Synchrocast Documentation

Synchrocast is being built to keep ESPHome entities on different devices
synchronized with very little delay. The target design lets a main device
publish state, another device follow it, and a wall controller send commands
without needing the target hardware on the same board.

## Start Here

1. Read [Getting Started](getting_started.md) to add the repository and create
   your first leader and follower.
2. Use the [Configuration Reference](configuration.md) when adding more
   entities, roles, or groups.
3. Check [Domains and Capabilities](domains.md) to see what each ESPHome domain
   can synchronize.
4. Follow [Synchronizing Sensor Values](sensors.md) for complete numeric,
   binary, and text examples that create usable entities on the reader.
5. Enable the focused logs in
   [Troubleshooting and Verbose Logs](troubleshooting.md) when testing devices.
6. If ChimeraFX is on the same device, follow
   [Using Synchrocast with ChimeraFX](chimerafx.md).

Developers and contributors can read
[Architecture and Resource Budget](development.md) for the fixed-memory design
and simulation boundary.

## A Simple Mental Model

Think of a Synchrocast group as one room or one system. Every device in the
group uses the same group name and private key.

| Role | What it does |
| --- | --- |
| `leader` | Holds and publishes the main state for the group. |
| `follower` | Receives state as ordinary local ESPHome entities. |
| `controller` | Reserved for command-only devices; mappings are still planned. |
| `satellite` | Mirrors configured actuator state and may read selected sensors; command mappings are still planned. |

Begin with one leader and one follower. Synchrocast supplies its own live
transport: ESP-NOW by default on ESP32 and UDP by default on ESP8266.
ChimeraFX is optional.

## Project Status

Synchrocast is still under development. The authenticated wire codec,
fixed-memory dispatcher, canonical Cover/Fan/Valve state handlers,
Sensor/Binary Sensor/Text Sensor leader sources and read-only entities, and
automatic `cfx_sync` transport arbitration are implemented together with the
standalone transport and channel-recovery backend.
The remaining actuator domains are still in development.

The planned public domain scope is:

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
