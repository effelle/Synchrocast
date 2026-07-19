# Synchrocast Documentation

Synchrocast keeps ESPHome entities on different devices synchronized with very
little delay. A main device can publish state, another device can follow it, and
a wall controller can send commands without needing the target hardware on the
same board.

## Start Here

1. Read [Getting Started](getting_started.md) to add the repository and create
   your first leader and follower.
2. Use the [Configuration Reference](configuration.md) when adding more
   entities, roles, or groups.
3. Check [Domains and Capabilities](domains.md) to see what each ESPHome domain
   can synchronize.
4. Enable the focused logs in
   [Troubleshooting and Verbose Logs](troubleshooting.md) when testing devices.

Developers and contributors can read
[Architecture and Resource Budget](development.md) for the fixed-memory design
and simulation boundary.

## A Simple Mental Model

Think of a Synchrocast group as one room or one system. Every device in the group
uses the same group name and private key.

| Role | What it does |
| --- | --- |
| `leader` | Holds the main state for the group. |
| `follower` | Copies the leader. |
| `controller` | Sends user actions but has no local synchronized entity. |
| `satellite` | Follows the leader and can also report local state or controls. |

Most users should begin with one leader and one follower. Add controllers,
satellites, additional entities, or more groups only after that first pair works.

## Project Status

Synchrocast is still under development. Cover, Fan, and Valve application
handlers and the fixed-memory dispatcher are implemented. The YAML examples in
these guides define the intended public configuration for Step 3 and do not yet
validate in the current skeleton.

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
