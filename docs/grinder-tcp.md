# Grinder TCP Firmware

This project adds a native Tasmota driver for grinder control from an HDS scale over a local TCP connection. It is meant for trusted LAN use only. The plug MAC is an identity label, not authentication.

## Supported Firmware Builds

| Build env | Image label | Target |
| --- | --- | --- |
| `tasmota32-nous-a6t-grinder` | `nous-a6t-grinder` | Tested NOUS A6T compatibility build |
| `tasmota32-grinder` | `grinder-tcp32` | Generic classic ESP32 single-relay build |

Both builds enable `USE_GRINDER_TCP`, listen on TCP port `31980`, advertise `_grinderplug._tcp.local`, and keep Web UI file upload updates available.

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

## HDS Scale Setup

Before setup, flash the plug, leave the grinder disconnected, and confirm the Tasmota web UI shows `Power1 OFF`.

The HDS grinder mode lets the scale switch the grinder plug on and off. The grinder itself still needs to be ready to run. This feature controls plug power, not the physical grinder switch.

The scale:

- starts the plug only after the empty cup is stable
- stops the plug when the dose reaches the cutoff
- waits for cup removal before preparing the next dose
- learns a better safety value from normal shots over time
- keeps normal weighing responsive when the plug is offline
- enters a fail-safe error if the plug connection is lost while grinding

On the HDS scale:

1. Flash HDS firmware with grinder support.
2. Put the scale and plug on the same Wi-Fi network.
3. Open the HDS OLED menu.
4. Open `Grinder Plug`.
5. Select `Grinder On`.
6. Select `Select Plug`.
7. Choose the plug by MAC address, for example `1C:69:20:0B:54:20`.
8. Set `Target g`, `Safety g`, and `Zero Range`.
9. Leave the menu with `Back`.
10. Run a dry cycle with no grinder load.
11. Connect the grinder only after dry tests pass.

The scale stores the selected plug MAC, not the IP address. mDNS, hostname, and the cached IP address are only used to find the plug again.

`Grinder On` forces Wi-Fi on boot. From the normal weight view, hold both buttons for 500 ms to open the `Grinder Plug` menu when grinder mode is on. Use `Target g` there for quick dose changes.

Default HDS settings:

```text
Grinder: Off
Target: 15.0 g
Safety: 0.2 g
Zero range: -1.0 g to 1.0 g
Zero hold: 1000 ms
Target tolerance: 0.5 g
```

The HDS cutoff threshold is:

```text
target grams - safety grams
```

The current HDS firmware does not use latency compensation. Adaptive safety is the early-stop compensation.

Normal use:

1. Turn grinder mode on and select a plug.
2. Put the empty dosing cup on the scale.
3. Tare the scale.
4. Wait until the cup is stable inside the zero range.
5. The scale sends `ON` to the plug.
6. Start the grinder physically if needed.
7. The scale sends fast OFF (`!`) when cutoff is reached.
8. Remove the filled cup.
9. Put the empty cup back.
10. The scale rearms after the zero hold time.

Cutoff is blocked until all of these are true:

- tare is not pending
- weight has left zero range
- 1500 ms passed since leaving zero range
- a real grind pattern was confirmed
- weight is at or above `target - safety`
- the selected plug connection is still valid

If a cup or setup mass is placed on the scale before tare, the scale shows `tare cup` and blocks cutoff until the user tares or the weight returns to zero range.

After a valid grind, the scale compares final weight to target and adjusts safety for later shots. The adaptive value averages the last three valid recommendations and saves before deep sleep instead of on every loop.

Safety learning is skipped when the shot does not look like a normal grind: final weight never stabilizes, the cup is removed too early, weight drops after OFF, the grind stalls below target, average grind rate is too high, or the final result is too far from target.

During dosing, the scale keeps one TCP connection open, sends heartbeat `PING` messages, sends `ON` only while armed, and sends `OFF` or `!` at cutoff. If the TCP connection is lost while grinding, the scale enters an error state and the plug firmware fails safe to `Power1 OFF`.

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

## HDS Troubleshooting

If `Select Plug` finds nothing, check mDNS from another machine:

```powershell
dns-sd -B _grinderplug._tcp local
dns-sd -L "INSTANCE NAME HERE" _grinderplug._tcp local
```

Expected TXT data:

```text
port=31980
mac=<plug_mac>
model=<build_model>
proto=1
```

If `_http._tcp` appears but `_grinderplug._tcp` does not, the plug service advertisement is wrong.

If the scale shows `plug wait`, the selected plug cannot be reached. Check that the plug is powered, on the same WLAN, listening on port `31980`, and still using the saved hostname or cached IP. If the IP changed and hostname lookup does not recover it, run `Select Plug` again.

If the scale shows `busy`, another scale or client is already connected. Disconnect that client or restart the plug.

If the scale shows `wrong mac`, the TCP server answered with a MAC different from the selected plug MAC. The cached IP probably points to another device. Run `Select Plug` again.

If the scale shows `grinder error`, it entered fail-safe mode. Common causes are lost plug connection while grinding, malformed TCP response, wrong plug MAC, plug `ERR`, `ON` timeout, or `OFF` timeout. The plug should turn `Power1 OFF` when the active TCP connection drops.

If weighing must stay responsive while the plug is offline, do not run full discovery in the background. Runtime lookup should use only the saved IP and saved hostname with short timeouts. Full mDNS discovery belongs in manual `Select Plug`.

If the scale does not rearm, remove the filled cup, return the empty cup, and wait until weight is stable inside zero range for the zero hold time. The default zero hold is 1000 ms.

USB serial grinder logs use these prefixes:

```text
[grinder] connect
[grinder] tx HELLO
[grinder] rx OK
[grinder] cutoff
[grinder] safety learn
[grinder] error
```

## Build

For normal flashing, download a release asset from [GitHub Releases](https://github.com/decentespresso/tasmota-auto-doser/releases/latest). Use `tasmota32-nous-a6t-grinder.bin` for NOUS A6T and `tasmota32-grinder.bin` for validated generic classic ESP32 single-relay plugs.

```powershell
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
pio run -e tasmota32-nous-a6t-grinder
pio run -e tasmota32-grinder
```

Use the generated OTA `.bin` from `build_output/firmware` for web upload. Factory images are not published in normal releases; maintainers can build them from source for serial recovery.

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
