# Getting Started

This guide shares one existing numeric sensor from an ESPHome leader to a
read-only follower. Synchrocast is complete on its own; ChimeraFX is not
required.

## 1. Add the Repository

Add this to every participating ESPHome file:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    components: [synchrocast]
    refresh: always
```

Adding the repository only makes the component available. The device joins a
group only when its YAML contains a `synchrocast:` block.

## 2. Add the Shared Secret

Put the same secret in every device's `secrets.yaml`:

```yaml
synchrocast_key: "replace-with-the-same-long-random-key"
```

Do not publish the real value in a public repository.

## 3. Configure the Leader

Assume the leader already has this ESPHome sensor:

```yaml
sensor:
  - platform: template
    id: phase_2_voltage
    name: "Phase 2 Voltage"
    unit_of_measurement: V
    update_interval: 10s
    lambda: return 125.7f;
```

Share it under a custom key:

```yaml
synchrocast:
  role: leader
  group: power_grid
  key: !secret synchrocast_key

  sensors:
    meter_phase_2: phase_2_voltage
```

The mapping reads `share key: local ESPHome ID`.

`meter_phase_2` is an arbitrary Synchrocast identifier. It is not a voltage
domain, device class, or unit. `phase_2_voltage` is the local sensor that
already exists on this leader.

Synchrocast uses the source sensor's normal ESPHome update behavior and final
filtered state. There are no additional publish, receive, timing, or delta
options.

## 4. Configure the Follower

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key
  sensors: meter_phase_2
```

Synchrocast creates a read-only sensor named `Meter Phase 2`. No Template
Sensor is needed on the follower. Its local ESPHome ID is the share key, so a
device automation can read `id(meter_phase_2).state` directly.

A second follower uses the same block. A device that does not list
`meter_phase_2` ignores that sensor's authenticated broadcasts.

## 5. Transport Defaults

You normally omit `transport`:

- ESP32 uses ESP-NOW.
- ESP8266 uses UDP.
- A mixed ESP32/ESP8266 group must explicitly use `transport: udp` on every
  member.

Standalone UDP uses port `39581` by default.

Standalone ESP-NOW recovery is automatic. If the router disappears,
Synchrocast waits through a short grace period, uses internal fallback channel
6, and rearms when Wi-Fi returns or changes channel. The leader then refreshes
its latest state. There is no recovery YAML to maintain.

A follower or satellite also asks for the current group state when it starts
and after transport recovery. The request is an authenticated broadcast; the
leader answers with its latest absolute values and each receiver keeps only the
entities listed in its own configuration. You do not add a polling interval,
request switch, or destination address.

## 6. Check the Logs

Temporarily enable focused verbose logs:

```yaml
logger:
  level: VERBOSE
  logs:
    synchrocast: VERBOSE
    synchrocast.transport: VERBOSE
    synchrocast.dispatcher: VERBOSE
    synchrocast.sensor: VERBOSE
```

At startup, look for `Transport state=standalone active`. On the leader,
`Broadcast numeric state` confirms transmission. On the follower,
`Published remote numeric value` confirms that a matching authenticated
broadcast reached the read-only sensor.

With verbose logs, the follower also reports `STATE_REQUEST sent` and the
leader reports `Processed STATE_REQUEST`. If the received value already matches
the local value, `Refreshed unchanged` is normal: liveness was renewed without
emitting another ESPHome state change.

The dispatcher statistics include `filtered`. That counter increases when the
device receives a valid state broadcast for a domain or share key it did not
configure.

Remove verbose logging after testing.

## 7. Add More Sensors

Leader:

```yaml
sensors:
  meter_voltage: local_voltage
  meter_current: local_current
  meter_power: local_power
```

Follower interested in only voltage and power:

```yaml
sensors:
  - meter_voltage
  - meter_power
```

The leader still sends one broadcast per state. Receivers perform their own
small, fixed-capacity interest check and discard unwanted values before
queueing them.

## Next Steps

- [Four-device sensor and cover example](example_sensor_cover.md)
- [Complete sensor guide](sensors.md)
- [Examples and behavior for every domain](domains.md)
- [Configuration reference](configuration.md)
- [Troubleshooting](troubleshooting.md)
- [Using Synchrocast with ChimeraFX](chimerafx.md)
