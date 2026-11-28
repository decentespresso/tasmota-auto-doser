# Grinder BLE

The BLE grinder builds replace TCP grinder control. A firmware upgrade preserves the plug's Wi-Fi settings, GPIO template and power-meter calibration. If a saved network is unavailable, the plug keeps its settings and retries without opening a setup network. If both SSIDs are empty, as after a factory reset, it opens Wi-Fi Manager. Restore the same plug's configuration backup before using the grinder again. To roll back to TCP, use your model's [v1.1.0 image](https://github.com/decentespresso/tasmota-auto-doser/releases/tag/v1.1.0).

Grinder control takes priority from the first BLE owner connection, including while waiting for tare, armed, or between doses with the relay OFF. The plug defers Wi-Fi reconnects and scans, pending credential-test work, time synchronization and Web UI polling until the owner disconnects and the relay is OFF. For maintenance, disable grinder mode on the HDS to release the connection, then retry the web page. Stopping a dose alone does not release the plug.

## Service

| Purpose | UUID | Properties |
| --- | --- | --- |
| Service | `6f735400-6d61-4d8e-bb2b-f5e23b9d7801` | Legacy advertisement |
| Command | `6f735401-6d61-4d8e-bb2b-f5e23b9d7801` | Write without response, write |
| Reply | `6f735402-6d61-4d8e-bb2b-f5e23b9d7801` | Notify |

The advertisement contains the service UUID and eight manufacturer data bytes: `FF FF` followed by the six bytes of the factory Wi-Fi station MAC in display order. It includes no name. The plug does not need a Wi-Fi connection for discovery or control. There is no pairing, PIN, or authentication; use only in a trusted area.

Each ATT value consists of a request ID followed by 1 to 19 ASCII bytes. IDs 1 through 255 are chosen by the requester and echoed in each reply fragment. ID zero is reserved for unsolicited `BUSY` or `ERR`. A line may span several values with the same ID. Terminate lines with LF, except `!` is a single-byte command in its own value. It aborts any partial line and drops pending owner writes before commanding OFF.

The first GAP connection reserves the owner slot. It must subscribe and send `HELLO <scale_mac>` within two seconds. MAC strings use upper-case hex and colons. The owner can then send `PING`, `ON`, `OFF`, `!`, `STATE`, or `BYE`. Replies are `OK <plug_mac> state=ON|OFF` or `ERR <plug_mac> reason=<reason>`. A contender receives unsolicited `BUSY <plug_mac>` after subscribing, then disconnects 250 ms later without affecting the owner.

Send `PING` every 500 ms when idle. The two-second watchdog runs from receipt of the last complete valid command, not from when the plug processes its queue. The plug commands OFF and closes on timeout, owner link loss or unsubscribe, or a framing, queue, or notify failure. A BUSY peer is never promoted even if the owner drops. `OK` reports software relay state, not physical contact state.

## Verification

The TCP console diagnostics and TCP smoke scripts do not apply to the BLE build. Run the BLE host checks from the repository root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tools/grinder_ble_protocol_test.cpp -o grinder_ble_protocol_test
./grinder_ble_protocol_test
python tools/test_grinder_ble_setup.py
python tools/test_grinder_ble_watchdog.py
python tools/test_grinder_ble_web_poll.py
python tools/test_grinder_ble_wifi_retry.py
```

Start with the grinder disconnected. Verify cold boot OFF, greeting, ON/OFF, cutoff, heartbeat timeout and second-client rejection. Start a Wi-Fi credential test before connecting the owner; keep it connected across the test timeout, first with the relay OFF and then during a dose. Confirm maintenance resumes only after disconnect and relay OFF.

For release qualification, use a timestamped outlet-state monitor. Set acceptance limits for normal cutoff and abrupt owner loss before the run. Measure from the cutoff trigger or induced link loss to physical outlet OFF, including HDS power loss, explicit BLE disconnect and loss of radio range. Test with the saved AP absent and on a connected LAN with the NTP server unreachable; include a cold boot with an early owner and repeat with the tablet's DE1 and scale connections active. Record firmware revisions, hardware model, sample count, and worst observed latency for each case against its limit. A BLE acknowledgement or a Web UI reading is not a physical measurement. Follow [empty-grinder commissioning](grinder-tcp.md#empty-grinder-commissioning) only after the dry tests pass.
