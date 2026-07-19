# Architecture and Resource Budget

This page is for contributors, simulation authors, and reviewers. Normal users
can begin with [Getting Started](getting_started.md).

## Boundary

Synchrocast is standalone. It does not depend on ChimeraFX or `cfx_sync`.

ESPHome `2026.7.0` is the current compatibility baseline. Checks against older
versions can be useful, but they do not replace the 2026.7 build.

The transport layer is responsible for receiving, authenticating, decrypting,
and parsing a packet. The application layer begins with a populated
`SynchrocastPacket` and ends with a native ESPHome entity call.

```text
transport task
    -> validate and enqueue fixed packet
    -> fixed ring buffer
    -> ESPHome main loop
    -> domain handler
    -> local ESPHome entity
```

Transport code must call `enqueue_packet()` from task context. It must not call a
domain handler or ESPHome entity directly.

## Good Citizen Rules

1. Do not allocate memory in the packet path.
2. Do not call ESPHome entities from Wi-Fi, ESP-NOW, UDP, or other worker tasks.
3. Bound all queue sizes, registries, scans, retries, and work per loop.
4. Prefer the newest absolute state, but never reorder a user intent.
5. Keep normal logs quiet and rate-limit warnings that network traffic can
   trigger repeatedly.
6. Do not initialize handlers or domain dependencies that the YAML does not use.
7. Treat malformed or unsupported authenticated packets as diagnostics, not a
   reason to destabilize the node.

## Current Static Budget

The core uses compile-time fixed storage:

| Item | Current value | 32-bit storage |
| --- | --- | --- |
| Application packet | 24 bytes | 24 bytes |
| Dispatcher queue | 16 packets | 384 bytes |
| Per-loop budget | 4 packets | No extra storage |
| Domain dispatch table | 15 pointer slots | 60 bytes |
| Entity registry | 16 entries per instantiated domain | Approximately 128 bytes plus bookkeeping |

The packet has a compile-time size assertion. Its fields are ordered to avoid
alignment padding without using packed or unaligned accesses.

The in-memory packet is not a wire format. Transport codecs must serialize each
field explicitly so compiler layout never becomes part of the network protocol.

## Queue Behavior

The dispatcher uses a fixed ring buffer:

- A packet is validated before it enters the queue.
- No heap allocation occurs while enqueueing or dequeueing.
- ESP32 queue operations use a short cross-core critical section.
- ESP8266 relies on its single-core, non-preemptive execution model.
- Host builds can protect the same boundary with a standard mutex.

Pending state broadcasts for the same domain and entity may be coalesced. The
scan begins at the newest queued packet. If a newer intent for that entity is
found, coalescing stops. This preserves command ordering while avoiding repeated
application of stale intermediate state.

When the queue is full, the incoming packet is dropped and counted. The main
loop emits a rate-limited warning rather than logging from the producer task.

## Main-Loop Fairness

The dispatcher applies at most four packets in one `loop()` call. Remaining
packets stay queued for the next ESPHome iteration. This prevents a burst from
monopolizing the cooperative main loop.

The per-loop budget is intentionally separate from queue capacity:

- queue capacity absorbs a short burst;
- loop budget limits latency imposed on other ESPHome components.

Both values must remain small and measurable. Increasing either requires a
simulation result that demonstrates why the additional memory or loop time is
needed.

## Domain Dispatch

Handlers are stored by numeric domain slot, making selection constant-time.
Handlers do not own their ESPHome entities. Code generation will register
existing entity pointers during setup.

Each handler uses a fixed entity table and a bounded linear lookup. Sixteen
entries keep the table predictable and small; at that size, a linear scan is
cheaper and simpler than a heap-backed map.

Fan speed-count traits are cached once during registration. This avoids an extra
traits copy on every incoming speed packet. ESPHome still performs its own final
call validation.

## Logging Policy

Configuration errors remain error-level logs. Queue exhaustion remains a
rate-limited warning. Expected runtime decisions use verbose logs:

- packet routing;
- missing handler;
- missing entity;
- unsupported intent;
- malformed domain payload;
- state-toggle rejection;
- native action applied.

Verbose message arguments compile out with lower logger levels. `log_stats()` is
also compile-time gated so a normal build does not take a queue lock merely to
format disabled statistics.

## Simulation Surface

Upcoming simulations can use these dispatcher operations:

- `register_handler()` installs one handler for a domain.
- `enqueue_packet()` returns queued, coalesced, queue-full, or invalid.
- `loop()` drains no more than four packets.
- `queue_depth()` reports pending work.
- `get_stats()` returns queue and dispatch counters.
- `log_stats()` prints the counters at verbose level.

Important scenarios to simulate:

1. An intent request reaches the correct entity.
2. A state broadcast reaches the correct entity.
3. Consecutive state broadcasts for one entity coalesce.
4. An intent between two states prevents unsafe coalescing across the intent.
5. More than four queued packets remain for the next loop.
6. A full queue drops input and emits a rate-limited warning.
7. Unknown domains, handlers, hashes, and malformed payloads fail safely.
8. Concurrent producer and main-loop access does not corrupt queue indices.
9. A Light state with brightness `0%` remains `0%`; it is not clamped upward.
10. Turning that Light on without an explicit brightness applies ESPHome's
    `100%` fallback at turn-on time.
11. Turning that Light on with an explicit brightness preserves the requested
    value instead of applying the fallback.

The Light cases follow
[ESPHome PR #17103](https://github.com/esphome/esphome/pull/17103). They are a
contract for the planned Light handler; the current skeleton does not yet
implement that domain.

## Review Checklist

Before adding a domain or transport feature, verify:

- fixed upper bounds are documented;
- new objects are created only when configured;
- callbacks do not call ESPHome entities off the main loop;
- packet serialization does not copy raw structs;
- logs expose domain, entity identity, intent, and outcome at verbose level;
- normal logging cannot be flooded by routine network traffic;
- syntax checks cover every new source file;
- simulations cover queue pressure and ordering, not only the happy path.
