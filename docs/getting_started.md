# Getting Started

This guide copies one numeric sensor from an ESPHome device to another. The
receiving device gets a normal ESPHome sensor that can appear in Home Assistant
and be used in automations. You do not need to create a template sensor on the
receiver.

> **Current `stage` limitation:** the authenticated Synchrocast protocol is
> working, but its standalone ESP-NOW/UDP backend is not ready yet. Live packets
> currently travel through an already configured ChimeraFX `cfx_sync` transport.
> Without `cfx_sync`, the YAML still validates and the entities are created, but
> no packets cross the network. See
> [Using Synchrocast with ChimeraFX](chimerafx.md) before testing two devices.

## What You Need

- Two ESPHome devices.
- A working ESPHome YAML file for each device.
- A valid `cfx_sync:` configuration on each device for live network transport
  at this development stage.
- One existing sensor on the publishing device. The receiver does not need the
  same hardware.

ESP32 is the preferred target for low-latency ESP-NOW. CFX can also provide UDP
where its own configuration enables it.

## Step 1: Add the Repositories

Add both sources to every device that participates in the live test:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@stage
    refresh: always

  - source: github://effelle/Synchrocast@stage
    refresh: always
```

Adding a repository makes its components available; it does not enable them.
Keep the device's normal `esphome:`, Wi-Fi, and `cfx_sync:` configuration.

`stage` is a development branch. `refresh: always` makes ESPHome check it on
every build. Once stable releases exist, normal installations should pin a
release tag instead.

Do not add a `components:` allow-list unless you know every dependency that must
be included. An incomplete list can hide a required Synchrocast component.

## Step 2: Create a Private Key

Open `secrets.yaml` and add:

```yaml
synchrocast_key: "replace-this-with-your-own-long-passphrase"
```

Use the same value on every Synchrocast device in this group. The minimum is
eight characters; a longer unique passphrase is better. Synchrocast uses it to
authenticate packets and detect modification. Sensor values are **not
encrypted**, so do not use Synchrocast to broadcast secrets over an untrusted
network.

The ChimeraFX key and Synchrocast key belong to separate protocols. They may be
different, and keeping them different is recommended.

## Step 3: Configure the Publishing Device

Assume the first device already has this sensor:

```yaml
sensor:
  - platform: template
    id: phase_2_voltage
    name: "Phase 2 Voltage"
    unit_of_measurement: V
    device_class: voltage
    state_class: measurement
    accuracy_decimals: 1
    update_interval: 10s
    lambda: return 125.7;
```

Add a Synchrocast publisher for it:

```yaml
synchrocast:
  id: power_grid_sync
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

`source` is the local ESPHome sensor. `sync_id` is the stable network name for
this value. Synchrocast observes the final sensor state after ESPHome filters,
then sends the number as a 32-bit floating-point value.

Only `leader` and `satellite` roles may publish observed state. Use `satellite`
when the device both publishes local state and receives other synchronized
values.

## Step 4: Configure the Receiving Device

Add this block to the second device:

```yaml
synchrocast:
  id: power_grid_sync
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

The devices must share:

- `group: power_grid`;
- the same `synchrocast_key`;
- `sync_id: grid.phase_2.voltage`.

The receiver's local `id` and visible `name` do not need to match the source.
Unit, device class, state class, icon, display precision, filters, and
automations are intentionally configured on the receiver because they describe
how that device will use the value.

## Step 5: Enable Focused Test Logs

Add this temporarily to both devices:

```yaml
logger:
  level: VERBOSE
  logs:
    cfx_sync.bus: VERBOSE
    synchrocast: VERBOSE
    synchrocast.dispatcher: VERBOSE
    synchrocast.sensor: VERBOSE
```

At startup, look for `Transport state=attached to cfx_sync`. On the publishing
device, `Broadcast numeric state` confirms that Synchrocast encoded and handed
off the value. On the receiver, `Published remote numeric value` confirms that
the authenticated packet reached the native sensor.

Remove verbose logging after testing. Packet-level logs are intentionally not
needed for normal operation.

## Step 6: Use the Received Sensor

The received entity is a normal ESPHome sensor. For example:

```yaml
interval:
  - interval: 30s
    then:
      - if:
          condition:
            lambda: return id(remote_phase_2_voltage).has_state();
          then:
            - logger.log:
                format: "Remote voltage is %.1f V"
                args: [id(remote_phase_2_voltage).state]
          else:
            - logger.log: "Remote voltage is unavailable"
```

Always check `has_state()` before using a remote numeric value in a lambda. If
the publisher stops refreshing for longer than `stale_after`, Synchrocast marks
the receiver unavailable instead of leaving an old reading looking current.

## What to Read Next

- [Synchronizing Sensor Values](sensors.md) explains numeric, binary, and text
  values, including energy totals and traffic controls.
- [Configuration Reference](configuration.md) lists every accepted option.
- [Troubleshooting and Verbose Logs](troubleshooting.md) explains the most
  useful messages when a value does not arrive.
- [Domains and Capabilities](domains.md) shows the status of actuator domains.
