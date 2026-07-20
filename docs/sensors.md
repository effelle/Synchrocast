# Sharing Sensors

Synchrocast treats sensors as immutable, leader-owned values:

- the leader reads and broadcasts the value;
- followers and satellites may read it;
- readers cannot change or reshare it;
- there are no `publish`, `receive`, timing, delta, or stale options.

Numeric Sensors, Binary Sensors, and Text Sensors all follow this model.

## The Share Key

A share key is a short identifier chosen by you. It is not an ESPHome domain,
device class, unit, or predefined Synchrocast category.

```yaml
sensors:
  meter_phase_2: phase_2_voltage
```

In this mapping:

- `meter_phase_2` is the stable share key used by Synchrocast;
- `phase_2_voltage` is the existing local ESPHome sensor on the leader.

The local ESPHome ID may change later without changing the share key. Update
only the right side of the mapping when that happens.

Share keys contain 1-64 lowercase letters, numbers, or underscores. They must
begin with a letter or underscore. A reader uses each share key as the local
ESPHome ID, so keys read by one device must be unique.

## Leader Example

Assume the leader already defines this sensor:

```yaml
sensor:
  - platform: template
    id: phase_2_voltage
    name: "Phase 2 Voltage"
    unit_of_measurement: V
    update_interval: 10s
    lambda: return 125.7f;
```

The Synchrocast block shares it as `meter_phase_2`:

```yaml
synchrocast:
  role: leader
  group: power_grid
  key: !secret synchrocast_key

  sensors:
    meter_phase_2: phase_2_voltage
```

The source sensor remains responsible for its own update interval and filters.
Synchrocast observes the final state produced by that sensor. Synchrocast does
not add a second user-configurable timer or delta filter.

## Two Followers

Both followers use the same share key:

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key

  sensors: meter_phase_2
```

Repeat that block on the second follower. Synchrocast creates a read-only
numeric sensor named `Meter Phase 2` on each device. No Template Sensor is
needed. The share key is also the local ESPHome ID, so device-side automations
can read it directly as `id(meter_phase_2).state`.

For several values, use a list:

```yaml
sensors:
  - meter_phase_1
  - meter_phase_2
  - meter_phase_3
```

## Use the Received Value on the Device

The share key is a real ESPHome ID. For example, this follower checks the
received voltage every 30 seconds and turns on an existing switch named
`voltage_alarm` when the value is above 130:

```yaml
interval:
  - interval: 30s
    then:
      - if:
          condition:
            sensor.in_range:
              id: meter_phase_2
              above: 130
          then:
            - switch.turn_on: voltage_alarm
          else:
            - switch.turn_off: voltage_alarm
```

Home Assistant also sees `Meter Phase 2` as a normal read-only sensor. A
Template Sensor is not required in either case.

The current stage protocol copies the value and availability, not presentation
metadata such as the leader's display name, unit, device class, or state class.
The reader remains fully usable by its share-key ID for device-side calculations.

## Satellites Choose Too

A satellite receives only the sensor share keys it lists. A satellite that
only mirrors a cover does not need a `sensors:` entry:

```yaml
synchrocast:
  role: satellite
  group: power_grid
  key: !secret synchrocast_key
  covers: living_room_cover
```

If it also needs the voltage, add one line:

```yaml
synchrocast:
  role: satellite
  group: power_grid
  key: !secret synchrocast_key
  sensors: meter_phase_2
  covers: living_room_cover
```

Satellites are still read-only for sensor domains. Only a leader may map a
share key to a local sensor source.

## Broadcast Filtering

The leader sends one authenticated broadcast for each shared state, regardless
of how many devices use it. There are no unicast subscriptions or peer lists.

Every reader checks the domain and hashed share key. If the share key is not
listed locally, the packet is discarded before entering the dispatch queue.
This keeps processing and logs quiet while preserving one-to-many broadcast.

The reader list therefore controls local interest; it does not change the
leader's sensor timing.

## Binary Sensors

Leader:

```yaml
synchrocast:
  role: leader
  group: pump_room
  key: !secret synchrocast_key
  binary_sensors:
    pump_running: local_pump_running
```

Follower or satellite:

```yaml
synchrocast:
  role: follower
  group: pump_room
  key: !secret synchrocast_key
  binary_sensors: pump_running
```

## Text Sensors

Leader:

```yaml
synchrocast:
  role: leader
  group: inverter
  key: !secret synchrocast_key
  text_sensors:
    inverter_status: local_inverter_status
```

Follower or satellite:

```yaml
synchrocast:
  role: follower
  group: inverter
  key: !secret synchrocast_key
  text_sensors: inverter_status
```

Text must be valid UTF-8 and no longer than 64 bytes. Oversized or invalid text
is rejected rather than silently truncated.

## Availability and Recovery

Synchrocast broadcasts unavailable when the leader's source has no valid state.
Readers also mark the local entity unavailable when the leader stops
refreshing it. These recovery intervals are internal protocol behavior and are
not YAML settings.

At startup and after network recovery, a follower or satellite broadcasts an
authenticated request for current state. The leader answers by rebroadcasting
the values it already owns. The request is group-wide and contains no sensor
list; each receiving device still keeps only the share keys present in its own
YAML.

Receiving the same available value again refreshes its last-seen time without
publishing a duplicate ESPHome update. The same applies to repeated
unavailability. A leader restart is recognized by its stable device identity,
so its new boot session is accepted immediately. A different device cannot
silently replace the active leader until that leader has been stale for the
fixed recovery window.

A numeric sensor transports its absolute 32-bit floating-point value. Energy
totals should therefore be shared as the complete accumulated total, not as the
change since the previous packet. A later successful packet then repairs any
missed update automatically.

## Common Mistakes

### Reversing the leader mapping

The mapping is always:

```yaml
# share_key: local_esphome_id
sensors:
  meter_phase_2: phase_2_voltage
```

### Giving a follower the local leader ID

Followers list the share key, not the leader's ESPHome ID:

```yaml
sensors: meter_phase_2
```

### Expecting an unlisted sensor

A follower or satellite with no matching entry deliberately ignores that
sensor's broadcasts.

### Using different group names or keys

All participating devices must use the same `group` and `key`. The share key
only identifies the sensor inside that authenticated group.
