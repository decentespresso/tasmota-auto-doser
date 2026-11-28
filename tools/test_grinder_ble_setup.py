import os
import subprocess
import tempfile
from pathlib import Path

from test_wifi_config_scan import ROOT, function_source


def main():
    driver = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_ble.h").read_text(encoding="utf-8")
    source = r"""
#include <cassert>
#include <cstdint>
#define PSTR(value) value
constexpr int ESP_OK = 0, ESP_MAC_WIFI_STA = 0, LOG_LEVEL_INFO = 0;
constexpr uint32_t GrinderBleSetupRetry = 5000;
struct { void *advertising{}; uint32_t setupAttemptAt = 0U - GrinderBleSetupRetry; bool setupNeedsReset{}; } GrinderBle;
struct { const char *plug_mac = ""; } GrinderTcp;
uint32_t now;
int attempts, starts, stops, clears;
bool hostRunning, stopAllowed;
uint32_t millis() { return now; }
bool GrinderTcpCacheIdentity() { return true; }
int esp_read_mac(uint8_t *, int) { return ESP_OK; }
void AddLog(int, const char *, ...) {}
namespace NimBLEDevice {
  bool init(const char *) {
    assert(!hostRunning);
    hostRunning = ++attempts > 1;
    return hostRunning;
  }
  bool deinit(bool clear) {
    if (clear) {
      assert(!hostRunning);
      ++clears;
    } else {
      assert(hostRunning);
      ++stops;
      if (!stopAllowed) return false;
      hostRunning = false;
    }
    return true;
  }
}
bool GrinderBleCreateService(const uint8_t (&manufacturer)[8]) {
  assert(hostRunning && manufacturer[0] == 0xff && manufacturer[1] == 0xff);
  if (++starts == 1) return false;
  assert(clears == 1);
  GrinderBle.advertising = &starts;
  return true;
}
"""
    source += function_source(driver, "static bool GrinderBleConfigure(void) {")
    source += r"""
int main() {
  assert(!GrinderBleConfigure() && attempts == 1);
  now = UINT32_MAX - 100;
  assert(!GrinderBleConfigure() && starts == 1 && hostRunning);
  now += GrinderBleSetupRetry - 1;
  assert(!GrinderBleConfigure() && stops == 0 && attempts == 2);
  ++now;
  assert(!GrinderBleConfigure() && stops == 1 && clears == 0 && starts == 1);
  stopAllowed = true;
  assert(!GrinderBleConfigure() && stops == 1);
  now += GrinderBleSetupRetry;
  assert(GrinderBleConfigure() && starts == 2 && stops == 2 && attempts == 3);
  now += GrinderBleSetupRetry;
  assert(GrinderBleConfigure() && starts == 2 && stops == 2);
}
"""
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / "grinder-ble-setup.exe"
        subprocess.run(
            [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-x", "c++", "-", "-o", str(executable)],
            input=source, text=True, check=True,
        )
        subprocess.run([str(executable)], check=True, timeout=5)


if __name__ == "__main__":
    main()
