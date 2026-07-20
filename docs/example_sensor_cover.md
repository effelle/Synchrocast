# Example: One Sensor, Two Followers, and One Cover Satellite

This example uses four ESPHome devices:

1. one leader sharing a numeric sensor and a cover;
2. two followers reading the sensor;
3. one satellite mirroring the cover.

Both the sensor and cover paths in this example work in the current `stage`
build.

## Add Synchrocast

Add the repository to every device:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    components: [synchrocast]
    refresh: always
```

Store the common key in each device's `secrets.yaml`:

```yaml
synchrocast_key: "replace-with-the-same-long-random-key"
```

All four devices use the same group and key.

## Leader

The leader already has a numeric sensor with ID `phase_2_voltage` and a cover
with ID `living_room_cover`:

```yaml
synchrocast:
  role: leader
  group: living_room
  key: !secret synchrocast_key

  sensors:
    meter_phase_2: phase_2_voltage

  covers: living_room_cover
```

`meter_phase_2` is a custom share key. It is not a voltage domain or a device
class. The name could be any valid share key.

## Follower One

```yaml
synchrocast:
  role: follower
  group: living_room
  key: !secret synchrocast_key
  sensors: meter_phase_2
```

Synchrocast creates a read-only sensor named `Meter Phase 2` on this device.
Its local ESPHome ID is `meter_phase_2`, so automations can read
`id(meter_phase_2).state` directly; no Template Sensor is needed.

## Follower Two

The second follower uses the same block:

```yaml
synchrocast:
  role: follower
  group: living_room
  key: !secret synchrocast_key
  sensors: meter_phase_2
```

The leader still sends one broadcast. It does not send one copy per follower.

## Cover Satellite

The satellite already has its own local cover with ID `living_room_cover`:

```yaml
synchrocast:
  role: satellite
  group: living_room
  key: !secret synchrocast_key
  covers: living_room_cover
```

The satellite intentionally omits `sensors`, so sensor broadcasts are rejected
before they enter its packet queue. If this satellite also needs the numeric
value, add `sensors: meter_phase_2`.

The leader broadcasts the cover's complete canonical state. If the leader cover
supports tilt but the satellite cover does not, the satellite still applies the
position and ignores only the unsupported tilt field.

## What Must Match

- All devices use the same `group` and `key`.
- Sensor readers list the leader's custom share key.
- The cover uses the same ESPHome ID on the leader and satellite.
- `transport` may be omitted when all devices use the normal platform default.

There are no `publish`, `receive`, `sync_id`, timing, delta, or subscription
options in this setup.
