# Using Synchrocast with ChimeraFX

This guide is for a device that needs both components. A common example is:

- ChimeraFX `cfx_sync` synchronizes a light and provides Magic Button behavior.
- Synchrocast registers a Fan, Cover, or Valve on the same ESPHome device.

Both protocols can use ESP-NOW or UDP, but the device must not start two owners
for the same radio and socket resources. When both YAML blocks are configured,
ChimeraFX owns the transport and Synchrocast attaches to it automatically.

> Synchrocast is currently an early skeleton. Transport sharing and YAML
> registration are implemented, but the authenticated Synchrocast wire codec
> is not. This guide prepares a valid coexistence configuration; live
> Synchrocast state exchange comes in a later milestone.

## 1. Add Both Repositories

Put both sources in `external_components:`. Their order does not matter:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@stage
    refresh: always
  - source: github://effelle/Synchrocast@stage
    refresh: always
```

Adding a repository only makes its components available. It does not enable
either component by itself.

## 2. Configure Each Component for Its Own Job

The example below assumes `room_light` and `room_fan` are already declared in
the same ESPHome YAML file:

```yaml
# Specialized ChimeraFX light synchronization.
cfx_sync:
  id: room_light_sync
  role: follower
  lights:
    - room_light
  group: living_room_lights
  key: !secret cfx_sync_key
  transport: auto

# General Synchrocast fan registration.
synchrocast:
  id: room_fan_sync
  role: follower
  fans:
    - room_fan
  group: living_room_fan
  key: !secret synchrocast_key
  transport: auto
```

No bridge ID or extra transport block is required. Synchrocast detects the
configured `cfx_sync:` integration during ESPHome validation.

The group names and keys above are intentionally different. They belong to
different packet protocols and do not need to match. Devices participating in
the same CFX group must share the CFX group and key; devices participating in
the same Synchrocast group must share the Synchrocast group and key.

## 3. Understand Who Owns What

| Resource or responsibility | Owner |
| --- | --- |
| Start and manage ESP-NOW | `cfx_sync` |
| Open and poll the shared UDP socket | `cfx_sync` |
| CFX peers, authentication, and packet parsing | `cfx_sync` |
| Recognize and parse Synchrocast packets | Synchrocast wire codec |
| Route decoded Fan, Cover, or Valve packets | Synchrocast dispatcher |

CFX first checks whether an incoming packet belongs to its protocol. Valid CFX
packets and malformed CFX-looking packets stay inside CFX. Only packets that
are clearly not CFX are offered to registered shared-protocol consumers. This
prevents another component from accidentally claiming a damaged CFX packet.

The sharing hook uses fixed-capacity registration and raw byte buffers. It does
not allocate a new packet container on every receive or send.

## 4. Transport Rules

For normal use, leave both components on `transport: auto`.

- On ESP32, CFX normally provides ESP-NOW. A CFX leader can also provide UDP.
- On ESP8266, CFX uses UDP.
- If Synchrocast explicitly requests `espnow` or `udp`, that transport must
  already be active in CFX.
- Attached UDP always inherits CFX's internal port `39580`.
- Synchrocast never starts a hidden second transport if CFX is unavailable.
  It waits or reports a blocked configuration instead.

That last rule is intentional: one visible owner is safer and easier to debug
than two components silently competing for the same resources.

## 5. Enable Focused Logs While Testing

```yaml
logger:
  level: VERBOSE
  logs:
    cfx_sync.bus: VERBOSE
    synchrocast: VERBOSE
    synchrocast.dispatcher: VERBOSE
    synchrocast.fan: VERBOSE
```

After startup, look for one of these Synchrocast states:

- `attached to cfx_sync`: CFX is active and Synchrocast attached successfully.
- `waiting for cfx_sync`: Synchrocast found CFX and is waiting for its selected
  transport to become active.
- `blocked`: the explicit transport or UDP-port request conflicts with what CFX
  provides. Return to `transport: auto` and inspect the preceding CFX log.

During development, `Shared frame left unclaimed` means the sharing hook worked
but no authenticated Synchrocast wire decoder claimed that raw frame. This is
expected until the codec milestone is complete.

Remove verbose logging after testing. Normal operation is designed to remain
quiet.
