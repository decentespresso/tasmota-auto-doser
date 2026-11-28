import os
from pathlib import Path
import subprocess
import tempfile

from test_wifi_config_scan import ROOT, function_source


def run_profiles(source):
    driver = ROOT / "tasmota/tasmota_xdrv_driver"
    ble = (driver / "xdrv_95_grinder_ble.h").read_text(encoding="utf-8")
    control = r"""
#include <cassert>
#include "xdrv_95_grinder_ble_mailbox.h"
#define portENTER_CRITICAL(mux) ((void)0)
#define portEXIT_CRITICAL(mux) ((void)0)
GrinderBleMailbox GrinderBleInput{};
bool relayOn = false;
bool GrinderTcpRelayStateOn() { return relayOn; }
""" + function_source(ble, "bool GrinderBleControlActive(void) {")
    with tempfile.TemporaryDirectory() as directory:
        for name, flags in (("stock", []), ("tcp", ["-DUSE_GRINDER_TCP"]),
                            ("ble", ["-DUSE_GRINDER_TCP", "-DUSE_GRINDER_BLE"])):
            executable = Path(directory) / f"{name}.exe"
            subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-DESP32", *flags, "-I", str(driver), "-x", "c++", "-", "-o", str(executable)],
                           input=control + "\n" + source, text=True, check=True)
            subprocess.run([str(executable)], check=True, timeout=5)
