from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DRIVER = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp.ino").read_text(encoding="utf-8")
DIAGNOSTICS = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_diagnostics.h").read_text(encoding="utf-8")
RECOVERY = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_recovery.h").read_text(encoding="utf-8")
CONFIG = (ROOT / "tasmota/my_user_config.h").read_text(encoding="utf-8")
SOAK = (ROOT / "tools/grinder_soak_test.ps1").read_text(encoding="utf-8")


def ordered(text, *tokens):
    positions = [text.index(token) for token in tokens]
    assert positions == sorted(positions), tokens


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
    assert "ARDUINO_EVENT_WIFI_STA_DISCONNECTED" in RECOVERY
    assert "ARDUINO_EVENT_WIFI_STA_LOST_IP" in RECOVERY
    assert "ARDUINO_EVENT_WIFI_STA_GOT_IP" in RECOVERY
    assert "event->event_info.wifi_sta_disconnected.reason" in RECOVERY
    assert "WiFi.onEvent(GrinderTcpWifiEvent)" in RECOVERY
    assert "WifiSetState(0);" in RECOVERY
    assert "WifiSetState(1);" in RECOVERY
    assert "WebserverStopSocket();" in RECOVERY
    assert "WebserverStartSocket();" in RECOVERY
    assert "Wifi.counter = 1;" in RECOVERY
    assert "(WL_CONNECTED == WiFi.status()) && WifiHasIPv4()" in RECOVERY
    assert "WifiLinkCount()" in RECOVERY
    assert "observed_wifi_event_generation" in RECOVERY
    assert "strcmp(bssid, GrinderTcpDiag.observed_bssid)" in RECOVERY
    assert "GrinderTcpDiag.network_generation++;" in RECOVERY
    assert "GrinderTcpRestartServer(reason);" in RECOVERY
    assert "GRINDER_TCP_MDNS_REFRESH" not in DRIVER
    assert "mdns_refresh_at" not in DRIVER
    assert "GrinderTcpAdvertise(refresh)" not in DRIVER
    assert "if (!GrinderTcpNetworkUsable())" in DRIVER
    assert "!authenticated" in DRIVER
    assert '"Grinder|Status|Restart"' in DIAGNOSTICS
    for key in ("Net", "TCP", "mDNS", "Relay", "Heap", "Loop", "Last", "Count"):
        assert f'\\"{key}\\"' in DIAGNOSTICS
    for key in ("WL", "Link", "Reason", "RecoveryMs", "MaxMs", "MaxGapMs", "WiFiDn", "LostIP", "GotIP", "WiFiAge"):
        assert f'\\"{key}\\"' in DIAGNOSTICS
    assert 'char proto_version[] = "1";' in RECOVERY
    assert "[int]$IntervalSeconds = 2" in SOAK
    assert "TcpLatencyMs" in SOAK
    assert "HttpLatencyMs" in SOAK
    assert "DiscoveryLatencyMs" in SOAK
    assert "MaxLoopGapMs" in SOAK
    assert "MaxMdnsOperationMs" in SOAK
    print("test_grinder_recovery_contract passed")


if __name__ == "__main__":
    main()
