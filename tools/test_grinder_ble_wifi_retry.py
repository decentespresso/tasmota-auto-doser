from grinder_ble_maintenance_test import run_profiles
from test_wifi_config_scan import ROOT, function_source


def main():
    support = ROOT / "tasmota/tasmota_support"
    wifi = (support / "support_wifi.ino").read_text(encoding="utf-8")
    timers = (support / "support_tasmota.ino").read_text(encoding="utf-8")
    tick = function_source(timers, "void Every250mSeconds(void)")
    wifi_cases = tick[tick.index("  case 2:"):]
    second = function_source(timers, "void PerformEverySecond(void)")
    housekeeping = second.split("  wifiKeepAlive();\n#endif", 1)[1]
    syslog = function_source((support / "support.ino").read_text(encoding="utf-8"), "void SyslogAsync(bool refresh)")
    syslog_gate = syslog[syslog.index("  if (!TasmotaGlobal.syslog_level"):syslog.index("  char* line;")]
    source = r"""
#define AddLog(...) ((void)0)
#define USE_DISCOVERY
enum { WIFI_RESTART, WIFI_MANAGER, WIFI_SERIAL, WIFI_RETRY, WIFI_WAIT };
enum { FUNC_NETWORK_UP, FUNC_NETWORK_DOWN };
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
struct { int wifi_state_flag = WIFI_RESTART, restart_flag = 0;
         uint32_t uptime = 11; bool ntp_force_sync = false; int syslog_level = 1;
         struct { bool network_down = false; } global_state; } TasmotaGlobal;
struct { int begun = 0; } Mdns;
int retries = 0, scans = 0, config = -1;
int mdnsStarts = 0, logPolls = 0;
void StartMdns() { ++mdnsStarts; }
void XdrvXsnsCall(int) {}
void MqttCheck() {}
bool NeedLogRefresh(int, uint32_t) { ++logPolls; return false; }
void WifiBegin(int, int) { ++retries; }
void WifiBeginAfterScan() { ++scans; Wifi.scan_state = 0; }
void WifiConfig(int mode) { config = mode; }
void WifiSetState(int) {}
bool WifiHasIP() { return false; }
void WifiDisable() {}
void WifiProcessEvents() {}
constexpr uint32_t START_VALID_TIME = 1000;
struct { bool user_time_entry; uint32_t utc_time, nanos; } Rtc{};
struct { int second = 0, year = 2026; } RtcTime;
int ntpQueries = 0, sketchChecks = 0;
uint32_t millis() { return 0; }
uint32_t ESP_getChipId() { return 0; }
uint64_t WifiGetNtp() { ++ntpQueries; return 1001000000000ULL; }
void RtcSync(const char *) {}
void ESP_getSketchSize() { ++sketchChecks; }
""" + "\n".join([
        function_source(wifi, "void WifiCheckIp(void) {"),
        function_source(wifi, "void WifiCheck(uint8_t param)"),
        function_source(wifi, "void WifiPollNtp() {"),
        "void everySecond() {" + housekeeping,
        "void tick(int slot = 2) { switch (slot) {\n" + wifi_cases,
        "void pollSyslog(bool refresh) { uint32_t index = 1;\n" + syslog_gate + "\n}",
    ]) + r"""
int main() {
  GrinderBleInput.enabled = true;
  assert(GrinderBleInput.connect(1, 0) && GrinderBleInput.connect(2, 0));
  Wifi.scan_state = 1;
  everySecond();
  tick();
  tick(3);
  pollSyslog(true);
#ifdef USE_GRINDER_BLE
  assert(!ntpQueries && TasmotaGlobal.ntp_force_sync && !sketchChecks);
  assert(!mdnsStarts && !logPolls);
  assert(!retries && !scans && Wifi.scan_state == 1 && Wifi.retry == Wifi.retry_init);
  TasmotaGlobal.wifi_state_flag = WIFI_MANAGER;
  GrinderBleInput.disconnect(1);
  relayOn = true;
  TasmotaGlobal.uptime = 65;
  everySecond();
  tick();
  assert(!ntpQueries && TasmotaGlobal.ntp_force_sync && !sketchChecks);
  assert(config == -1 && TasmotaGlobal.wifi_state_flag == WIFI_MANAGER);
  relayOn = false;
  everySecond();
  tick(3);
  pollSyslog(true);
  tick();
  assert(config == WIFI_MANAGER && TasmotaGlobal.wifi_state_flag == WIFI_RESTART);
  Wifi.counter = 1;
  tick();
#endif
  assert(mdnsStarts == 1 && logPolls == 1);
  assert(ntpQueries == 1 && !TasmotaGlobal.ntp_force_sync && sketchChecks == 1);
  ++TasmotaGlobal.uptime;
  everySecond();
  assert(sketchChecks == 1 && ntpQueries == 1);
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
    run_profiles(source)


if __name__ == "__main__":
    main()
