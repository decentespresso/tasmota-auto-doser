from grinder_ble_maintenance_test import run_profiles
from test_wifi_config_scan import ROOT, function_source


def main():
    web = ROOT / "tasmota/tasmota_xdrv_driver/xdrv_01_9_webserver.ino"
    production = web.read_text(encoding="utf-8")
    poll = function_source(production, "void PollDnsWebserver(void) {")
    dispatch = function_source(production, "bool Xdrv01(uint32_t function) {")
    source = r"""
#define FIRMWARE_MINIMAL
#define RESTART_AFTER_INITIAL_WIFI_CONFIG 1
#define AddLog(...) ((void)0)
enum { FUNC_LOOP, FUNC_EVERY_SECOND, FUNC_ACTIVE, WIFI_TEST_FINISHED, WIFI_TEST_FINISHED_BAD,
       MAX_WIFI_OPTION, WL_CONNECTED, WL_NO_SSID_AVAIL, WL_CONNECT_FAILED };
struct { bool initial_config = false; } Web;
struct { int config_counter, wifi_test_counter, wifiTest, old_wificonfig, save_data_counter;
         bool wifi_test_AP_TIMEOUT, wifi_Test_Restart; } Wifi{};
struct { int sta_config, save_data; } settings{}, *Settings = &settings;
struct { int wifi_state_flag, save_data_counter, restart_flag; } TasmotaGlobal{};
struct { int scans = 0; int status() { return WL_NO_SSID_AVAIL; }
         void scanNetworks() { ++scans; } } WiFi;
struct IPAddress {};
bool WifiGetIP(IPAddress *, bool) { return false; }
void SettingsSaveAll() { assert(false); }
struct Dns { int calls = 0; void processNextRequest() { calls++; } } dns;
struct Http { int calls = 0; void handleClient() { calls++; } } web;
Dns *DnsServer = &dns;
Http *Webserver = &web;
""" + poll + "\n" + dispatch + r"""
int main() {
    GrinderBleInput.enabled = true;
    assert(GrinderBleInput.connect(1, 0) && GrinderBleInput.connect(2, 0));
    Wifi.wifi_test_counter = 1;
    Xdrv01(FUNC_LOOP);
    Xdrv01(FUNC_EVERY_SECOND);
#ifdef USE_GRINDER_BLE
    assert(!web.calls && Wifi.wifi_test_counter == 1 && !WiFi.scans);
#else
    assert(web.calls == 1 && !Wifi.wifi_test_counter && WiFi.scans == 1);
#endif
    GrinderBleInput.disconnect(1);
    relayOn = true;
    Xdrv01(FUNC_LOOP);
    Xdrv01(FUNC_EVERY_SECOND);
#ifdef USE_GRINDER_BLE
    assert(!web.calls && Wifi.wifi_test_counter == 1 && !WiFi.scans);
#endif
    relayOn = false;
    const int previousCalls = web.calls;
    Xdrv01(FUNC_LOOP);
    Xdrv01(FUNC_EVERY_SECOND);
    assert(web.calls == previousCalls + 1 && dns.calls == 3);
    assert(!Wifi.wifi_test_counter && WiFi.scans == 1 && Wifi.wifiTest == WIFI_TEST_FINISHED_BAD);
}
"""
    run_profiles(source)


if __name__ == "__main__":
    main()
