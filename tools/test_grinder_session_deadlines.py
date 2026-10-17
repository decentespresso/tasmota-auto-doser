from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def production_function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def main():
    driver = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp.ino").read_text(encoding="utf-8")
    timers = (ROOT / "tasmota/tasmota_support/support.ino").read_text(encoding="utf-8")
    functions = [production_function(timers, name) for name in (
        "inline int32_t TimeDifference(", "int32_t TimePassedSince(", "bool TimeReached(",
    )] + [production_function(driver, name) for name in (
        "void GrinderTcpProcessLine(", "void GrinderTcpProcessEmergencyOff(",
        "void GrinderTcpReadClient(", "void GrinderTcpCheckTimeout(", "void GrinderTcpLoop(",
    )]
    source = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_protocol.h"

uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }
constexpr uint32_t GRINDER_TCP_HEARTBEAT_TIMEOUT = 2000;
constexpr uint32_t GRINDER_TCP_HELLO_TIMEOUT = 1000;
constexpr uint32_t GRINDER_TCP_MAX_BYTES_PER_LOOP = 256;

struct FakeClient {
  std::string input;
  bool alive = true;
  bool connected() const { return alive; }
  size_t available() const { return input.size(); }
  int read() {
    const auto value = input.front();
    input = input.substr(1);
    return value;
  }
};

struct State {
  FakeClient client;
  GrinderTcpLineReader reader = {};
  struct { uint32_t length = 0; } tx;
  uint32_t last_rx = 0;
  bool client_open = true;
  bool close_pending = false;
  bool greeted = false;
  bool authorized_on = false;
  bool server_started = true;
  bool advertised = true;
} GrinderTcp;

struct {
  uint32_t heartbeat_timeouts = 0;
  uint32_t hello_timeouts = 0;
} GrinderTcpDiag;
struct { bool begun = true; } Mdns;
bool output_on = false;

void GrinderTcpRelayOff() {
  output_on = false;
  GrinderTcp.authorized_on = false;
}
void GrinderTcpRelayOnCommand() {
  output_on = true;
  GrinderTcp.authorized_on = true;
}
void GrinderTcpCloseActiveNow(bool off, const char * = "disconnect") {
  if (off) { GrinderTcpRelayOff(); }
  GrinderTcp.client_open = false;
}
void GrinderTcpQueueActiveErrAndClose(uint32_t) { GrinderTcpCloseActiveNow(true); }
bool GrinderTcpQueueOk(bool = false) { GrinderTcp.tx.length = 1; return true; }
bool GrinderTcpRelayHardwareSupported() { return true; }
bool GrinderTcpRelayStateOn() { return output_on; }
void GrinderTcpObserveDiagnostics() {}
void GrinderTcpCheckNetwork() {}
void GrinderTcpFinishActiveClose() {}
void GrinderTcpFinishClosingClients() {}
void GrinderTcpFlushActiveTx() { GrinderTcp.tx.length = 0; }
void GrinderTcpStop(const char *) { GrinderTcpCloseActiveNow(true); }
void GrinderTcpEnforceRelayOwnership() {}
void GrinderTcpKeepAwakeWhileConnected() {}
void GrinderTcpPollServer() {}
void GrinderTcpAdvertise() {}

__PRODUCTION_FUNCTIONS__

void CheckDeadline(uint32_t started, uint32_t age, const char *input, bool greeted) {
  GrinderTcp = State{};
  GrinderTcpDiag.heartbeat_timeouts = 0;
  GrinderTcpDiag.hello_timeouts = 0;
  GrinderTcp.greeted = greeted;
  GrinderTcp.authorized_on = greeted && std::string(input) == "PING\n";
  output_on = GrinderTcp.authorized_on;
  GrinderTcp.last_rx = started;
  GrinderTcp.client.input = input;
  now_ms = started + age;
  const auto timeout = greeted ? GRINDER_TCP_HEARTBEAT_TIMEOUT : GRINDER_TCP_HELLO_TIMEOUT;
  GrinderTcpLoop();
  if (age >= timeout) {
    assert(!GrinderTcp.client_open);
    assert(!GrinderTcp.authorized_on);
    assert(!output_on);
    assert(GrinderTcp.last_rx == started);
    assert(GrinderTcp.client.input == input);
    assert(GrinderTcpDiag.heartbeat_timeouts == (greeted ? 1u : 0u));
    assert(GrinderTcpDiag.hello_timeouts == (greeted ? 0u : 1u));
  } else {
    assert(GrinderTcp.client_open);
    assert(GrinderTcp.greeted);
    assert(GrinderTcp.last_rx == now_ms);
    assert(GrinderTcp.client.input.empty());
    assert(output_on == greeted);
  }
}

int main() {
  for (const auto started : {100u, 0xfffffff0u}) {
    for (const auto age : {1999u, 2000u, 2001u}) {
      CheckDeadline(started, age, "PING\n", true);
      CheckDeadline(started, age, "ON\n", true);
    }
    for (const auto age : {999u, 1000u, 1001u}) {
      CheckDeadline(started, age, "HELLO 10:20:30:40:50:60\n", false);
    }
  }
  puts("production session deadline checks passed");
}
'''.replace("__PRODUCTION_FUNCTIONS__", "\n".join(functions))
    with tempfile.TemporaryDirectory(prefix="grinder-deadlines-") as directory:
        executable = Path(directory) / "session-deadlines.exe"
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-UNDEBUG",
            "-I", str(ROOT), "-x", "c++", "-", "-o", str(executable),
        ], input=source, text=True, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
