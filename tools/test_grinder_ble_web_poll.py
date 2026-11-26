import os
import subprocess
import tempfile
from pathlib import Path

from test_wifi_config_scan import ROOT, function_source


def main():
    web = ROOT / "tasmota/tasmota_xdrv_driver/xdrv_01_9_webserver.ino"
    poll = function_source(web.read_text(encoding="utf-8"), "void PollDnsWebserver(void) {")
    source = r"""
#include <cassert>
struct Dns { int calls = 0; void processNextRequest() { calls++; } } dns;
struct Web { int calls = 0; void handleClient() { calls++; } } web;
Dns *DnsServer = &dns;
Web *Webserver = &web;
bool relayOn = false;
bool GrinderTcpRelayStateOn() { return relayOn; }
""" + poll + r"""
int main() {
    PollDnsWebserver();
    assert(web.calls == 1);
    relayOn = true;
    PollDnsWebserver();
#ifdef USE_GRINDER_BLE
    assert(web.calls == 1);
#else
    assert(web.calls == 2);
#endif
    relayOn = false;
    const int previousCalls = web.calls;
    PollDnsWebserver();
    assert(web.calls == previousCalls + 1 && dns.calls == 3);
}
"""
    with tempfile.TemporaryDirectory() as directory:
        for name, flags in (
            ("stock", []),
            ("tcp", ["-DUSE_GRINDER_TCP"]),
            ("ble", ["-DUSE_GRINDER_TCP", "-DUSE_GRINDER_BLE"]),
        ):
            executable = Path(directory) / f"{name}.exe"
            subprocess.run(
                [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 *flags, "-x", "c++", "-", "-o", str(executable)],
                input=source, text=True, check=True,
            )
            subprocess.run([str(executable)], check=True, timeout=5)


if __name__ == "__main__":
    main()
