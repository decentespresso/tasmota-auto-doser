from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DRIVER = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp.ino").read_text(encoding="utf-8")
DIAGNOSTICS = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_diagnostics.h").read_text(encoding="utf-8")
RECOVERY = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_recovery.h").read_text(encoding="utf-8")
CONFIG = (ROOT / "tasmota/my_user_config.h").read_text(encoding="utf-8")


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
    assert "strcmp(bssid, GrinderTcpDiag.observed_bssid)" in RECOVERY
    assert "GrinderTcpDiag.network_generation++;" in RECOVERY
    assert "GrinderTcpRestartServer(reason);" in RECOVERY
    assert "GRINDER_TCP_MDNS_REFRESH" in RECOVERY
    assert "!authenticated" in DRIVER
    assert '"Grinder|Status|Restart"' in DIAGNOSTICS
    for key in ("Net", "TCP", "mDNS", "Relay", "Heap", "Last", "Count"):
        assert f'\\"{key}\\"' in DIAGNOSTICS
    assert 'char proto_version[] = "1";' in RECOVERY
    print("test_grinder_recovery_contract passed")


if __name__ == "__main__":
    main()
