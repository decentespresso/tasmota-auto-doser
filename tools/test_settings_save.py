import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def functions(path, signatures):
    source = (ROOT / path).read_text(encoding="utf-8")
    return "\n".join(
        source[start:source.index("\n}", start) + 2]
        for signature in signatures
        if (start := source.find(signature)) >= 0
    )


def main():
    production = functions("tasmota/tasmota_support/settings.ino", [
        "uint16_t GetCfgCrc16(", "uint16_t GetSettingsCrc(",
        "uint32_t GetCfgCrc32(", "uint32_t GetSettingsCrc32(", "void SettingsSave(",
    ])
    storage = functions("tasmota/tasmota_support/support_esp32.ino", [
        "void NvmSave(", "bool NvmSave(", "void SettingsWrite(", "bool SettingsWrite(",
    ])
    filesystem = functions("tasmota/tasmota_xdrv_driver/xdrv_50_filesystem.ino", ["bool TfsSaveFile("])
    timers = (ROOT / "tasmota/tasmota_support/support_tasmota.ino").read_text(encoding="utf-8")
    start = timers.index("    if (TasmotaGlobal.save_data_counter && CommandsReady()) {")
    end = timers.index("    if (TasmotaGlobal.restart_flag && CommandsReady())", start)
    autosave = "void AutoSave() {\n" + timers[start:end] + "}\n"
    template = (ROOT / "tools/settings_save_test.cpp.in").read_text(encoding="utf-8")
    source = template.replace("__PRODUCTION__", filesystem + "\n" + storage + "\n" + production + "\n" + autosave)
    with tempfile.TemporaryDirectory() as directory:
        for mode in ([], ["-DUSE_UFILESYS"]):
            executable = Path(directory) / ("settings-save.exe" if os.name == "nt" else "settings-save")
            subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-DESP32", "-DUSE_COUNTER", *mode, "-x", "c++", "-", "-o", str(executable),
            ], input=source, text=True, check=True)
            subprocess.run([str(executable)], check=True)
            print(f"{mode}: settings save checks passed", flush=True)


if __name__ == "__main__":
    main()
