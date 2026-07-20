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
4. Enable the focused logs in
   [Troubleshooting and Verbose Logs](troubleshooting.md) when testing devices.
5. If ChimeraFX is on the same device, follow
   [Using Synchrocast with ChimeraFX](chimerafx.md).

Developers and contributors can read
[Architecture and Resource Budget](development.md) for the fixed-memory design
and simulation boundary.

## A Simple Mental Model

Think of a Synchrocast group as one room or one system. In the finished wire
protocol, every device in the group will use the same group name and private
key.

| Role | What it does |
| --- | --- |
| `leader` | Holds the main state for the group. |
| `follower` | Copies the leader. |
| `controller` | Will send user actions but has no local synchronized entity. |
| `satellite` | Will follow the leader and report local state or controls. |

For configuration and simulation work, begin with one leader and one follower.
Controller mappings and live cross-device exchange are not implemented yet.

## Project Status

Synchrocast is still under development. Cover, Fan, and Valve application
handlers, the fixed-memory dispatcher, their initial YAML registration, and
automatic `cfx_sync` transport arbitration are implemented. The authenticated
wire codec, standalone transport backend, and remaining domains are still in
development.

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
