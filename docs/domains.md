# Domains and Capabilities

Synchrocast follows one rule for every actuator:

> The leader broadcasts its complete current state. A receiver applies the
> fields its local device supports and ignores unsupported optional fields.

For example, a cover leader may report position and tilt. A satellite whose
cover has no tilt support still follows the position; it quietly ignores only
the tilt field. One unsupported feature never rejects the rest of a valid
state.

Absolute state is idempotent. Startup requests and periodic recovery refreshes
may repeat the same canonical state, but a receiver does not issue another
hardware command when its supported local fields already match.

The examples below show only the `synchrocast:` block. The named entities, such
as `living_room_cover`, must already exist elsewhere in that device's normal
ESPHome configuration.

## What Works Today

| Domain | YAML option | Current stage status |
| --- | --- | --- |
| Cover | `covers` | Complete canonical state path |
| Fan | `fans` | Complete canonical state path |
| Valve | `valves` | Complete canonical state path |
| Sensor | `sensors` | Numeric leader source and read-only receiver |
| Binary Sensor | `binary_sensors` | On/off leader source and read-only receiver |
| Text Sensor | `text_sensors` | Text leader source and read-only receiver |
| Light | `lights` | Planned; not accepted in YAML yet |
| Climate | `climates` | Planned; not accepted in YAML yet |
| Lock | `locks` | Planned; not accepted in YAML yet |
| Media Player | `media_players` | Planned; not accepted in YAML yet |
| Switch | `switches` | Planned; not accepted in YAML yet |

“Planned” means the domain belongs to Synchrocast's intended scope, but the
current `stage` branch will reject that option. The planned snippets are shown
to explain the lean configuration shape, not as copy-and-paste configuration
for today's build.

## Cover

A Cover leader shares position, optional tilt, and current operation. Open,
close, stop, toggle, and position commands are available to the intent path.
State itself is always absolute, never “toggle,” so nodes cannot drift into
opposite states.

Leader:

```yaml
synchrocast:
  role: leader
  group: living_room
  key: !secret synchrocast_key
  covers: living_room_cover
```

Follower or satellite:

```yaml
synchrocast:
  role: satellite
  group: living_room
  key: !secret synchrocast_key
  covers: living_room_cover
```

Use the same ESPHome entity ID on both devices. A receiver with no position
support can still follow fully open and fully closed states. Intermediate
positions are ignored on that receiver. A receiver without tilt applies the
position and ignores tilt.

## Fan

A Fan leader shares power and every optional feature it supports: speed,
oscillation, direction, and preset. The receiver applies each field
independently.

Leader:

```yaml
synchrocast:
  role: leader
  group: bedroom
  key: !secret synchrocast_key
  fans: bedroom_fan
```

Follower or satellite:

```yaml
synchrocast:
  role: follower
  group: bedroom
  key: !secret synchrocast_key
  fans: bedroom_fan
```

Speed is sent as a percentage and translated to the nearest level supported by
the receiving fan. This lets a three-speed fan follow a five-speed fan without
sharing an impossible raw level. If the follower has no oscillation, direction,
or matching preset support, those fields are ignored while power and speed
continue to work.

## Valve

A Valve leader shares absolute position and current operation. Open, close,
stop, toggle, and position commands are available to the intent path.

Leader:

```yaml
synchrocast:
  role: leader
  group: garden
  key: !secret synchrocast_key
  valves: irrigation_valve
```

Follower or satellite:

```yaml
synchrocast:
  role: satellite
  group: garden
  key: !secret synchrocast_key
  valves: irrigation_valve
```

Position is `0%` closed through `100%` open. A simple valve without position
control can follow fully open and fully closed states; it ignores unsupported
intermediate positions.

## Numeric Sensor

A numeric Sensor is immutable on the network: the leader reads an existing
local sensor and receivers get a native read-only ESPHome sensor. No template
sensor, publish switch, receive switch, or Synchrocast timing option is needed.

Leader—the left side is your custom share key, and the right side is the local
ESPHome sensor ID:

```yaml
synchrocast:
  role: leader
  group: power_grid
  key: !secret synchrocast_key
  sensors:
    meter_phase_2: phase_2_voltage
```

Follower or satellite—list only the share keys this device needs:

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key
  sensors: meter_phase_2
```

The receiver can use `id(meter_phase_2).state` in local automations. A second
follower uses the same one-line `sensors: meter_phase_2` interest. The leader
still sends one broadcast, not one packet per follower.

## Binary Sensor

A Binary Sensor carries `ON`, `OFF`, or unavailable. It is useful for presence,
contacts, alarms, and other facts that receivers should read but never write
back to the source.

Leader:

```yaml
synchrocast:
  role: leader
  group: workshop
  key: !secret synchrocast_key
  binary_sensors:
    pump_running: local_pump_running
```

Follower or satellite:

```yaml
synchrocast:
  role: follower
  group: workshop
  key: !secret synchrocast_key
  binary_sensors: pump_running
```

The receiver gets a read-only entity with ID `pump_running`.

## Text Sensor

A Text Sensor carries a valid UTF-8 string up to 64 bytes, or unavailable. It
is suitable for a status such as `Charging`, `Idle`, or an inverter mode. Text
is never silently cut to fit.

Leader:

```yaml
synchrocast:
  role: leader
  group: solar
  key: !secret synchrocast_key
  text_sensors:
    inverter_status: local_inverter_status
```

Follower or satellite:

```yaml
synchrocast:
  role: follower
  group: solar
  key: !secret synchrocast_key
  text_sensors: inverter_status
```

The receiver reads the value as `id(inverter_status).state`.

## Light — Planned

Light will share power, brightness, supported color channels, color
temperature, and effect state. A monochrome follower will apply power and
brightness while ignoring RGB fields it cannot represent.

Planned shape—do not add this option to the current `stage` build yet:

```yaml
synchrocast:
  role: leader
  group: hallway
  key: !secret synchrocast_key
  lights: hallway_light
```

A brightness of `0%` must remain zero while the light is off. On a later turn-on
with no explicit brightness, ESPHome may choose `100%`; receiving an OFF state
must not perform that change. This follows ESPHome's
[brightness-preservation behavior](https://github.com/esphome/esphome/pull/17103).

## Climate — Planned

Climate will share current canonical state, target temperature, and supported
HVAC mode fields. A receiver will ignore a mode its local climate entity does
not support while applying the compatible fields.

Planned shape—not accepted yet:

```yaml
synchrocast:
  role: leader
  group: upstairs
  key: !secret synchrocast_key
  climates: upstairs_thermostat
```

## Lock — Planned

Lock will use absolute locked or unlocked state. User commands may request lock
or unlock, but synchronized state will never use a relative toggle.

Planned shape—not accepted yet:

```yaml
synchrocast:
  role: leader
  group: front_door
  key: !secret synchrocast_key
  locks: front_door_lock
```

## Media Player — Planned

Media Player will share playback state, volume, and compatible optional fields.
A receiver without a given capability will ignore that field and keep applying
the rest.

Planned shape—not accepted yet:

```yaml
synchrocast:
  role: leader
  group: whole_house_audio
  key: !secret synchrocast_key
  media_players: kitchen_speaker
```

## Switch — Planned

Switch will share absolute on/off state. Toggle remains a user command only;
using toggle as synchronized state could make two devices diverge.

Planned shape—not accepted yet:

```yaml
synchrocast:
  role: leader
  group: utility
  key: !secret synchrocast_key
  switches: circulation_pump
```

## Several Domains in One Group

Put all domains for one group in the same block. Only configured domains are
compiled and reserve their fixed entity tables.

```yaml
synchrocast:
  role: leader
  group: utility_room
  key: !secret synchrocast_key
  fans: ventilation_fan
  valves: water_valve
  sensors:
    room_temperature: local_room_temperature
  binary_sensors:
    leak_detected: local_leak_detected
```

For a complete multi-device example, see
[One Sensor, Two Followers, and One Cover Satellite](example_sensor_cover.md).
