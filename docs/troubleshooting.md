# Troubleshooting and Verbose Logs

Synchrocast keeps normal logs quiet. Enable focused verbose logs temporarily
when a value is missing or a device does not react.

## Enable the Relevant Tags

```yaml
logger:
  level: VERBOSE
  logs:
    synchrocast: VERBOSE
    synchrocast.transport: VERBOSE
    synchrocast.dispatcher: VERBOSE
    synchrocast.sensor: VERBOSE
    synchrocast.binary_sensor: VERBOSE
    synchrocast.text_sensor: VERBOSE
    synchrocast.cover: VERBOSE
    synchrocast.fan: VERBOSE
    synchrocast.valve: VERBOSE
```

Keep only the domains you are testing. Remove verbose logging after setup;
packet-by-packet output is unnecessarily noisy during normal operation.
Add `cfx_sync.bus: VERBOSE` only when the same device also uses ChimeraFX.

## Follow a Value from Publisher to Receiver

For an observational value, look for these stages:

1. Publisher: `Broadcast numeric state`, `Broadcast binary state`, or
   `Broadcast text state`.
2. Receiver main tag: the `rx_authenticated` counter increases.
3. Dispatcher: a `Dispatch type=STATE_BROADCAST ...` line appears.
4. Receiver handler: `Published remote ...` confirms the native ESPHome entity
   was updated.

A dispatcher line looks like:

```text
[V][synchrocast.dispatcher]: Dispatch type=STATE_BROADCAST domain=SENSOR entity=0xA1B2C3D4 intent=SET_VALUE payload=4 queue=0
```

| Field | Meaning |
| --- | --- |
| `type` | `INTENT_REQUEST`, `STATE_BROADCAST`, or `HEARTBEAT`. |
| `domain` | ESPHome value or actuator type carried by the packet. |
| `entity` | Compact hash of observational `sync_id`, or of the actuator ESPHome ID. |
| `intent` | Absolute value/state operation or user command. |
| `payload` | Application value bytes; a numeric sensor uses four. |
| `queue` | Decoded packets still waiting for the main loop. |

The hash is useful for comparing log lines, but users should compare the YAML
names that produce it. For Sensor, Binary Sensor, and Text Sensor, compare
`sync_id`. For the current Cover, Fan, and Valve receivers, compare the local
ESPHome entity ID.

## Transport States

### `standalone active`

This is the normal state when ChimeraFX is not configured. Synchrocast owns its
transport and packets can flow. ESP32 uses ESP-NOW with `transport: auto`;
ESP8266 uses UDP.

### `attached to cfx_sync`

This is the expected state only when the same YAML contains a valid `cfx_sync:`
block for ChimeraFX lights or Magic Buttons. CFX owns the radio/socket and
Synchrocast has registered its distinct authenticated protocol without creating
another transport.

### `waiting for cfx_sync`

Synchrocast found CFX and will not start a second transport. Check earlier CFX
logs for an ESP-NOW or UDP startup error. There is intentionally no hidden
fallback.

### `blocked`

Transport initialization failed or the request conflicts with the selected
owner. In standalone mode, check platform support, Wi-Fi, and UDP port use. In
attached mode, return to `transport: auto`, remove a conflicting
Synchrocast `udp_port`, and inspect the CFX startup logs.

## Packet Rejections

### `authentication failed`

An `SCST` frame named this group but did not have a valid authentication tag.
The most common setup cause is a different Synchrocast key on the sender and
receiver. Compare the secret values without posting them in logs or issue
reports.

Authentication protects integrity and identity within the shared-key group. It
does not encrypt the value.

### `unsupported version`

The devices use incompatible Synchrocast protocol versions. Pin the same
repository branch or release on every member, clean ESPHome's external
component cache if necessary, and rebuild both devices.

### `Rejected duplicate/stale frame`

Synchrocast has already accepted this sender sequence, or an older copy arrived
after a newer one. This can be normal when the same authenticated packet reaches
the device over both ESP-NOW and UDP. A rapidly growing replay counter with
missing new values warrants a sender log capture.

### `Rejected role`

The packet type is not allowed from the claimed role. Observed state may come
from a `leader` or `satellite`; future command intents may come from a
`controller` or `satellite`. A `follower` cannot publish sensor state.

### `malformed` or `unsupported type`

The frame has invalid lengths, fields, payload semantics, or a domain that this
build intentionally did not include. Synchrocast claims and discards a malformed
frame for its group rather than letting unsafe data reach an ESPHome entity.

## Observational Value Problems

### The publisher never logs `Broadcast ...`

- Confirm the source entity has a valid state.
- Confirm it is listed under `publish`, not `receive`.
- Confirm the Synchrocast role is `leader` or `satellite`.
- Confirm the transport state is `standalone active` or, on a device that also
  uses ChimeraFX, `attached to cfx_sync`.
- Remember that `min_interval` limits retries as well as successful sends.

A numeric `NaN` or infinite source is unavailable. Text that is invalid UTF-8
or longer than 64 bytes is also unavailable.

### The receiver has no value

- Compare `group`, key, domain, and `sync_id` on both devices.
- Confirm one side uses `publish` and the other uses `receive`.
- Check the receiver's `rx_authenticated`, dispatcher, and domain-handler logs
  in that order.
- Make sure `stale_after` is comfortably longer than `refresh_interval`.

The receiver does not need a template entity. The entity created inside
`receive:` is the value to use.

### `No numeric receiver`, `No binary receiver`, or `No text receiver`

The authenticated packet reached the right domain handler, but its hashed
`sync_id` is not registered on this device. Compare spelling, punctuation, and
case. A valid `sync_id` is lowercase and contains no spaces.

### `Ignoring competing publisher`

Two active devices are publishing the same group, domain, and `sync_id`. The
receiver keeps the first authenticated publisher and ignores the contender so
the value cannot jump between sources. Remove the duplicate publisher. After
the current owner becomes stale, a new publisher may take ownership.

### `Numeric receiver stale`, `Binary receiver stale`, or `Text receiver stale`

No refresh arrived before `stale_after`. The local entity was intentionally
marked unavailable instead of retaining an old value. Check publisher power,
transport state, `refresh_interval`, key, and signal quality.

### `Text state rejected`

The source is longer than 64 UTF-8 bytes or is not valid UTF-8. The log includes
the byte length. Accented characters and many symbols use more than one byte.
Synchrocast does not truncate or split the string; shorten it at the source.

## Dispatcher and Actuator Messages

### `No handler`

The packet was valid, but this device did not configure that domain. Only used
domains are compiled and registered to avoid wasting memory.

### `No cover`, `No fan`, or `No valve`

The receive handler exists, but no listed local entity matches the packet hash.
Compare the ESPHome entity IDs, not the visible Home Assistant names.

### `Rejected relative TOGGLE state`

This is intentional. Toggle is relative to each device's current state, so
using it as an absolute broadcast could make devices diverge. State broadcasts
must carry an absolute state or position.

### `Rejected SET_POSITION`

Cover and Valve position must be finite and between `0.0` closed and `1.0`
open.

### `Rejected fan speed`

Fan speed must be a whole level supported by the receiver. A three-speed fan
accepts 1, 2, or 3, not a fraction or out-of-range value.

### The Cover, Fan, or Valve leader does not transmit

Automatic outbound observation for these actuator domains is not implemented
yet. Their current lists register receive handlers. Sensor, Binary Sensor, and
Text Sensor have the complete publisher/receiver path.

## Queue Pressure

```text
[W][synchrocast.dispatcher]: Packet queue full; dropped=3 capacity=16
```

The authenticated producer delivered packets faster than the cooperative main
loop could apply them. Warnings are rate-limited. Occasional state coalescing is
normal; repeated full-queue warnings need investigation:

- look for a sender transmitting in a tight loop;
- increase a needlessly short `min_interval`;
- check whether another ESPHome component blocks the main loop;
- inspect `queue=` and the periodic dispatcher statistics.

## Repository and Schema Errors

### `Component not found: synchrocast`

- Confirm the source is `github://effelle/Synchrocast@stage`.
- Remove an incomplete `components:` allow-list.
- Confirm the ESPHome host can reach GitHub while preparing the build.

### ESPHome rejects a role, duplicate, or interval

Schema rejection is deliberate. Common causes are a publisher on a `follower`,
the same `sync_id` twice in one domain, one generated receiver republished on
the same device, more than 16 bindings, or `refresh_interval` not being longer
than `min_interval`.

## Capturing a Useful Report

Include:

- ESPHome and Synchrocast versions or pinned branches;
- sender and receiver roles;
- domain and `sync_id` (or actuator ESPHome ID);
- transport state on both devices;
- publisher, authentication/stats, dispatcher, and receiver-handler lines;
- the smallest YAML configuration that reproduces the problem.

Never publish Synchrocast keys, Wi-Fi passwords, or other secrets.
