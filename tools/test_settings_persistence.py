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
        "uint32_t GetCfgCrc32(", "uint32_t GetSettingsCrc32(", "void SettingsLoad(",
    ])
    storage = functions("tasmota/tasmota_support/support_esp32.ino", [
        "bool NvmLoad(", "size_t NvmLoad(", "bool SettingsConfigValid(",
        "bool SettingsReadFile(", "uint32_t SettingsRead(",
    ])
    filesystem = functions("tasmota/tasmota_xdrv_driver/xdrv_50_filesystem.ino", ["bool TfsLoadFile("])
    template = (ROOT / "tools/settings_persistence_test.cpp.in").read_text(encoding="utf-8")
    source = template.replace("__PRODUCTION__", filesystem + "\n" + storage + "\n" + production)
    with tempfile.TemporaryDirectory() as directory:
        for target in ("ESP32", "ESP32C3", "ESP32S3", "ESP32S2", "ESP32C2", "ESP32C6", "ESP32P4", "ESP32C5"):
            executable = Path(directory) / (target + (".exe" if os.name == "nt" else ""))
            subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-DESP32", "-DUSE_UFILESYS", f"-DCONFIG_IDF_TARGET_{target}=1",
                "-x", "c++", "-", "-o", str(executable),
            ], input=source, text=True, check=True)
            subprocess.run([str(executable)], check=True)
            print(f"{target}: settings persistence checks passed", flush=True)


if __name__ == "__main__":
    main()
