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
| `entity` | Compact hash of an observational share key, or of the actuator ESPHome ID. |
| `intent` | Absolute value/state operation or user command. |
| `payload` | Application value bytes; a numeric sensor uses four. |
| `queue` | Decoded packets still waiting for the main loop. |

The hash is useful for comparing log lines, but users should compare the YAML
names that produce it. For Sensor, Binary Sensor, and Text Sensor, compare
the custom share key. For the current Cover, Fan, and Valve handlers, compare the local
ESPHome entity ID.

## Transport States

### `standalone active`

This is the normal state when ChimeraFX is not configured. Synchrocast owns its
transport and packets can flow. ESP32 uses ESP-NOW with `transport: auto`;
ESP8266 uses UDP.

With standalone ESP-NOW, verbose logs may report a scheduled fallback, a rearm,
and `Transport recovered`. After Wi-Fi has been absent for the internal grace
period, Synchrocast moves to channel 6. When Wi-Fi returns or changes channel,
it rearms again and refreshes the leader's current semantic state. These are
normal recovery messages, not a request to add YAML options.

### `attached to cfx_sync`

This is the expected state only when the same YAML contains a valid `cfx_sync:`
block for ChimeraFX lights or Magic Buttons. CFX owns the radio/socket and
Synchrocast has registered its distinct authenticated protocol without creating
another transport.

CFX owns physical channel recovery in this mode. Synchrocast observes the
recovery generation published by shared-transport API v2 and refreshes its own
state; it does not poll, rearm, or change the CFX radio. With verbose logs,
`Shared transport recovery generation=` confirms that ChimeraFX published a
completed rearm to attached consumers.

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

The packet type is not allowed from the claimed role. Observed sensor state may
come from a `leader`; future command intents may come from a
`controller` or `satellite`. A `follower` cannot publish sensor state.

### `STATE_REQUEST sent`, `Processed STATE_REQUEST`, or `Suppressed repeated STATE_REQUEST`

These are normal recovery diagnostics. A follower or satellite asks the group
leader for current absolute state at startup or after transport recovery. One
broadcast response benefits every listener, so a leader suppresses duplicate
requests received within a short window. No YAML action is required.

### `Canonical leader restarted`

The same physical leader started a new boot session. Its stable node identity
still matches, so Synchrocast accepts its state immediately instead of waiting
for the old session to become stale.

### `malformed` or `unsupported type`

The frame has invalid lengths, fields, payload semantics, or a domain that this
build intentionally did not include. Synchrocast claims and discards a malformed
frame for its group rather than letting unsafe data reach an ESPHome entity.

## Observational Value Problems

### The leader never logs `Broadcast ...`

- Confirm the source entity has a valid state.
- Confirm the leader maps the share key to the correct local ESPHome ID.
- Confirm the Synchrocast role is `leader`.
- Confirm the transport state is `standalone active` or, on a device that also
  uses ChimeraFX, `attached to cfx_sync`.

A numeric `NaN` or infinite source is unavailable. Text that is invalid UTF-8
or longer than 64 bytes is also unavailable.

### The reader has no value

- Compare `group`, key, domain, and share key on both devices.
- Confirm the leader maps that share key and the reader lists it under the same
  observational domain.
- Check the reader's `rx_authenticated`, dispatcher, and domain-handler logs
  in that order.

The reader does not need a template entity. Synchrocast creates a native,
read-only ESPHome entity for every share key it lists.

### The dispatcher `filtered` counter rises

This is normally expected. The device received an authenticated broadcast whose
domain or share key it did not request, so Synchrocast discarded it before the
packet queue. If a wanted value is missing, compare the share key's spelling,
underscores, and case. A valid share key is lowercase and contains no spaces.

### `Ignored competing canonical leader`

Two active devices are publishing authoritative state for the same group. The
receiver keeps the first authenticated stable node and ignores the contender so
sensors and actuators cannot jump between leaders. Remove the duplicate leader
configuration. After the current owner becomes stale, a new leader may take
ownership.

### `Numeric receiver stale`, `Binary receiver stale`, or `Text receiver stale`

No recovery refresh arrived before Synchrocast's fixed internal timeout. The
local entity was intentionally marked unavailable instead of retaining an old
value. Check leader power, transport state, key, and signal quality. This timeout
is a safety behavior, not a YAML setting.

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

This message refers to an invalid direct speed intent. Canonical fan state uses
a percentage and maps it to the nearest level supported by the receiver.

### The Cover, Fan, or Valve leader does not transmit

- Confirm the entity ID exists locally and is listed in the leader's matching
  `covers`, `fans`, or `valves` option.
- Confirm the role is `leader`.
- Confirm the transport is active.
- Enable the matching domain's verbose log and look for `Broadcast canonical`.

Messages such as `Ignored unsupported cover tilt` or `Ignored unsupported fan
oscillation` are expected when devices have different capabilities. The
receiver still applies every compatible field in the same state.

## Queue Pressure

```text
[W][synchrocast.dispatcher]: Packet queue full; dropped=3 capacity=16
```

The authenticated producer delivered packets faster than the cooperative main
loop could apply them. Warnings are rate-limited. Occasional state coalescing is
normal; repeated full-queue warnings need investigation:

- look for a sender transmitting in a tight loop;
- check whether the source sensor itself updates unnecessarily often;
- check whether another ESPHome component blocks the main loop;
- inspect `queue=` and the periodic dispatcher statistics.

## Repository and Schema Errors

### `Component not found: synchrocast`

- Confirm the source is `github://effelle/Synchrocast@stage`.
- Remove an incomplete `components:` allow-list.
- Confirm the ESPHome host can reach GitHub while preparing the build.

### ESPHome rejects a role, mapping, or duplicate

Schema rejection is deliberate. Common causes are a source mapping on a
non-leader, using a mapping where a reader must provide a list, the same share
key twice in one domain, reusing one generated reader as a leader source, or
more than 16 entries in a domain.

## Capturing a Useful Report

Include:

- ESPHome and Synchrocast versions or pinned branches;
- leader and reader roles;
- domain and share key (or actuator ESPHome ID);
- transport state on both devices;
- leader-source, authentication/stats, dispatcher, and reader-handler lines;
- the smallest YAML configuration that reproduces the problem.

Never publish Synchrocast keys, Wi-Fi passwords, or other secrets.
