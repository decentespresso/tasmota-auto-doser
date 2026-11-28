# Grinder BLE

The BLE grinder builds replace TCP grinder control. A firmware upgrade preserves the plug's Wi-Fi settings, GPIO template and power-meter calibration. If a saved network is unavailable, the plug keeps its settings and retries without opening a setup network. If both SSIDs are empty, as after a factory reset, it opens Wi-Fi Manager. Restore the same plug's configuration backup before using the grinder again. To roll back to TCP, use your model's [v1.1.0 image](https://github.com/decentespresso/tasmota-auto-doser/releases/tag/v1.1.0).

While the relay is ON, the plug defers Wi-Fi reconnects, scans and maintenance Web UI requests so they do not delay cutoff. Wait until the relay is OFF before retrying a maintenance request.

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

Start hardware checks with the grinder disconnected. Verify boot OFF, greeting, ON/OFF, cutoff, abrupt owner loss, heartbeat timeout, and a rejected second client while the owner's relay remains unchanged. Repeat with no shared Wi-Fi and with the tablet's DE1 and scale BLE connections active. Record cutoff and fail-off timing before operating the grinder. Follow the physical safety checks in [empty-grinder commissioning](grinder-tcp.md#empty-grinder-commissioning) before grinding.
