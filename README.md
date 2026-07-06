# Tasmota Grinder TCP Firmware

Private Tasmota fork for controlling an espresso grinder plug from an HDS scale over a small local TCP protocol.

This is not a general Tasmota distribution. Official Tasmota binaries do not include the grinder TCP driver.

[![Grinder release](https://img.shields.io/github/v/release/ODevStudio/tasmota-nous-a6t-grinder?filter=grinder-v*)](https://github.com/ODevStudio/tasmota-nous-a6t-grinder/releases/latest)
[![Grinder release build](https://github.com/ODevStudio/tasmota-nous-a6t-grinder/actions/workflows/grinder_release.yml/badge.svg)](https://github.com/ODevStudio/tasmota-nous-a6t-grinder/actions/workflows/grinder_release.yml)
[![License](https://img.shields.io/github/license/ODevStudio/tasmota-nous-a6t-grinder.svg)](LICENSE.txt)

## Quick Install

Download firmware from this fork's [latest GitHub Release](https://github.com/ODevStudio/tasmota-nous-a6t-grinder/releases/latest).

Use:

- `tasmota32-nous-a6t-grinder.bin` for the tested NOUS A6T plug.
- `tasmota32-grinder.bin` for validated classic ESP32 plugs with exactly one normal `Relay1`.

Upload the `.bin` file through the Tasmota web UI firmware upgrade page.

Do not upload `.factory.bin` through the web UI. Factory images are for serial flashing or recovery only.

## Supported Devices

Supported now:

- NOUS A6T, tested with the `tasmota32-nous-a6t-grinder` build.

Expected but not hardware-tested here:

- Classic ESP32 Tasmota plugs/modules with exactly one normal `Relay1`.
- The Tasmota template must not use `Relay2`, `Relay_b`, dimmer, shutter, Tuya MCU, serial relay, or other custom actuator drivers.

Not supported by the current builds:

- ESP8266.
- ESP32-C3, ESP32-S2, ESP32-S3, ESP32-C6.
- Multi-relay, bistable relay, dimmer, shutter, and vendor-MCU relay devices.

See [docs/grinder-tcp.md](docs/grinder-tcp.md) for the current device evidence table and smoke tests.

## Safety Contract

The grinder relay is off by default and may only turn on while one TCP client owns the plug.

The firmware forces `Power1 OFF` on boot, Wi-Fi loss, TCP disconnect, heartbeat timeout, invalid input, `BYE`, `OFF`, duplicate `OFF`, and unsupported relay layout.

Non-TCP `Power1 ON` attempts from the web UI, HTTP API, MQTT, buttons, rules, timers, device groups, or retained state are blocked or immediately forced off.

This is intended only for a trusted local WLAN. The plug MAC is an identity label, not authentication.

## HDS Scale Setup

On the scale:

1. Put the HDS and plug on the same Wi-Fi network.
2. Open the HDS setup menu and enter `Grinder Plug`.
3. Select `Grinder On`.
4. Use `Select Plug` and choose the plug MAC shown by discovery.
5. Set `Target g`, `Safety g`, and `Zero Range`.
6. Dry-test with no grinder load before connecting the actual grinder.

The HDS stores the selected plug by MAC address and verifies the plug MAC returned by the TCP protocol before using it. The plug MAC is visible on the Tasmota web UI status pages.

Full setup notes are in [docs/grinder-tcp.md](docs/grinder-tcp.md).

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

Releases are created from tags named like:

```text
grinder-v2026.07.06.2
```

The grinder release workflow runs host protocol tests, builds the two grinder firmware images, uploads CI artifacts, and attaches the `.bin` and `.factory.bin` files to the GitHub Release.

## Upstream Tasmota

This fork is based on [arendst/Tasmota](https://github.com/arendst/Tasmota). For general Tasmota documentation, templates, commands, and migration notes, use the upstream docs:

- [Tasmota documentation](https://tasmota.github.io/docs)
- [Tasmota device templates](https://templates.blakadder.com)
- [Upstream Tasmota releases](https://github.com/arendst/Tasmota/releases)

## License

GPL-3.0-only. See [LICENSE.txt](LICENSE.txt).
