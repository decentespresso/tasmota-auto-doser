import os
from pathlib import Path
import subprocess
import tempfile

from test_wifi_config_scan import ROOT, function_source


def main():
    support = ROOT / "tasmota/tasmota_support"
    wifi = (support / "support_wifi.ino").read_text(encoding="utf-8")
    tick = function_source((support / "support_tasmota.ino").read_text(encoding="utf-8"),
                           "void Every250mSeconds(void)")
    wifi_case = tick[tick.index("  case 2:"):tick.index("  case 3:")]
    source = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#define AddLog(...) ((void)0)
enum { WIFI_RESTART, WIFI_MANAGER, WIFI_SERIAL, WIFI_RETRY, WIFI_WAIT };
enum { WL_CONNECTED = 1, WL_NO_SSID_AVAIL, WL_CONNECT_FAILED };
enum { SET_STASSID1, SET_STASSID2, SET_APBSSID1, WIFI_CHECK_SEC = 20, WIFI_RESCAN_MINUTES = 44 };
const char *ssid = "saved";
const char *SettingsText(int index) { return index == SET_STASSID1 ? ssid : ""; }
struct {
  int sta_config = WIFI_MANAGER, sta_active = 0, wifi_channel = 0;
  uint32_t ipv4_address[5]{};
  uint8_t wifi_bssid[6]{};
  struct { bool use_wifi_rescan = false, use_wifi_scan = false; } flag3;
  struct { bool network_wifi = true; } flag4;
} settings, *Settings = &settings;
struct { int counter = 1, retry_init = 20, retry = 20, max_retry = 0, status = 0,
             scan_state = 0, config_counter = 0, config_type = 0; } Wifi;
struct {
  uint32_t status() { return 0; }
  uint32_t gatewayIP() { return 0; } uint32_t subnetMask() { return 0; }
  uint32_t dnsIP(int = 0) { return 0; } uint32_t channel() { return 0; }
  uint8_t *BSSID() { return Settings->wifi_bssid; }
} WiFi;
struct { int wifi_state_flag = WIFI_RESTART, restart_flag = 0, uptime = 1; } TasmotaGlobal;
struct { int begun = 0; } Mdns;
int retries = 0, scans = 0, config = -1;
bool relayOn = true;
bool GrinderTcpRelayStateOn() { return relayOn; }
void WifiBegin(int, int) { ++retries; }
void WifiBeginAfterScan() { ++scans; Wifi.scan_state = 0; }
void WifiConfig(int mode) { config = mode; }
void WifiSetState(int) {}
bool WifiHasIP() { return false; }
void WifiDisable() {}
void WifiProcessEvents() {}
""" + function_source(wifi, "void WifiCheckIp(void) {") + "\n" + function_source(
        wifi, "void WifiCheck(uint8_t param)"
    ) + "\nvoid tick() { switch (2) {\n" + wifi_case + r"""
} }
int main() {
  Wifi.scan_state = 1;
  tick();
#ifdef USE_GRINDER_BLE
  assert(!retries && !scans && Wifi.scan_state == 1 && Wifi.retry == Wifi.retry_init);
  TasmotaGlobal.wifi_state_flag = WIFI_MANAGER;
  tick();
  assert(config == -1 && TasmotaGlobal.wifi_state_flag == WIFI_MANAGER);
  relayOn = false;
  tick();
  assert(config == WIFI_MANAGER && TasmotaGlobal.wifi_state_flag == WIFI_RESTART);
  Wifi.counter = 1;
  tick();
#endif
  assert(retries == 1 && scans == 1);
  relayOn = false;
  Wifi.retry = 0;
  Wifi.counter = 1;
  tick();
#ifdef USE_GRINDER_BLE
  assert(config == WIFI_RETRY);
#else
  assert(config == WIFI_MANAGER);
#endif
  ssid = "";
  config = -1;
  tick();
  assert(config == WIFI_MANAGER && !TasmotaGlobal.restart_flag);
}
"""
    with tempfile.TemporaryDirectory() as directory:
        for name, flags in (("stock", []), ("tcp", ["-DUSE_GRINDER_TCP"]),
                            ("ble", ["-DUSE_GRINDER_TCP", "-DUSE_GRINDER_BLE"])):
            executable = Path(directory) / f"{name}.exe"
            subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-DESP32", *flags, "-x", "c++", "-", "-o", str(executable)],
                           input=source, text=True, check=True)
            subprocess.run([str(executable)], check=True, timeout=5)


if __name__ == "__main__":
    main()
