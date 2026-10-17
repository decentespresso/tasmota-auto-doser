from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = re.search(r"^" + re.escape(signature) + r"[^\n]*\{", source, re.MULTILINE).start()
    return source[start:source.index("\n}", start) + 2]


def main():
    driver = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp.ino").read_text(encoding="utf-8")
    support = (ROOT / "tasmota/tasmota_support/support.ino").read_text(encoding="utf-8")
    settings = (ROOT / "tasmota/tasmota_support/settings.ino").read_text(encoding="utf-8")
    web = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_01_9_webserver.ino").read_text(encoding="utf-8")
    state_start = driver.index("struct {\n  WiFiClient client;")
    state_end = driver.index("} GrinderTcp;", state_start) + len("} GrinderTcp;")
    functions = [function(driver, signature) for signature in (
        "void GrinderTcpNeutralizePowerControls(", "bool GrinderTcpRelayStateOn(",
        "bool GrinderTcpRelayOwned(", "void GrinderTcpRelayOffDirect(",
        "void GrinderTcpRelayOff(", "void GrinderTcpResetTx(",
        "void GrinderTcpResetActiveClient(", "void GrinderTcpCloseActiveNow(",
        "void GrinderTcpStart(", "void GrinderTcpStopClosingClients(",
        "void GrinderTcpStop(", "void GrinderTcpLoop(",
        "bool Xdrv95(",
    )]
    functions += [function(support, "void AllowInterrupts("),
                  function(settings, "void SettingsSaveAll("),
                  function(web, "void UploadServices("),
                  function(web, "void HandleUploadLoop("),
                  function(web, "void HandleUploadDone("),
                  function(web, "void HandleSwitchBootPartition(")]
    template = (ROOT / "tools/grinder_update_lifecycle_test.cpp.in").read_text(encoding="utf-8")
    source = template.replace("__DRIVER_STATE__", driver[state_start:state_end])
    source = source.replace("__PRODUCTION_FUNCTIONS__", "\n\n".join(functions))
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / "grinder_update_lifecycle_test.exe"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-x", "c++",
                        "-", "-o", str(executable)], input=source, text=True, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
