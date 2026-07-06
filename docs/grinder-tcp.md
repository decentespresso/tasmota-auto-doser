# Grinder TCP Firmware

This fork adds a native Tasmota driver for grinder control from a scale over a local TCP connection. It is meant for trusted LAN use only. The plug MAC is an identity label, not authentication.

## Supported Firmware Builds

| Build env | Image label | Target |
| --- | --- | --- |
| `tasmota32-nous-a6t-grinder` | `nous-a6t-grinder` | Tested NOUS A6T compatibility build |
| `tasmota32-grinder` | `grinder-tcp32` | Generic classic ESP32 single-relay build |

Both builds enable `USE_GRINDER_TCP`, listen on TCP port `31980`, advertise `_grinderplug._tcp.local`, and keep Web UI/OTA available.

The generic build is for classic ESP32 Tasmota devices only. It is not for ESP8266, ESP32-C3, ESP32-S2, ESP32-S3, ESP32-C6, dimmers, shutters, multi-relay devices, or bistable relay devices. Those chips need their own Tasmota binary families, and unsupported relay layouts are rejected at runtime.

## Hardware Support

The driver starts only when Tasmota reports:

- exactly one device relay: `TasmotaGlobal.devices_present == 1`
- one normal `GPIO_REL1`
- no second `GPIO_REL1`
- no bistable relay mode

This keeps templates out of the firmware. Configure the correct Tasmota template for the plug model, then the runtime guard decides whether the grinder TCP service may start.

The generic image supports classic ESP32 single normal-`Relay1` templates. Devices that need Tuya MCU, dimmer, shutter, bistable, serial, or vendor-specific relay drivers are outside this firmware's safety contract until validated separately.

Current support status:

| Device | Status | Notes |
| --- | --- | --- |
| NOUS A6T | Tested target | Classic ESP32, Tasmota pre-installed, `Relay 1` on GPIO13 |
| NOUS A8T | Expected candidate | Same single-relay ESP32 layout class as A6T; not hardware-tested here |
| Shelly Plus Plug S | Expected candidate | Classic ESP32 with one `Relay 1`; some stock firmware versions cannot OTA-flash to Tasmota |
| Sonoff POWR316 / POWR316D | Expected candidate | Classic ESP32 with one `Relay 1`; usually serial flashing, inline module form factor |
| Sonoff POWR320D | Not supported | Uses bistable `Relay_b` outputs; rejected by the guard |

Before buying or flashing another device, verify its Tasmota template shows one normal `Relay 1` and no `Relay 2`, `Relay_b`, dimmer, shutter, or special actuator output.

References:

- [Tasmota ESP32 docs](https://tasmota.github.io/docs/ESP32/)
- [Blakadder ESP32 device list](https://templates.blakadder.com/esp32.html)
- [NOUS A6T template](https://templates.blakadder.com/nous_A6T.html)
- [NOUS A8T template](https://templates.blakadder.com/nous_A8T.html)
- [Shelly Plus Plug S template](https://templates.blakadder.com/shelly_plus_plug_S.html)
- [Sonoff POWR316 template](https://templates.blakadder.com/sonoff_POWR316.html)
- [Sonoff POWR316D template](https://templates.blakadder.com/sonoff_POWR316D.html)
- [Sonoff POWR320D template](https://templates.blakadder.com/sonoff_POWR320D.html)

## Safety Behavior

On boot and while running, the firmware keeps `Power1` off unless the active TCP client has explicitly authorized it with `ON`.

The driver forces `Power1 OFF` on:

- boot/init
- Wi-Fi down
- TCP client disconnect
- heartbeat timeout
- invalid protocol input
- `BYE`
- `OFF`
- duplicate `OFF`
- unsupported relay layout

It also blocks or immediately cancels non-TCP `Power1 ON` attempts from Web UI, HTTP API, MQTT, buttons, rules, timers, device groups, or retained state. External `OFF` clears TCP ownership.

The firmware applies quiet defaults for grinder use: MQTT publish/control, Home Assistant discovery, timers, rules, emulation, device groups, MI32 BLE, Matter, Wizmote, and Berry autoexec are disabled. mDNS is enabled. These are persistent Tasmota settings, so do not flash this profile onto a plug that should still be a general automation device.

## TCP Protocol

Clients connect to port `31980`. Commands are ASCII lines ending in `\n` or `\r\n`.

Commands:

```text
HELLO <scale_mac>
PING
ON
OFF
!
STATE
BYE
```

`!` is a one-byte fast-off command. It has the same relay behavior as `OFF`.

Responses:

```text
OK <plug_mac> state=OFF
OK <plug_mac> state=ON
BUSY <plug_mac>
ERR <plug_mac> reason=<token>
```

MAC strings must be uppercase colon-separated hex, for example `1C:69:20:0B:54:20`.

Only one pending or active TCP client is allowed. A second client receives `BUSY <plug_mac>` and is closed without changing the relay. Commands before `HELLO`, duplicate `HELLO`, extra arguments, bad MACs, invalid characters, embedded carriage returns, and line overflow return `ERR` where possible, then close fail-safe with the relay off.

The scale must verify the returned `plug_mac` against its selected plug MAC before using the plug.

## Discovery

The scale stores and shows the plug MAC address as the human-readable plug name, for example `1C:69:20:0B:54:20`. You can view the plug MAC on the Tasmota web UI main page or status pages before selecting it on the scale.

The plug advertises:

```text
_grinderplug._tcp.local
```

TXT fields:

```text
mac=<plug_mac>
name=<hostname>
model=<build_model>
proto=1
```

Multiple plugs share the service type. Their mDNS instance names are unique through the Tasmota hostname, and the scale must select by MAC.

## Build

For normal flashing, download a release asset from this fork's GitHub Releases. Use `tasmota32-nous-a6t-grinder.bin` for NOUS A6T and `tasmota32-grinder.bin` for validated generic classic ESP32 single-relay plugs.

```powershell
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
pio run -e tasmota32-nous-a6t-grinder
pio run -e tasmota32-grinder
```

Use the generated OTA `.bin` from `build_output/firmware` for web upload. Do not upload `.factory.bin` through the Tasmota web UI; factory images are for serial flashing or recovery.

## Smoke Tests

With no grinder connected:

```powershell
python tools/grinder_mdns_probe.py --expected-mac 1C:69:20:0B:54:20 --expected-model NOUS_A6T --target-ip 192.168.178.30
powershell -ExecutionPolicy Bypass -File tools/grinder_plug_smoke_test.ps1 -Ip 192.168.178.30 -ExpectedMac 1C:69:20:0B:54:20
```

Expected coverage:

- relay starts and stays off
- HTTP `Power1 ON` does not keep the relay on
- bad `HELLO` is rejected safe
- first TCP client wins and second receives `BUSY`
- `ON`, `OFF`, `!`, `STATE`, duplicate `OFF`, and `BYE` return expected responses
- dropped client and missed heartbeat force `Power1 OFF`
