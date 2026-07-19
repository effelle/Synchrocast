# Troubleshooting and Verbose Logs

Synchrocast keeps normal logs quiet. During setup or simulation, enable verbose
logs to see how a packet moves through the application layer.

## Enable Focused Logs

Add the tags for the domains you are testing:

```yaml
logger:
  level: VERBOSE
  logs:
    synchrocast.dispatcher: VERBOSE
    synchrocast.cover: VERBOSE
    synchrocast.fan: VERBOSE
    synchrocast.valve: VERBOSE
```

Remove the verbose level after testing. Packet-by-packet logs are useful for
development but unnecessarily noisy for a finished device.

## Reading a Dispatch Log

A dispatcher line looks like this:

```text
[V][synchrocast.dispatcher]: Dispatch type=STATE_BROADCAST domain=COVER entity=0xA1B2C3D4 intent=SET_POSITION payload=4 queue=0
```

| Field | Meaning |
| --- | --- |
| `type` | `INTENT_REQUEST`, `STATE_BROADCAST`, or `HEARTBEAT`. |
| `domain` | ESPHome entity type selected for the packet. |
| `entity` | Compact identity derived from the matching ESPHome entity ID. |
| `intent` | Requested action or absolute state operation. |
| `payload` | Number of payload bytes used by this packet. |
| `queue` | Packets still waiting after this packet was removed. |

The matching handler then confirms what it applied:

```text
[V][synchrocast.cover]: Apply STATE_BROADCAST SET_POSITION=0.500 to 'Garage Door' hash=0xA1B2C3D4
```

Together, these lines answer two different questions:

1. Did the dispatcher route the packet?
2. Did the domain handler apply it to the expected local entity?

## Useful Diagnostic Messages

### `No handler`

```text
[V][synchrocast.dispatcher]: No handler: type=STATE_BROADCAST domain=LIGHT ...
```

The packet reached the dispatcher, but that domain handler was not registered.
Check that the domain is implemented and listed in the device configuration.

### `No cover`, `No fan`, or `No valve`

The domain handler exists, but no local entity matches the incoming identity.

- Compare the YAML entity IDs on sender and receiver.
- Check that the entity is listed under the correct Synchrocast domain.
- Remember that visible Home Assistant names do not perform the matching.

### `Rejected relative TOGGLE state`

This is intentional. Toggle is relative to a device's current state. Applying a
toggle as synchronized state could make two devices move in opposite directions.
Use an absolute state broadcast instead.

### `Rejected SET_POSITION`

For Cover and Valve, position must be a finite value from 0% to 100%. The
application payload uses the equivalent range from `0.0` to `1.0`.

### `Rejected fan speed`

Fan speed must be a whole level supported by the receiving fan. If a fan exposes
three levels, accepted values are 1, 2, and 3.

### `Packet queue full`

```text
[W][synchrocast.dispatcher]: Packet queue full; dropped=3 capacity=16
```

The producer delivered packets faster than the main loop could apply them.
Warnings are rate-limited so a busy queue cannot flood the logger.

Occasional state coalescing is normal and does not produce a warning. Repeated
queue-full warnings need investigation:

- Look for a sender transmitting the same command in a tight loop.
- Check whether another ESPHome component is blocking the main loop.
- Check the remaining `queue=` value in verbose dispatch logs.
- Capture dispatcher statistics during simulation.

## Common Setup Problems

### ESPHome reports `Component not found: synchrocast`

- Confirm the `external_components` source is
  `github://effelle/Synchrocast@stage`.
- Remove an incomplete `components:` allow-list.
- Confirm the device can reach GitHub during preparation.
- The public YAML layer is scheduled for Step 3 and does not exist in the current
  skeleton yet.

### The follower does not react

- Confirm sender and receiver use the same `group`.
- Confirm they use the same `key`.
- Confirm matching entities use the same YAML `id`.
- Confirm there is only one leader in the group.
- Confirm the receiver is a follower or satellite.
- For ESP-NOW, confirm both devices use the same Wi-Fi channel.
- For UDP, confirm both devices are on the same reachable network.

### A controller does nothing

- Confirm the device uses `role: controller` or `role: satellite`.
- Confirm `input` points to an existing local binary sensor.
- Confirm `target` matches the leader entity ID exactly.
- Confirm `intent` is valid for the target domain.

## Capturing a Useful Simulation Log

When reporting a problem, include:

- sender role and receiver role;
- domain and intended action;
- matching entity IDs from both YAML files;
- the dispatcher line;
- the domain-handler line or rejection line;
- any queue-full warning;
- the smallest YAML configuration that reproduces the problem.

Avoid publishing your Synchrocast key, Wi-Fi password, or other secrets.
