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
        "bool GrinderTcpNetworkUsable(", "void GrinderTcpMarkNetworkDown(", "void GrinderTcpCheckNetwork(",
    )] + [production_function(driver, name) for name in (
        "void GrinderTcpStart(", "void GrinderTcpStop(const char *reason) {",
        "void GrinderTcpRestartServer(const char *reason) {",
    )]
    source = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

uint32_t now_ms = 100;
uint32_t millis() { return now_ms; }
constexpr uint32_t GRINDER_TCP_IDENTITY_CHECK = 5000;
#define PSTR(value) value
constexpr int LOG_LEVEL_INFO = 1;
constexpr int LOG_LEVEL_ERROR = 2;

struct State {
  uint32_t mdns_retry_at = 0;
  bool server_started = false;
  bool client_open = false;
  bool advertised = false;
  bool suspended = false;
} GrinderTcp;
struct { uint8_t restart_flag = 0, ota_state_flag = 0; } TasmotaGlobal;
__DIAGNOSTICS__
enum class Failure { None, Socket, Bind, Listen };
struct NativeServer {
  Failure failure = Failure::None;
  unsigned begin_calls = 0;
  unsigned resources = 0;
  bool listening = false;
  void begin() {
    ++begin_calls;
    if (failure == Failure::Socket) { return; }
    ++resources;
    if (failure != Failure::None) { return; }
    listening = true;
  }
  void stop() { resources = 0; listening = false; }
  void setNoDelay(bool) {}
  explicit operator bool() const { return listening; }
} GrinderTcpServer;
bool output_on = false;
constexpr int WL_CONNECTED = 3;
struct {
  int status() const { return WL_CONNECTED; }
  uint32_t localIP() const { return 1; }
} WiFi;

size_t strlcpy(char *dest, const char *value, size_t size) {
  std::snprintf(dest, size, "%s", value);
  return std::strlen(value);
}
void AddLog(int, const char *, ...) {}
void AddLogServerActive(const char *) {}
bool WifiHasIPv4() { return true; }
uint32_t WifiEventGeneration() { return 0; }
uint16_t WifiLinkCount() { return 1; }
void GrinderTcpFormatBssid(char *dest, size_t size) { strlcpy(dest, "AA:BB:CC:DD:EE:FF", size); }
bool GrinderTcpCacheIdentity() { return true; }
bool GrinderTcpRelayHardwareSupported() { return true; }
bool GrinderTcpRelayStateOn() { return output_on; }
void GrinderTcpRelayOff(const char * = nullptr) { output_on = false; }
void GrinderTcpNeutralizePowerControls() {}
void GrinderTcpRecordEvent(const char *) {}
void GrinderTcpCloseActiveNow(bool, const char *) { GrinderTcp.client_open = false; }
void GrinderTcpStopClosingClients() {}
void GrinderTcpRemoveMdnsService() { GrinderTcp.advertised = false; }
void GrinderTcpAdvertise() { ++GrinderTcpDiag.mdns_attempts; GrinderTcp.advertised = true; }
void GrinderTcpStart();
void GrinderTcpStop(const char *);
void GrinderTcpRestartServer(const char *);

__PRODUCTION_FUNCTIONS__

void CheckFailure(Failure failure) {
  now_ms = 100;
  GrinderTcp = State{};
  GrinderTcpDiag = {};
  GrinderTcpServer = NativeServer{};
  GrinderTcpServer.failure = failure;
  output_on = true;
  for (unsigned attempt = 1; attempt <= 3; ++attempt) {
    GrinderTcpCheckNetwork();
    assert(!output_on);
    assert(!GrinderTcp.server_started && !GrinderTcp.advertised);
    assert(!GrinderTcpServer && GrinderTcpServer.resources == 0);
    assert(GrinderTcpServer.begin_calls == attempt);
    assert(GrinderTcpDiag.server_starts == 0 && GrinderTcpDiag.server_generation == 0);
    assert(GrinderTcpDiag.mdns_attempts == 0);
    now_ms += GRINDER_TCP_IDENTITY_CHECK - 1;
    GrinderTcpCheckNetwork();
    assert(GrinderTcpServer.begin_calls == attempt);
    now_ms += 1;
  }
  GrinderTcpServer.failure = Failure::None;
  GrinderTcpCheckNetwork();
  assert(GrinderTcp.server_started && GrinderTcp.advertised);
  assert(GrinderTcpServer && GrinderTcpServer.resources == 1);
  assert(GrinderTcpServer.begin_calls == 4);
  assert(GrinderTcpDiag.server_starts == 1 && GrinderTcpDiag.server_generation == 1);
  assert(GrinderTcpDiag.mdns_attempts == 1);
  assert(!output_on);
  now_ms += GRINDER_TCP_IDENTITY_CHECK;
  GrinderTcpCheckNetwork();
  assert(GrinderTcpServer.begin_calls == 4);
  GrinderTcpStop("test_shutdown");
  assert(!GrinderTcpServer && GrinderTcpServer.resources == 0);
  assert(!GrinderTcp.server_started && !GrinderTcp.advertised);
}

int main() {
  for (const auto failure : {Failure::Socket, Failure::Bind, Failure::Listen}) {
    CheckFailure(failure);
  }
  puts("production listener start checks passed");
}
'''.replace("__DIAGNOSTICS__", diagnostics[:diagnostics.index("void GrinderTcpRecordEvent(")])
    source = source.replace("__PRODUCTION_FUNCTIONS__", "\n".join(functions))
    with tempfile.TemporaryDirectory(prefix="grinder-listener-") as directory:
        executable = Path(directory) / "listener-start.exe"
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-UNDEBUG",
            "-x", "c++", "-", "-o", str(executable),
        ], input=source, text=True, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
