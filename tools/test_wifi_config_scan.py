import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def function_source(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def production_source():
    web = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_01_9_webserver.ino").read_text(encoding="utf-8")
    driver = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp.ino").read_text(encoding="utf-8")
    start = web.index('      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_WIFI "Scanning..."));')
    end = web.index('      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_WIFI D_SCAN_DONE));', start)
    stop = function_source(driver, "void GrinderTcpStop(const char *reason) {")
    return stop + "\nint ScanConfiguration() {\n" + web[start:end] + "\nreturn n;\n}\n"


HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#define PSTR(value) value
#define D_LOG_WIFI ""
#define LOG_LEVEL_DEBUG 0
#define WIFI_SCAN_RUNNING -1
#define WIFI_SCAN_FAILED -2

struct { bool server_started = true; } GrinderTcp;
struct { unsigned server_stops = 0; } GrinderTcpDiag;
bool relay_on = true;
bool owner = true;
bool client_open = true;
bool closing_clients = true;
bool advertised = true;
unsigned shutdown_step = 0;
uint32_t now = 0;

void AddLog(int, const char *) {}
uint32_t millis() { return now; }
void delay(unsigned duration) {
  now += duration;
  if (now > 20000) { throw std::runtime_error("unbounded scan wait"); }
}
void GrinderTcpRelayOff(const char *reason) {
  assert(!strcmp(reason, "wifi_scan"));
  assert(shutdown_step++ == 0);
  relay_on = owner = false;
}
void GrinderTcpCloseActiveNow(bool, const char *) {
  assert(!relay_on && !owner && shutdown_step++ == 1);
  client_open = false;
}
void GrinderTcpStopClosingClients() {
  assert(shutdown_step++ == 2);
  closing_clients = false;
}
struct {
  void stop() { assert(shutdown_step++ == 3); }
} GrinderTcpServer;
void GrinderTcpRecordEvent(const char *) {}
void GrinderTcpRemoveMdnsService() {
  assert(shutdown_step++ == 4);
  advertised = false;
}

struct {
  int initial = 0;
  int completion = 0;
  unsigned polls = 0;
  unsigned deleted = 0;
  bool finish = true;
  int scanNetworks(bool async) {
    assert(async);
#ifdef USE_GRINDER_TCP
    assert(!relay_on && !owner && !client_open && !closing_clients);
    assert(!GrinderTcp.server_started && !advertised && shutdown_step == 5);
#endif
    return initial;
  }
  int scanComplete() {
    polls++;
    return finish && polls >= 2 ? completion : WIFI_SCAN_RUNNING;
  }
  void scanDelete() { deleted++; }
} WiFi;

__PRODUCTION__

int main(int argc, char **argv) {
  assert(argc == 2);
  const char *test = argv[1];
  if (!strcmp(test, "immediate-failed")) {
    WiFi.initial = WIFI_SCAN_FAILED;
    assert(ScanConfiguration() == 0);
    assert(WiFi.polls == 0 && WiFi.deleted == 1 && now == 0);
  } else if (!strcmp(test, "pending-failed")) {
    WiFi.initial = WIFI_SCAN_RUNNING;
    WiFi.completion = WIFI_SCAN_FAILED;
    assert(ScanConfiguration() == 0);
    assert(WiFi.polls == 2 && WiFi.deleted == 1 && now == 100);
  } else if (!strcmp(test, "pending-success")) {
    WiFi.initial = WIFI_SCAN_RUNNING;
    WiFi.completion = 3;
    assert(ScanConfiguration() == 3);
    assert(WiFi.polls == 2 && WiFi.deleted == 0 && now == 100);
  } else if (!strcmp(test, "timeout")) {
    WiFi.initial = WIFI_SCAN_RUNNING;
    WiFi.finish = false;
    assert(ScanConfiguration() == 0);
    assert(WiFi.deleted == 1 && now == 10000);
  } else {
    assert(!strcmp(test, "immediate-success"));
    WiFi.initial = 3;
    assert(ScanConfiguration() == 3);
    assert(WiFi.polls == 0 && WiFi.deleted == 0 && now == 0);
  }
  std::printf("%s passed\n", test);
}
'''


def main():
    source = HARNESS.replace("__PRODUCTION__", production_source())
    with tempfile.TemporaryDirectory(prefix="wifi-scan-") as directory:
        source_path = Path(directory) / "scan.cpp"
        source_path.write_text(source, encoding="utf-8")
        for profile in ("grinder", "standard"):
            executable = Path(directory) / (profile + ".exe")
            flags = ["-DUSE_GRINDER_TCP"] if profile == "grinder" else []
            subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags, str(source_path), "-o", str(executable)], check=True)
            for test in ("immediate-failed", "pending-failed", "pending-success", "timeout", "immediate-success"):
                subprocess.run([str(executable), test], check=True, timeout=5)
            print(profile + " production scan checks passed")


if __name__ == "__main__":
    main()
