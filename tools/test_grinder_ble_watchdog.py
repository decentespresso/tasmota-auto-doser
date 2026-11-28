import os
import subprocess
import tempfile
from pathlib import Path

from test_wifi_config_scan import ROOT, function_source


def main():
    driver = ROOT / "tasmota/tasmota_xdrv_driver"
    ble = (driver / "xdrv_95_grinder_ble.h").read_text(encoding="utf-8")
    source = r"""
#include <cassert>
#include "xdrv_95_grinder_ble_mailbox.h"
#define portENTER_CRITICAL(mux) ((void)0)
#define portEXIT_CRITICAL(mux) ((void)0)
constexpr uint32_t GrinderBleHelloTimeout = 2000, GRINDER_TCP_HEARTBEAT_TIMEOUT = 2000;
constexpr uint32_t start = UINT32_MAX - 1500;
uint32_t now = start, notifyDelay = 0;
uint32_t millis() { return now; }
GrinderBleMailbox GrinderBleInput{};
struct { uint32_t ownerGeneration; GrinderBleLine line; } GrinderBle{};
struct { bool client_open, greeted, close_pending; uint32_t last_rx; char plug_mac[18]; } GrinderTcp{};
bool relayOn = false;
unsigned onCommands = 0;
bool GrinderBleOwnsRelayLink() { return GrinderBleInput.owns(GrinderBle.ownerGeneration); }
bool GrinderTcpRelayStateOn() { return relayOn; }
void GrinderTcpRelayOnCommand() { relayOn = true; ++onCommands; }
void GrinderTcpRelayOff(const char * = nullptr) { relayOn = false; }
void GrinderBleCloseOwner(const GrinderBlePeer &, const char *) { GrinderTcp.close_pending = true; relayOn = false; }
void GrinderBleReject(const GrinderBlePeer &, uint8_t, GrinderTcpReason) { assert(false); }
void GrinderBleDisconnect(const GrinderBlePeer &, bool) { assert(false); }
bool GrinderBleNotify(const GrinderBlePeer &, bool, uint8_t, const char *) { now += notifyDelay; return true; }
""" + function_source(ble, "static void GrinderBleProcessCommand(") + "\n" + function_source(
        ble, "static void GrinderBlePollOwner("
    ) + r"""
void writeAt(uint32_t elapsed, uint8_t id, const char *text) {
  uint8_t packet[GrinderBleValueSize]{id};
  const size_t length = strlen(text);
  assert(length < sizeof(packet));
  memcpy(packet + 1, text, length);
  GrinderBleInput.write(1, packet, length + 1, start + elapsed);
}
void pollAt(uint32_t elapsed) {
  now = start + elapsed;
  GrinderBlePollOwner(GrinderBleInput.owner, GrinderBleInput.fault, GrinderBleInput.faultId);
}
void greet() {
  GrinderBleInput.enabled = true;
  assert(GrinderBleInput.connect(1, start));
  GrinderBleInput.subscribe(1, true);
  writeAt(100, 1, "HELLO AA:BB:CC:DD:E");
  pollAt(200);
  assert(!GrinderTcp.greeted && GrinderTcp.last_rx == start);
  writeAt(400, 1, "E:FF\n");
  pollAt(1700);
  assert(GrinderTcp.greeted && GrinderTcp.last_rx == start + 400);
}
int main(int argc, char **) {
  greet();
  if (argc == 1) {
    writeAt(1800, 2, "ON\n");
    pollAt(2200);
    assert(relayOn);
    writeAt(3700, 3, "PI");
    pollAt(3799);
    assert(relayOn && !GrinderTcp.close_pending);
    writeAt(3800, 3, "NG\n");
    pollAt(3800);
    assert(!relayOn && GrinderTcp.close_pending);
  } else {
    writeAt(1800, 2, "PING\n");
    writeAt(1900, 3, "ON\n");
    notifyDelay = 1800;
    pollAt(2100);
    assert(onCommands == 0 && GrinderTcp.close_pending);
  }
}
"""
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / "watchdog.exe"
        subprocess.run(
            [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-I", str(driver), "-x", "c++", "-", "-o", str(executable)],
            input=source, text=True, check=True,
        )
        for args in ([], ["delayed-notify"]):
            subprocess.run([str(executable), *args], check=True, timeout=5)


if __name__ == "__main__":
    main()
