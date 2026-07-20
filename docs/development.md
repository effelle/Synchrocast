# Architecture and Resource Budget

This page is for contributors, simulation authors, and reviewers. Normal users
can begin with [Getting Started](getting_started.md).

## Boundary

Synchrocast is the canonical general synchronization component. It owns its
application protocol, authentication, replay protection, queues, domain
handlers, and native receiver entities. ChimeraFX is an optional transport
sharing partner, not Synchrocast's packet decoder or functional dependency.

When `cfx_sync:` is configured, Synchrocast conditionally compiles a narrow
adapter and borrows the already-running CFX ESP-NOW/UDP bus. Without CFX,
Synchrocast compiles and owns its standalone transport. ESP32 `auto` selects
ESP-NOW; ESP8266 `auto` selects UDP. ChimeraFX is never auto-loaded merely
because its repository files are present.

ESPHome `2026.7.0` is the current compatibility baseline. Older-version checks
can be useful, but they do not replace a 2026.7 source-generation and firmware
build.

The receive path is:

```text
raw transport frame
    -> signature, group, length, and protocol-version checks
    -> HMAC authentication and semantic validation
    -> role and replay checks
    -> fixed dispatcher queue
    -> ESPHome main loop, at most four packets per pass
    -> fixed domain handler
    -> native ESPHome entity
```

Transport callbacks may offer raw bytes but must never call a domain handler or
ESPHome entity directly. `on_transport_packet()` authenticates and converts the
wire frame into a bounded `SynchrocastPacket`; `enqueue_packet()` is the only
handoff to application processing.

## Authenticated Wire Format

Synchrocast frames use a distinct `SCST` signature and protocol version 1. Every
multibyte field is serialized in network byte order; the in-memory C++ struct is
never copied onto the network.

| Field group | Bytes | Notes |
| --- | --- | --- |
| Signature and version | 5 | `SCST`, version `1` |
| Message, domain, intent, role, flags | 5 | Flags are currently zero |
| Payload length | 2 | Maximum 64 |
| Group hash | 4 | Routes one raw frame among configured groups |
| Sender boot ID | 4 | Random nonzero identity for one boot |
| Sequence | 4 | Monotonic nonzero counter |
| Entity hash | 4 | `sync_id` or current actuator identity |
| Payload | 0-64 | Explicit domain representation |
| Authentication tag | 16 | Truncated HMAC-SHA256 over header and payload |

The fixed header is 28 bytes and the largest authenticated frame is 108 bytes,
well below the shared transport MTU of 250 bytes. Numeric payloads are four
bytes, binary payloads one byte, and an unavailable value has no payload. Empty
text is distinct from unavailable because its intent remains `SET_VALUE`.

The YAML passphrase is domain-separated and hashed into a 32-byte HMAC key at
code-generation time. The protocol authenticates integrity and group
membership; it does **not** encrypt values.

Tag comparison is constant-time. A frame for the configured group is not
queued until its authentication tag and domain semantics pass. Frames for a
different Synchrocast group remain unclaimed so another configured group on the
same shared bus can inspect them.

Each outbound packet carries the current boot ID and a sequence. A fixed table
tracks up to eight remote boot sessions. Equal or lower sequences are discarded
before dispatch, which also suppresses the duplicate copy when one packet is
received through both ESP-NOW and UDP. Sender roles are enforced after
authentication: state comes from leader/satellite and intent comes from
controller/satellite; every role may send a heartbeat.

## Transport Ownership and CFX Coexistence

There is exactly one transport owner on a device:

| Configuration | Owner | Synchrocast state |
| --- | --- | --- |
| No `cfx_sync:` block | Synchrocast | `standalone active` |
| Active compatible `cfx_sync:` block | `cfx_sync` | `attached to cfx_sync` |
| CFX selected but unavailable or incompatible | None | `waiting` or `blocked`; no automatic fallback |

The standalone backend is one shared, fixed-capacity owner for all Synchrocast
groups on the device. It stores at most eight sink pointers. UDP polling handles
at most four datagrams per millisecond/main-loop pass and uses a fixed 251-byte
receive buffer so an oversized datagram can be detected without truncating it
into a valid-looking frame. Standalone UDP defaults to port `39581`.

All standalone groups on one device must resolve to the same transport and UDP
port. This prevents multiple sockets or competing ESP-NOW owners. Code
generation enforces the rule before firmware generation.

In attached mode Synchrocast:

- registers one detachable raw consumer with the CFX bus;
- receives frames that do not claim the `CFXS` protocol;
- inherits active ESP-NOW/UDP availability and UDP port `39580`;
- sends bounded raw `SCST` broadcasts through the active CFX transport;
- never starts, stops, or rearms the shared radio;
- never opens another UDP socket;
- never maintains a competing peer table.

Malformed or unsupported `CFXS` frames remain owned by ChimeraFX and are not
offered to Synchrocast. A Synchrocast instance claims a same-group `SCST` frame
after its signature/group routing decision even when later authentication or
semantic validation fails, preventing unsafe fall-through.

## Good Citizen Rules

1. Do not allocate memory in the frame, replay, or dispatcher path.
2. Do not call ESPHome entities from Wi-Fi, ESP-NOW, UDP, or worker-task
   callbacks.
3. Bound every queue, registry, scan, retry, and amount of work per loop.
4. Prefer the newest absolute state, but never reorder a user intent.
5. Keep normal logs quiet and rate-limit warnings that traffic can trigger.
6. Compile handlers and ESPHome domain dependencies only when YAML uses them.
7. Treat malformed or unsupported authenticated packets as diagnostics, never
   a reason to destabilize the node.
8. Never silently truncate user data or reinterpret unavailable as a normal
   zero, `OFF`, or empty value.

## Current Static Budget

Core storage uses fixed compile-time bounds:

| Item | Current bound | Storage consequence |
| --- | --- | --- |
| Authenticated wire frame | 108 bytes maximum | One fixed encode buffer |
| Application packet without Text Sensor | 32 bytes | 16-packet queue is 512 bytes |
| Application packet with Text Sensor | 80 bytes | 16-packet queue is 1,280 bytes |
| Dispatcher work | 4 packets per loop | Remaining packets wait |
| Domain dispatch table | 16 pointer slots | 64 bytes on a 32-bit target |
| Replay table | 8 boot sessions | Fixed array, oldest entry replaced |
| Standalone group sinks | 8 pointers | One transport shared by all groups |
| Standalone UDP receive | 251-byte stack buffer | At most 4 datagrams per poll |
| Observational handler | 16 publishers plus 16 receivers per instantiated domain | No heap-backed map |
| Component instances | 8 maximum | Matches shared-consumer bound |

Text Sensor is the only current domain that needs a 64-byte application
payload. If no text sensor publisher or receiver is configured, the preprocessor
keeps each packet at 32 bytes and avoids the larger queue. The Text Sensor
handler itself is also omitted when unused.

Each observational handler reserves its publisher and receiver tables only when
that domain is configured. A text publisher keeps one fixed last-sent buffer;
the current value remains owned by the ESPHome source entity until it is copied
into the bounded packet. This avoids a second 64-byte buffer per publisher.

## Queue and Loop Behavior

The dispatcher uses a fixed ring buffer:

- validation occurs before enqueue;
- no heap allocation occurs while enqueueing or dequeueing;
- ESP32 queue operations use a short cross-core critical section;
- ESP8266 uses its single-core execution model;
- host builds can protect the boundary with a standard mutex.

Pending state broadcasts for the same domain and entity may be coalesced. The
scan starts with the newest packet. If a newer intent for that entity appears,
coalescing stops so commands cannot be crossed or reordered.

At most four packets are applied in one `loop()` call. Queue capacity absorbs a
short burst; the loop budget protects latency for other ESPHome components. A
full queue drops the incoming packet, counts the drop, and emits only a
rate-limited warning from the main loop.

## Observational Publisher and Receiver Model

Sensor, Binary Sensor, and Text Sensor handlers poll their source entity's final
published state from the main loop. This avoids allocating per-binding callback
closures and naturally observes the value after normal ESPHome filters.

Each publisher:

- retains only current and last-sent state;
- coalesces changes during `min_interval`;
- periodically repeats the latest absolute value at `refresh_interval`;
- transmits unavailable explicitly;
- applies numeric `delta` only to immediate-change traffic.

Each receiver is a native ESPHome entity. It tracks one active publisher boot
ID for each `sync_id`, ignores competing publishers with a rate-limited warning,
and clears ownership after `stale_after`. Stale and explicit-unavailable inputs
invalidate the entity; they are not converted into ordinary values.

Text is valid UTF-8 and at most 64 bytes. Oversized or invalid input produces
one rate-limited-by-state warning and unavailable. It is never truncated or
fragmented.

Code generation also rejects a generated receiver reused as a publisher on the
same device, preventing a simple echo loop. Each handler rejects a duplicate
hash across its own publisher and receiver tables as a second defense.

## Domain Dispatch

Handlers are stored by numeric domain slot, making selection constant-time.
They do not own source entities. Code generation registers existing publisher
pointers and creates only explicitly configured receiver entities.

Cover, Fan, and Valve handlers currently apply received state/intent packets.
Their automatic outbound observation is not implemented. Numeric Sensor,
Binary Sensor, and Text Sensor provide the complete publish/receive lifecycle.

## Logging Policy

Configuration errors remain errors. Queue exhaustion and competing publishers
use rate-limited warnings. Expected runtime decisions use verbose logs:

- transport transitions and periodic counters;
- authentication, malformed, role, and replay rejection;
- packet routing and queue depth;
- missing handler or entity;
- unsupported intent or payload;
- publisher handoff, receiver publication, unavailability, and staleness.

Verbose message arguments compile out at lower logger levels. Periodic stats are
also compile-time gated so a normal build does not take a queue lock merely to
format disabled output.

## Validation Checklist

Before adding a domain or transport feature, verify:

- fixed upper bounds are documented;
- new objects and component dependencies exist only when configured;
- callbacks never update ESPHome entities off the main loop;
- packet serialization never copies raw structs;
- authentication precedes replay and application dispatch;
- logs expose domain, identity, intent, and outcome at verbose level;
- traffic-triggered warnings cannot flood normal logs;
- ESPHome 2026.7 validates, generates, compiles, and links representative
  configurations;
- tests cover malformed frames, replay, role rules, queue pressure, coalescing,
  availability, and conditional memory footprints.

The planned Light handler must also preserve brightness `0` while off and apply
ESPHome's later turn-on fallback only when an ON request has no explicit
brightness. See [Domains and Capabilities](domains.md).
