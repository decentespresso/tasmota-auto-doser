# Tasmota Auto Doser Firmware

Decent Espresso firmware for controlling a grinder or auto-doser plug from an HDS scale over a small local TCP protocol.

This is not a general Tasmota distribution. Official Tasmota binaries do not include the grinder TCP driver.

[![Release](https://img.shields.io/github/v/release/decentespresso/tasmota-auto-doser)](https://github.com/decentespresso/tasmota-auto-doser/releases/latest)
[![Release build](https://github.com/decentespresso/tasmota-auto-doser/actions/workflows/grinder_release.yml/badge.svg)](https://github.com/decentespresso/tasmota-auto-doser/actions/workflows/grinder_release.yml)
[![License](https://img.shields.io/github/license/decentespresso/tasmota-auto-doser.svg)](LICENSE.txt)

## Quick Install

Download firmware from the [latest GitHub Release](https://github.com/decentespresso/tasmota-auto-doser/releases/latest).

Use:

- `tasmota32-nous-a6t-grinder.bin` for the tested NOUS A6T plug.
- `tasmota32-grinder.bin` for validated classic ESP32 plugs with exactly one normal `Relay1`.

Before flashing, disconnect the grinder or use a harmless load. Back up the current Tasmota configuration, then upload the `.bin` file through the Tasmota web UI firmware upgrade page.

Do not use official Tasmota OTA binaries for grinder control. They do not contain this TCP driver.

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

This is intended only for a trusted local WLAN. The plug MAC is an identity label, not authentication.

## HDS Scale Setup

On the scale:

1. Put the HDS and plug on the same Wi-Fi network.
2. Open the HDS setup menu and enter `Grinder Plug`.
3. Enable grinder mode.
4. Use `Select Plug` and choose the plug MAC shown by discovery.
5. Set `Target g`, `Safety g`, and `Zero Range`.
6. Dry-test with no grinder load before connecting the actual grinder.

The HDS stores the selected plug by MAC address and verifies the plug MAC returned by the TCP protocol before using it. The plug MAC is visible on the Tasmota web UI status pages.

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
