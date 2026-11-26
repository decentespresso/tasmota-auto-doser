# Grinder BLE

The BLE grinder builds replace TCP grinder control. They retain Tasmota's Wi-Fi settings, manual provisioning, web firmware upload, and existing plug template and power-meter calibration. Missing Wi-Fi no longer triggers automatic provisioning or reboot. A TCP firmware image for the same model is the rollback path.

The maintenance Web UI pauses while the plug relay is ON so request parsing cannot delay grinder cutoff or the heartbeat watchdog. Wait until the relay is OFF before opening the UI or retrying a request.

## Service

| Purpose | UUID | Properties |
| --- | --- | --- |
| Service | `6f735400-6d61-4d8e-bb2b-f5e23b9d7801` | Legacy advertisement |
| Command | `6f735401-6d61-4d8e-bb2b-f5e23b9d7801` | Write without response, write |
| Reply | `6f735402-6d61-4d8e-bb2b-f5e23b9d7801` | Notify |

The advertisement contains the service UUID and eight manufacturer data bytes: `FF FF` followed by the six bytes of the factory Wi-Fi station MAC in display order. It includes no name. The plug does not need a Wi-Fi connection for discovery or control. There is no pairing, PIN, or authentication; use only in a trusted area.

Each ATT value consists of a request ID followed by 1 to 19 ASCII bytes. IDs 1 through 255 are chosen by the requester and echoed in each reply fragment. ID zero is reserved for unsolicited `BUSY` or `ERR`. A line may span several values with the same ID. Terminate lines with LF, except `!` is a single-byte command in its own value. It aborts any partial line and drops pending owner writes before commanding OFF.

The first GAP connection reserves the owner slot. It must subscribe and send `HELLO <scale_mac>` within two seconds. MAC strings use upper-case hex and colons. The owner can then send `PING`, `ON`, `OFF`, `!`, `STATE`, or `BYE`. Replies are `OK <plug_mac> state=ON|OFF` or `ERR <plug_mac> reason=<reason>`. A contender receives unsolicited `BUSY <plug_mac>` after subscribing, then disconnects 250 ms later without affecting the owner.

Send `PING` every 500 ms when idle. The plug commands OFF and closes after two seconds without a valid command, on owner link loss or unsubscribe, or on a framing, queue, or notify failure. A BUSY peer is never promoted even if the owner drops. `OK` reports software relay state, not physical contact state.

## Verification

The TCP console diagnostics and TCP smoke scripts do not apply to the BLE build. Run the BLE host check from the repository root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tools/grinder_ble_protocol_test.cpp -o grinder_ble_protocol_test
./grinder_ble_protocol_test
python tools/test_grinder_ble_web_poll.py
```

Start hardware checks with the grinder disconnected. Verify boot OFF, greeting, ON/OFF, cutoff, abrupt owner loss, heartbeat timeout, and a rejected second client while the owner's relay remains unchanged. Repeat with no shared Wi-Fi and with the tablet's DE1 and scale BLE connections active. Record cutoff and fail-off timing before operating the grinder. Follow the physical safety checks in [empty-grinder commissioning](grinder-tcp.md#empty-grinder-commissioning) before grinding.
