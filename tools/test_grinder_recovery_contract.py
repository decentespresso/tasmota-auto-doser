import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DRIVER = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp.ino").read_text(encoding="utf-8")
DIAGNOSTICS = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_diagnostics.h").read_text(encoding="utf-8")
RECOVERY = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_recovery.h").read_text(encoding="utf-8")
CONFIG = (ROOT / "tasmota/my_user_config.h").read_text(encoding="utf-8")
SOAK = (ROOT / "tools/grinder_soak_test.ps1").read_text(encoding="utf-8")
WIFI = (ROOT / "tasmota/tasmota_support/support_wifi.ino").read_text(encoding="utf-8")


def ordered(text, *tokens):
    positions = [text.index(token) for token in tokens]
    assert positions == sorted(positions), tokens


def compact_size(value):
    return len(json.dumps(value, separators=(",", ":")))


def assert_diagnostics_fit():
    u32 = 4294967295
    i32 = -2147483648
    ip = "255.255.255.255"
    text23 = "x" * 23
    status = {
        "GrinderStatus": {
            "Net": {"Up": 1, "WL": i32, "IP": ip, "BSSID": "FF:FF:FF:FF:FF:FF", "RSSI": i32, "Gen": u32, "Link": u32, "Reason": 255, "RecoveryMs": u32},
            "TCP": {"Listen": 1, "Gen": u32, "Client": 1, "Hello": 1, "Closing": 1, "PeerIP": ip, "PeerPort": 65535, "RxAge": u32},
            "mDNS": {"Ad": 1, "Up": 1, "MaxMs": u32},
            "Relay": {"Owner": 1, "On": 1},
            "Heap": {"Free": u32, "Min": u32},
            "Loop": {"MaxGapMs": u32},
            "Last": {"Event": text23, "Age": u32, "Net": text23},
            "Count": {"NetUp": u32, "NetDn": u32, "Link": u32, "WiFiDn": u32, "LostIP": u32, "GotIP": u32, "Restart": u32, "MdnsFail": u32},
        }
    }
    diag = {
        "GrinderDiag": {
            "Change": {"IP": u32, "BSSID": u32},
            "TCP": {"Start": u32, "Stop": u32, "Accept": u32, "Busy": u32, "Close": u32, "HelloTO": u32, "HeartbeatTO": u32, "Protocol": u32},
            "mDNS": {"Try": u32, "Ok": u32, "Fail": u32, "Down": u32, "SvcFail": u32, "TxtFail": u32},
            "Last": {"Close": text23, "Off": text23},
        }
    }
    assert compact_size(status) < 1040
    assert compact_size(diag) < 1040
    assert compact_size(status) < 1200
    assert compact_size(diag) < 1200


def main():
    assert "#ifdef USE_GRINDER_TCP\n#undef USE_DEEPSLEEP" in CONFIG
    assert "Settings->deepsleep = 0;" in DRIVER
    assert "Settings->flag5.wifi_no_sleep = 1;" in DRIVER
    assert "Settings->flag3.use_wifi_rescan = 0;" in DRIVER
    ordered(
        DRIVER[DRIVER.index("void GrinderTcpStop(const char *reason)"):],
        "GrinderTcpRelayOff(reason);",
        "GrinderTcpCloseActiveNow(false, reason);",
        "GrinderTcpStopClosingClients();",
        "GrinderTcpServer.stop();",
        "GrinderTcpRemoveMdnsService();",
    )
    ordered(
        DRIVER[DRIVER.index("void GrinderTcpRestartServer(const char *reason)"):],
        "GrinderTcpStop(reason);",
        "GrinderTcpStart();",
    )
    assert "ARDUINO_EVENT_WIFI_STA_DISCONNECTED" in WIFI
    assert "ARDUINO_EVENT_WIFI_STA_LOST_IP" in WIFI
    assert "ARDUINO_EVENT_WIFI_STA_GOT_IP" in WIFI
    assert "event->event_info.wifi_sta_disconnected.reason" in WIFI
    assert "event->event_info.got_ip.ip_changed" in WIFI
    assert "portENTER_CRITICAL(&WifiEventState.mux);" in WIFI
    assert "portEXIT_CRITICAL(&WifiEventState.mux);" in WIFI
    process = WIFI[WIFI.index("void WifiProcessEvents(void)"):WIFI.index("uint32_t WifiDisconnectEventCount(void)")]
    assert "Wifi.counter = 1;" in process
    assert "StopWebserver();" in process
    assert "WebserverStopSocket();" not in process
    assert "WebserverStartSocket();" not in process
    assert "WifiEventState.outage_active" in process
    assert "WifiEventState.last_disconnect_reason" in process
    assert "MDNS.end();" in RECOVERY
    assert "Mdns.begun = 0;" in RECOVERY
    assert "WifiMDNSAfterReconnectv4();" in RECOVERY
    mark_down = RECOVERY[RECOVERY.index("void GrinderTcpMarkNetworkDown"):RECOVERY.index("void GrinderTcpEnsureMdns")]
    assert "GrinderTcpResetMdnsResponder();" in mark_down
    assert "WiFi.onEvent(GrinderTcpWifiEvent)" not in RECOVERY
    assert "(WL_CONNECTED == WiFi.status()) && WifiHasIPv4()" in RECOVERY
    assert "WifiLinkCount()" in RECOVERY
    assert "WifiEventGeneration()" in RECOVERY
    assert "observed_wifi_event_generation" in RECOVERY
    assert "strcmp(bssid, GrinderTcpDiag.observed_bssid)" in RECOVERY
    assert "GrinderTcpDiag.network_generation += wifi_event_changes ? wifi_event_changes : 1;" in RECOVERY
    assert "GrinderTcpRestartServer(reason);" in RECOVERY
    assert "GRINDER_TCP_MDNS_REFRESH" not in DRIVER
    assert "mdns_refresh_at" not in DRIVER
    assert "GrinderTcpAdvertise(refresh)" not in DRIVER
    assert "if (!GrinderTcpNetworkUsable())" in DRIVER
    assert "!authenticated" in DRIVER
    assert '"Grinder|Status|Diag|Restart"' in DIAGNOSTICS
    assert "void CmndGrinderDiag(void)" in DIAGNOSTICS
    for key in ("Net", "TCP", "mDNS", "Relay", "Heap", "Loop", "Last", "Count"):
        assert f'\\"{key}\\"' in DIAGNOSTICS
    for key in ("WL", "Link", "Reason", "RecoveryMs", "MaxMs", "MaxGapMs", "WiFiDn", "LostIP", "GotIP"):
        assert f'\\"{key}\\"' in DIAGNOSTICS
    assert "MdnsRefresh" not in DIAGNOSTICS
    assert "MdnsRefreshOk" not in DIAGNOSTICS
    assert 'char proto_version[] = "1";' in RECOVERY
    assert "[int]$IntervalSeconds = 2" in SOAK
    assert "TcpLatencyMs" in SOAK
    assert "HttpLatencyMs" in SOAK
    assert "DiscoveryLatencyMs" in SOAK
    assert "MaxLoopGapMs" in SOAK
    assert "MaxMdnsOperationMs" in SOAK
    assert_diagnostics_fit()
    print("test_grinder_recovery_contract passed")


if __name__ == "__main__":
    main()
