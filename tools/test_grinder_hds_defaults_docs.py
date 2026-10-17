import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = "https://github.com/decentespresso/openscale/blob/22219ec71349a809a5f6fbf0a5ff43c4ec7e2c0b/include/grinder_runtime.h"
HDS_DEFAULTS = {
    "Grinder": "Off",
    "Target": "15.0 g",
    "Safety": "2.0 g",
    "Zero range": "-1.0 g to 1.0 g",
    "Zero hold": "1000 ms",
    "Target tolerance": "0.5 g",
}


def main():
    protocol = (ROOT / "docs/grinder-tcp.md").read_text(encoding="utf-8")
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    defaults = re.search(r"```text\n(Grinder: Off\n.*?)\n```", protocol, re.DOTALL)
    assert defaults is not None
    assert dict(line.split(": ", 1) for line in defaults[1].splitlines()) == HDS_DEFAULTS
    settings = protocol.split("### HDS Settings\n", 1)[1].split("Normal use:", 1)[0]
    assert HDS_SOURCE in settings
    assert "v3.1.15" in settings and "Grind by weight" in settings
    assert "installed HDS" in settings
    assert "Saved settings and adaptive safety can differ" in settings
    assert "target grams - safety grams" in settings
    assert "[versioned source defaults](docs/grinder-tcp.md#hds-settings)" in readme
    assert "Default HDS settings:" not in readme
    assert "Safety 2.0 g" not in readme and "Safety 0.2 g" not in readme
    assert "your configured zero hold time" in protocol
    print("test_grinder_hds_defaults_docs passed")


if __name__ == "__main__":
    main()
