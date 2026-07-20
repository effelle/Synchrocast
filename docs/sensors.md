# Synchronizing Sensor Values

This guide explains how to copy numeric, on/off, and text information from one
ESPHome device to another. It is written for users who are comfortable editing
an ESPHome YAML file, but it does not assume programming knowledge.

Synchrocast creates a normal ESPHome entity on the receiving device. You do not
need to create a template sensor just to decode the network value. The received
entity can appear in Home Assistant, be displayed on the device, or be used in
ESPHome automations exactly like a locally measured entity.

## What Is Sent

Synchrocast keeps the three value types separate:

| ESPHome type | Network value | Typical examples |
| --- | --- | --- |
| Sensor | One 32-bit number | Voltage, temperature, power, energy total |
| Binary Sensor | `ON`, `OFF`, or unavailable | Door, motion, pump running |
| Text Sensor | UTF-8 text up to 64 bytes, or unavailable | Operating mode, status message |

A numeric value stays numeric. A value such as `125.699997` is the same
single-precision number that ESPHome already uses for sensors. If the receiver
has `accuracy_decimals: 1`, ESPHome displays it as `125.7`; Synchrocast does not
turn it into text or round it on the network.

## The Three Names in an Example

The following names have different jobs:

- `source:` is the existing ESPHome entity on the publishing device.
- `sync_id:` is the permanent network name shared by publisher and receiver.
- `id:` under `receive:` is the new local ESPHome entity on the receiving
  device.

Only `sync_id` must match across devices. Local ESPHome IDs and visible names
may be different.

Choose a descriptive `sync_id` once and keep it stable, for example
`grid.phase_2.voltage`. It may contain lowercase letters, numbers, dots,
underscores, and hyphens. It cannot contain spaces or uppercase letters.

## Before Configuring a Sensor

Add the Synchrocast repository to every participating device:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
```

The current `stage` implementation can exchange live packets by attaching to a
configured ChimeraFX `cfx_sync` transport. If the device already uses
ChimeraFX, add its repository too:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
  - source: github://effelle/ChimeraFX@stage
    refresh: always
```

See [Using Synchrocast with ChimeraFX](chimerafx.md) for the complete shared
transport setup. Without `cfx_sync`, the Synchrocast YAML and entities compile,
but the standalone network backend is still pending.

Store the Synchrocast key in `secrets.yaml`:

```yaml
synchrocast_key: "replace-this-with-a-private-long-passphrase"
```

Every Synchrocast node in the same group must use the same `group` and key.
The key authenticates packets and protects them from undetected modification;
it does not encrypt sensor values. Do not broadcast private information over an
untrusted network.

## Complete Numeric Sensor Example

### Publishing device

Assume this device already has a real voltage sensor with the ID
`phase_2_voltage`:

```yaml
sensor:
  - platform: adc
    pin: GPIO34
    id: phase_2_voltage
    name: "Phase 2 Voltage"
    unit_of_measurement: V
    device_class: voltage
    state_class: measurement
    accuracy_decimals: 1
    filters:
      - multiply: 130.0

synchrocast:
  role: leader
  group: power_grid
  key: !secret synchrocast_key

  sensors:
    publish:
      - source: phase_2_voltage
        sync_id: grid.phase_2.voltage
        min_interval: 1s
        refresh_interval: 30s
        delta: 0.1
```

Synchrocast reads the final value after the source sensor's filters. In this
example the multiplied voltage is sent, not the raw ADC reading.

### Receiving device

The receiving node does not need an ADC sensor or a template sensor:

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key

  sensors:
    receive:
      - sync_id: grid.phase_2.voltage
        id: remote_phase_2_voltage
        name: "Phase 2 Voltage"
        unit_of_measurement: V
        device_class: voltage
        state_class: measurement
        accuracy_decimals: 1
        stale_after: 2min
```

This creates `remote_phase_2_voltage` as an ordinary local ESPHome sensor. Its
unit, device class, state class, icon, accuracy, filters, and automations are
configured locally because those choices describe how this receiving device
will use and display the value.

For example, an automation can be attached directly to the received entity:

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key

  sensors:
    receive:
      - sync_id: grid.phase_2.voltage
        id: remote_phase_2_voltage
        name: "Phase 2 Voltage"
        unit_of_measurement: V
        accuracy_decimals: 1
        stale_after: 2min
        on_value_range:
          - below: 105
            then:
              - logger.log: "Phase 2 voltage is low"
```

In another ESPHome lambda, test `id(remote_phase_2_voltage).has_state()` before
using `id(remote_phase_2_voltage).state`. This avoids treating an unavailable
network value as a real measurement.

## Power and Energy Totals

Power and energy are different quantities:

- Power is an instantaneous measurement, normally expressed in watts.
- Energy is an accumulated total, normally expressed in watt-hours or
  kilowatt-hours.

If the publishing device already calculates a cumulative energy total, send
that absolute total. Do not send only the change since the previous packet.
Absolute totals survive a lost packet: the next successful update contains the
complete current total.

Publisher:

```yaml
synchrocast:
  role: leader
  group: power_grid
  key: !secret synchrocast_key
  sensors:
    publish:
      - source: total_energy
        sync_id: grid.total.energy
        min_interval: 5s
        refresh_interval: 60s
        delta: 0.001
```

Receiver:

```yaml
synchrocast:
  role: follower
  group: power_grid
  key: !secret synchrocast_key
  sensors:
    receive:
      - sync_id: grid.total.energy
        id: remote_total_energy
        name: "Grid Total Energy"
        unit_of_measurement: kWh
        device_class: energy
        state_class: total_increasing
        accuracy_decimals: 3
        stale_after: 3min
```

If you only have a power reading and need to calculate energy, perform the
integration on the publishing device. Integrating again on the receiver can
undercount energy when packets are delayed or lost.

## Binary Sensor Example

Publisher:

```yaml
synchrocast:
  role: satellite
  group: pump_room
  key: !secret synchrocast_key
  binary_sensors:
    publish:
      - source: local_pump_running
        sync_id: pump.running
        min_interval: 100ms
        refresh_interval: 30s
```

Receiver:

```yaml
synchrocast:
  role: follower
  group: pump_room
  key: !secret synchrocast_key
  binary_sensors:
    receive:
      - sync_id: pump.running
        id: remote_pump_running
        name: "Pump Running"
        device_class: running
        stale_after: 2min
        on_press:
          then:
            - logger.log: "The remote pump started"
```

Binary Sensor is for observed `ON`/`OFF` state. A momentary user command, such
as “toggle the fan,” belongs to a controller intent rather than a synchronized
binary state.

## Text Sensor Example

Publisher:

```yaml
synchrocast:
  role: leader
  group: inverter
  key: !secret synchrocast_key
  text_sensors:
    publish:
      - source: inverter_status
        sync_id: inverter.status
        min_interval: 1s
        refresh_interval: 60s
```

Receiver:

```yaml
synchrocast:
  role: follower
  group: inverter
  key: !secret synchrocast_key
  text_sensors:
    receive:
      - sync_id: inverter.status
        id: remote_inverter_status
        name: "Inverter Status"
        icon: mdi:transmission-tower
        stale_after: 3min
        on_value:
          then:
            - logger.log:
                format: "Remote inverter says: %s"
                args: [x.c_str()]
```

Text must be valid UTF-8 and at most 64 bytes. Bytes are not the same as visible
characters: accented letters and many symbols use more than one byte. If a
source exceeds the limit, Synchrocast logs one warning, sends unavailable, and
does not silently cut the message. Text is not split across multiple packets.

## Traffic and Freshness Options

| Option | Where | Default | Purpose |
| --- | --- | --- | --- |
| `min_interval` | `publish` | Numeric `250ms`, binary `50ms`, text `1s` | Fastest allowed transmission rate; newer values replace older pending values. |
| `delta` | Numeric `publish` | `0` | Minimum numeric change that triggers an immediate packet. |
| `refresh_interval` | `publish` | `60s` | Repeats the latest absolute state so a late or recovering receiver catches up. |
| `stale_after` | `receive` | `3min` | Marks the local entity unavailable if no refresh arrives. |

`delta: 0` means every different numeric state can trigger an update, still
limited by `min_interval`. A nonzero delta reduces traffic for noisy sensors.
The latest value is always used for the periodic refresh, even when small
changes did not trigger immediate packets.

Choose `stale_after` comfortably above `refresh_interval`. Two or three refresh
periods is a practical starting point.

## Availability and Restart Behavior

Unavailable is a real state, not the number zero, the word `unknown`, or an
empty substitute:

- A numeric receiver reports no state instead of publishing `0`.
- A binary receiver is invalidated instead of assuming `OFF`.
- A text receiver reports no state; an empty string remains a valid text value.

Each publisher repeats its current state periodically. A receiver that starts
late therefore becomes usable without a manual request. If the publishing node
restarts, its new authenticated boot identity is accepted. Duplicate copies of
the same packet received over both ESP-NOW and UDP are discarded.

## One Authoritative Publisher

Configure only one publisher for each combination of `group`, domain, and
`sync_id`. A receiver locks that value to the first active authenticated
publisher. Packets from a competing publisher are ignored and reported with a
rate-limited warning. After the active publisher becomes stale, a new publisher
can take ownership.

A Synchrocast-created receiver cannot also be configured as a publisher on the
same device. YAML validation rejects that echo loop.

## Common Mistakes

- Publisher and receiver use different `sync_id` values.
- The same `sync_id` is published by two devices.
- A follower is configured to publish; use `role: satellite` for a node that
  both owns local state and participates in synchronization.
- Receiver metadata uses the wrong unit or state class. Metadata is local and
  is not copied over the network.
- `stale_after` is shorter than the publisher's `refresh_interval`.
- A text state is longer than 64 UTF-8 bytes.
- The standalone backend is expected to work without a current `cfx_sync`
  transport. That backend is not implemented on `stage` yet.

Use the focused logs in [Troubleshooting and Verbose Logs](troubleshooting.md)
when checking any of these cases.
