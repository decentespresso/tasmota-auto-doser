from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = re.search(r"^" + re.escape(signature) + r"[^\n;]*\s*\{", source, re.MULTILINE).start()
    return source[start:source.index("\n}", start) + 2]


def main():
    commands = (ROOT / "tasmota/tasmota_support/support_command.ino").read_text(encoding="utf-8")
    support = (ROOT / "tasmota/tasmota_support/support_tasmota.ino").read_text(encoding="utf-8")
    web = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_01_9_webserver.ino").read_text(encoding="utf-8")
    functions = [function(support, "char* GetOtaUrl("),
                 function(commands, "void CmndUpgrade("),
                 function(commands, "void CmndOtaUrl("),
                 function(web, "void HandleUpgradeFirmwareStart(")]
    template = (ROOT / "tools/grinder_empty_ota_test.cpp.in").read_text(encoding="utf-8")
    source = template.replace("__PRODUCTION_FUNCTIONS__", "\n\n".join(functions))
    with tempfile.TemporaryDirectory() as directory:
        for grinder in (False, True):
            executable = Path(directory) / f"empty_ota_{grinder}.exe"
            flags = ["-DUSE_GRINDER_TCP"] if grinder else []
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                            "-x", "c++", "-", "-o", str(executable)],
                           input=source, text=True, check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
