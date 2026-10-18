from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def production_function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def main():
    driver = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp.ino").read_text(encoding="utf-8")
    recovery = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_recovery.h").read_text(encoding="utf-8")
    diagnostics = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_diagnostics.h").read_text(encoding="utf-8")
    timers = (ROOT / "tasmota/tasmota_support/support.ino").read_text(encoding="utf-8")
    functions = [production_function(timers, name) for name in (
        "inline int32_t TimeDifference(", "int32_t TimePassedSince(", "bool TimeReached(",
    )] + [production_function(recovery, name) for name in (
        "bool GrinderTcpNetworkUsable(", "void GrinderTcpMarkNetworkDown(",
        "bool GrinderTcpWriteMdnsTxt(", "void GrinderTcpRecordMdnsDuration(",
        "void GrinderTcpAdvertise(", "void GrinderTcpCheckNetwork(",
        "void GrinderTcpNetworkUp(", "void GrinderTcpNetworkDown(",
    )] + [production_function(driver, name) for name in (
        "void GrinderTcpStart(", "void GrinderTcpRestartServer(const char *reason) {",
    )]
    source = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }
constexpr uint32_t GRINDER_TCP_IDENTITY_CHECK = 5000;
constexpr uint32_t GRINDER_TCP_MDNS_RETRY = 5000;
constexpr uint32_t GRINDER_TCP_PORT = 31980;
#define GRINDER_TCP_MODEL "TEST"
#define PSTR(value) value
constexpr int LOG_LEVEL_INFO = 1;
constexpr int LOG_LEVEL_ERROR = 2;

struct State {
  uint32_t mdns_retry_at = 0;
  char plug_mac[18] = "10:20:30:40:50:60";
  bool server_started = false;
  bool client_open = false;
  bool advertised = false;
} GrinderTcp;
__DIAGNOSTICS__
struct { bool begun = true; } Mdns;
struct {
  bool listening = false;
  void begin() { listening = true; }
  void stop() { listening = false; }
  void setNoDelay(bool) {}
  explicit operator bool() const { return listening; }
} GrinderTcpServer;
bool output_on = false;
bool network_usable = true;
bool service_success = true;
uint32_t event_generation = 0;
constexpr int WL_CONNECTED = 3;
struct {
  int status() const { return network_usable ? WL_CONNECTED : 0; }
  uint32_t localIP() const { return 1; }
} WiFi;
struct {
  bool addService(char *, char *, uint32_t) { return service_success; }
  bool addServiceTxt(char *, char *, char *, const char *) { return true; }
} MDNS;

size_t strlcpy(char *dest, const char *value, size_t size) {
  std::snprintf(dest, size, "%s", value);
  return std::strlen(value);
}
void AddLog(int, const char *, ...) {}
void AddLogServerActive(const char *) {}
bool WifiHasIPv4() { return network_usable; }
uint32_t WifiEventGeneration() { return event_generation; }
uint16_t WifiLinkCount() { return 1; }
const char *NetworkHostname() { return "plug"; }
void GrinderTcpFormatBssid(char *dest, size_t size) { strlcpy(dest, "AA:BB:CC:DD:EE:FF", size); }
bool GrinderTcpCacheIdentity() { return true; }
bool GrinderTcpRelayHardwareSupported() { return true; }
bool GrinderTcpRelayStateOn() { return output_on; }
void GrinderTcpRelayOff() { output_on = false; }
void GrinderTcpNeutralizePowerControls() {}
void GrinderTcpRecordEvent(const char *) {}
void GrinderTcpEnsureMdns() { Mdns.begun = true; }
void GrinderTcpRemoveMdnsService() { GrinderTcp.advertised = false; }
void GrinderTcpStop(const char *) {
  output_on = false;
  GrinderTcpServer.stop();
  GrinderTcp.server_started = false;
  GrinderTcp.client_open = false;
  GrinderTcpRemoveMdnsService();
}
void GrinderTcpStart();
void GrinderTcpRestartServer(const char *);

__PRODUCTION_FUNCTIONS__

void Reset(uint32_t at) {
  now_ms = at;
  GrinderTcp = State{};
  GrinderTcpDiag = {};
  GrinderTcpServer = {};
  output_on = false;
  network_usable = true;
  service_success = true;
  event_generation = 0;
}

void CheckReconnect(uint32_t at) {
  Reset(at);
  GrinderTcpNetworkUp();
  assert(GrinderTcp.server_started);
  assert(GrinderTcp.advertised);
  assert(GrinderTcpDiag.server_starts == 1);
  assert(GrinderTcpDiag.mdns_successes == 1);
  assert(GrinderTcpDiag.identity_check_at == at + GRINDER_TCP_IDENTITY_CHECK);
  output_on = true;
  GrinderTcp.client_open = true;
  ++event_generation;
  now_ms += 1;
  GrinderTcpCheckNetwork();
  assert(!output_on && !GrinderTcp.client_open);
  assert(GrinderTcp.server_started && GrinderTcp.advertised);
  assert(GrinderTcpDiag.server_starts == 2);
  assert(GrinderTcpDiag.observed_wifi_event_generation == event_generation);
  network_usable = false;
  GrinderTcpNetworkDown();
  assert(!GrinderTcp.server_started);
  now_ms += 1;
  network_usable = true;
  GrinderTcpCheckNetwork();
  assert(GrinderTcp.server_started && GrinderTcp.advertised);
  assert(GrinderTcpDiag.server_starts == 3);
}

void CheckRetry(uint32_t at) {
  Reset(at);
  service_success = false;
  GrinderTcpStart();
  assert(GrinderTcpDiag.mdns_attempts == 1);
  assert(!GrinderTcp.advertised);
  now_ms = at + GRINDER_TCP_MDNS_RETRY - 1;
  GrinderTcpAdvertise();
  assert(GrinderTcpDiag.mdns_attempts == 1);
  now_ms += 1;
  service_success = true;
  GrinderTcpAdvertise();
  assert(GrinderTcpDiag.mdns_attempts == 2);
  assert(GrinderTcp.advertised);
}

int main() {
  for (const auto at : {100u, 0x80000000u, 0x90000000u, 0xfffffff0u, 0xffffec78u}) {
    CheckReconnect(at);
    CheckRetry(at);
  }
  puts("production recovery deadline checks passed");
}
'''.replace("__DIAGNOSTICS__", diagnostics[:diagnostics.index("void GrinderTcpRecordEvent(")])
    source = source.replace("__PRODUCTION_FUNCTIONS__", "\n".join(functions))
    with tempfile.TemporaryDirectory(prefix="grinder-recovery-") as directory:
        executable = Path(directory) / "recovery-deadlines.exe"
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-UNDEBUG",
            "-x", "c++", "-", "-o", str(executable),
        ], input=source, text=True, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
