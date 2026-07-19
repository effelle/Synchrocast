# Getting Started

This guide shows the shortest path to a first Synchrocast test: one leader, one
follower, and one cover shared between them.

> **Before you start:** the examples define the planned Step 3 YAML interface.
> The current skeleton does not contain that YAML layer yet. Keep these examples
> as the public configuration contract while development continues.

## What You Need

- Two ESPHome devices.
- A working ESPHome YAML file for each device.
- Both devices connected to the same Wi-Fi network for setup and management.
- One local ESPHome entity on each device that represents the same logical
  entity. The first example uses a cover.

ESP32 is the preferred target for the lowest-latency ESP-NOW transport. ESP8266
uses the UDP fallback.

## Step 1: Add the Repository

Add this block to every device that will use Synchrocast:

```yaml
external_components:
  - source: github://effelle/Synchrocast@stage
    refresh: always
```

ESPHome will download Synchrocast from
[GitHub](https://github.com/effelle/Synchrocast) when it prepares the device.

The project currently targets `stage`. Once stable releases exist, normal users
should pin a release tag instead of following the development branch.

`refresh: always` is useful during development because ESPHome checks for new
files on every build. Remove it or pin a release after your setup is stable.

Do not add a `components:` allow-list for the normal GitHub installation. An
incomplete allow-list can hide a required Synchrocast domain from ESPHome.

## Step 2: Create the Shared Key

Open `secrets.yaml` and add:

```yaml
synchrocast_key: "replace-this-with-your-own-long-passphrase"
```

Use the same key on every device in the group. Choose a private passphrase with
at least eight characters. You do not need to generate a hexadecimal key.

## Step 3: Choose the Leader

The leader owns the main state. Add the following to the first device:

```yaml
cover:
  - platform: template
    name: "Garage Door"
    id: garage_door
    optimistic: true
    has_position: true

synchrocast:
  id: garage_sync
  role: leader
  group: garage
  key: !secret synchrocast_key
  covers:
    - garage_door
```

Your real device can use any normal ESPHome cover platform. The template cover
keeps this first example easy to read.

## Step 4: Add the Follower

On the second device, add:

```yaml
cover:
  - platform: template
    name: "Garage Door Follower"
    id: garage_door
    optimistic: true
    has_position: true

synchrocast:
  id: garage_sync
  role: follower
  group: garage
  key: !secret synchrocast_key
  covers:
    - garage_door
```

The devices must share these values:

- `group: garage`
- the same `synchrocast_key`
- `id: garage_door` for the matching entity

ESPHome IDs are local to each device, so both YAML files can safely use the same
ID. The visible names can be different; Synchrocast matches the YAML ID.

## Step 5: Enable Useful Test Logs

During early testing, add this logger configuration to both devices:

```yaml
logger:
  level: VERBOSE
  logs:
    synchrocast.dispatcher: VERBOSE
    synchrocast.cover: VERBOSE
```

The dispatcher log shows the packet type, domain, entity identity, intent,
payload size, and remaining queue depth. The cover log confirms the action that
was applied.

Remove the verbose level after testing. Normal operation is intentionally quiet.

## What Should Happen

After both devices start:

- opening the leader opens the follower;
- closing the leader closes the follower;
- stopping the leader stops the follower;
- changing the leader position changes the follower position;
- a normal intent can toggle the target;
- a relative toggle is not accepted as a state broadcast, because applying it
  to devices with different starting states could make them diverge.

## Next Steps

- Read [Configuration Reference](configuration.md) to add other roles or groups.
- Read [Domains and Capabilities](domains.md) before adding another domain.
- Use [Troubleshooting and Verbose Logs](troubleshooting.md) when a device does
  not react as expected.
