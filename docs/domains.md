# Domains and Capabilities

Synchrocast uses one shared vocabulary across several ESPHome domains. The
receiving entity applies only actions and state that make sense for its domain.

## Current Status

| Domain | YAML option | Common behavior | Status |
| --- | --- | --- | --- |
| Light | `lights` | On, off, toggle, brightness, color, color temperature, effects | Planned |
| Cover | `covers` | Open, close, stop, toggle, position | Handler implemented |
| Fan | `fans` | On, off, toggle, speed | Handler implemented |
| Climate | `climates` | Target temperature, HVAC mode, climate state | Planned |
| Lock | `locks` | Lock, unlock, lock state | Planned |
| Media Player | `media_players` | Play, pause, volume, playback state | Planned |
| Valve | `valves` | Open, close, stop, toggle, position | Handler implemented |
| Switch | `switches` | On, off, toggle, switch state | Planned |
| Sensor | `sensors` | Numeric publish and native receive entity | Implemented |
| Binary Sensor | `binary_sensors` | On/off publish and native receive entity | Implemented |
| Text Sensor | `text_sensors` | UTF-8 text publish and native receive entity | Implemented |

"Planned" means the domain belongs to the public Synchrocast scope but is not
ready to configure in the current branch. Cover, Fan, and Valve currently have
receive handlers; automatic outbound state observation for those actuator
domains is still pending.

## Cover

A cover can receive:

- `open`
- `close`
- `stop`
- `toggle` as a user intent
- a position from `0%` closed to `100%` open

```yaml
synchrocast:
  id: garage_sync
  role: follower
  group: garage
  key: !secret synchrocast_key
  covers:
    - garage_door
```

A relative toggle is not accepted as a state broadcast. State synchronization
uses absolute open, closed, or position information so devices cannot drift into
opposite states.

## Fan

A fan can receive on, off, toggle, and speed changes:

```yaml
synchrocast:
  id: bedroom_sync
  role: follower
  group: bedroom
  key: !secret synchrocast_key
  fans:
    - bedroom_fan
```

Fan speed uses the whole speed levels supported by the receiving ESPHome fan. A
three-speed fan accepts levels 1, 2, and 3. Synchrocast rejects fractional or
out-of-range levels instead of silently changing them.

## Valve

A valve can receive open, close, stop, toggle, and position changes:

```yaml
synchrocast:
  id: garden_sync
  role: follower
  group: garden
  key: !secret synchrocast_key
  valves:
    - irrigation_valve
```

Position runs from `0%` closed to `100%` open. The receiving valve still decides
which capabilities it supports.

## Light

Light synchronization is planned for normal ESPHome light state such as power,
brightness, supported color channels, and color temperature. A receiving light
will ignore fields it cannot represent. For example, a monochrome light can
follow power and brightness but cannot reproduce RGB color.

### Brightness zero and a later turn-on

A brightness of `0%` means zero and must be preserved as zero. Synchrocast must
not silently change it to `1%` or `100%` while the light is off. This keeps the
reported state accurate and lets devices such as display backlights use their
real minimum level.

There is one separate rule when the light is turned on again:

- If the ON command includes a brightness, that brightness is used.
- If the ON command does not include a brightness and the saved brightness is
  `0%`, ESPHome starts the light at `100%`.
- Receiving or reporting an OFF state does not perform that `100%` change.

This follows ESPHome's
[brightness-preservation behavior](https://github.com/esphome/esphome/pull/17103).
The future Light handler and its simulations must keep the OFF state and the
later ON action as two distinct events.

## Climate

Climate synchronization is planned for target temperature, operating mode, and
the state fields supported by the receiving climate entity. The final handler
must avoid applying unsupported HVAC modes.

## Lock

Lock synchronization is planned for lock and unlock intents plus absolute lock
state. Security-sensitive state will use absolute broadcasts rather than
relative toggles.

## Media Player

Media Player synchronization is planned for play, pause, volume, and supported
playback state. Devices will apply only capabilities exposed by their ESPHome
media player.

## Switch

Switch synchronization is planned for on, off, toggle intents, and absolute
on/off state. Toggle is appropriate for a user command; synchronized state uses
an absolute value.

## Sensor, Binary Sensor, and Text Sensor

Sensors report state and do not accept actuator commands.

- Sensor carries one finite 32-bit numeric value.
- Binary Sensor carries `ON`, `OFF`, or unavailable.
- Text Sensor carries valid UTF-8 text up to 64 bytes, or unavailable.

The publisher watches an existing ESPHome entity. The receiver is created by
Synchrocast as a native ESPHome entity, so the destination does not need a
template just to unpack a network value. Only the explicit `sync_id` must match
between devices; units, names, display precision, filters, and automations are
configured locally.

```yaml
synchrocast:
  role: follower
  group: utility_room
  key: !secret synchrocast_key
  sensors:
    receive:
      - sync_id: utility.voltage
        id: remote_voltage
        name: "Remote Voltage"
        unit_of_measurement: V
        device_class: voltage
        state_class: measurement
        stale_after: 2min
```

See [Synchronizing Sensor Values](sensors.md) for complete numeric, binary, and
text publisher/receiver examples.

## One Device with Several Domains

A device can participate in several domains at the same time:

```yaml
synchrocast:
  id: utility_room_sync
  role: satellite
  group: utility_room
  key: !secret synchrocast_key
  fans:
    - ventilation_fan
  valves:
    - water_valve
  sensors:
    publish:
      - source: room_temperature
        sync_id: utility_room.temperature
```

Only configured domains should create handlers and reserve their entity tables.
This keeps small devices from paying the memory cost of unused domains.

An observational handler uses fixed-capacity publisher and receiver tables.
Text Sensor support also enables the larger 64-byte application payload; builds
without text sensors retain the smaller packet and dispatcher queue footprint.
