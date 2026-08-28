# Tasmota Grind by Weight Firmware

Decent Espresso firmware for controlling a grinder with a Tasmota Wi-Fi plug from an HDS scale over a small local TCP protocol.

This is not a general Tasmota distribution. Official Tasmota binaries do not include the grinder TCP driver.

Latest firmware: [GitHub Releases](https://github.com/decentespresso/tasmota-auto-doser/releases/latest). License: [GPL-3.0-only](LICENSE.txt).

## Quick Install

Download firmware from the [latest GitHub Release](https://github.com/decentespresso/tasmota-auto-doser/releases/latest).

Use:

- `tasmota32-nous-a6t-grinder.bin` for the tested NOUS A6T plug.
- `tasmota32-grinder.bin` for validated classic ESP32 plugs with exactly one normal `Relay1`.

Before flashing, disconnect the grinder or use a harmless load. Back up the current Tasmota configuration, then upload the `.bin` file through the Tasmota web UI firmware upgrade page.

Do not use official Tasmota OTA binaries for grinder control. They do not contain this TCP driver.

You also need HDS firmware with grinder support. The plug firmware only switches power; the HDS scale decides when to start and stop by weight.

## Supported Devices

Supported now:

- NOUS A6T, tested with the `tasmota32-nous-a6t-grinder` build.

Expected candidates:

- Classic ESP32 Tasmota plugs/modules with exactly one normal `Relay1`.
- The Tasmota template must not use `Relay2`, `Relay_b`, dimmer, shutter, Tuya MCU, serial relay, or other custom actuator drivers.

Not supported by the current builds:

- ESP8266.
- ESP32-C3, ESP32-S2, ESP32-S3, ESP32-C6.
- Multi-relay, bistable relay, dimmer, shutter, and vendor-MCU relay devices.

See [docs/grinder-tcp.md](docs/grinder-tcp.md) for the device evidence table and smoke tests.

## How It Works

The firmware adds a native TCP service on port `31980`. One scale connects, says `HELLO`, keeps the connection alive, and controls only `Power1`.

The relay is off by default and may only turn on while one TCP client owns the plug. The firmware forces `Power1 OFF` on boot, Wi-Fi loss, TCP disconnect, heartbeat timeout, invalid input, `BYE`, `OFF`, duplicate `OFF`, and unsupported relay layout.

Non-TCP `Power1 ON` attempts from the web UI, HTTP API, MQTT, buttons, rules, timers, device groups, or retained state are blocked or immediately forced off.

On the HDS side, grinder mode starts only after the empty cup is stable, stops at cutoff, waits for cup removal before rearming, learns adaptive safety from valid shots, and keeps weighing responsive when the plug is offline.

This is intended only for a trusted local WLAN. The plug MAC is an identity label, not authentication.

## HDS Scale Setup

First safe dry run:

1. Flash the plug and leave the grinder disconnected.
2. Confirm the Tasmota web UI shows `Power1 OFF`.
3. Put the HDS and plug on the same Wi-Fi network.
4. Open the HDS setup menu and enter `Grinder Plug`.
5. Enable grinder mode.
6. Use `Select Plug` and choose the plug MAC shown by discovery.
7. Set `Target g` , `Safety g`, and `Zero Range`.
8. Dry-test with no grinder load (Target - Safety g is the stop point of the plug).
9. Connect the grinder only after the dry test passes.

The HDS stores the selected plug by MAC address and verifies the plug MAC returned by the TCP protocol before using it. The plug MAC is visible on the Tasmota web UI status pages.

Default HDS settings: `Target 15.0 g`, `Safety 2.0 g`, `Zero range -1.0 g to 1.0 g`, `Zero hold 1000 ms`.

Full setup notes are in [docs/grinder-tcp.md](docs/grinder-tcp.md).

## Versioning

Public project releases use semantic versions, starting with `v1.0.0`.

The firmware still reports the upstream Tasmota base version plus the image name, for example `15.5.0.1(nous-a6t-grinder)`. That means the build is based on Tasmota `15.5.0.1`; the public release version is the GitHub release tag.

## Build From Source

```powershell
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
pio run -e tasmota32-nous-a6t-grinder
pio run -e tasmota32-grinder
```

The generated OTA files are written under `build_output/firmware`.

## Release Workflow

Maintainer releases are created from tags named like:

```text
v1.0.0
```

The release workflow runs host protocol tests, builds the two grinder firmware images, uploads CI artifacts, and attaches only web-upload OTA `.bin` files plus `SHA256SUMS.txt` to the GitHub Release.

See [docs/releasing.md](docs/releasing.md).

## Upstream Tasmota

This project is based on [arendst/Tasmota](https://github.com/arendst/Tasmota). For general Tasmota documentation, templates, commands, and migration notes, use the upstream docs:

- [Tasmota documentation](https://tasmota.github.io/docs)
- [Tasmota device templates](https://templates.blakadder.com)

## License

GPL-3.0-only. See [LICENSE.txt](LICENSE.txt).
