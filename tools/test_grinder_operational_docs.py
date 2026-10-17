import ast
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def response_schema(source, command):
    body = source.split(f"void Cmnd{command}(void) {{\n", 1)[1].split("\n}", 1)[0]
    strings = re.findall(r'(?:Response_P|ResponseAppend_P)\(PSTR\(("(?:\\.|[^"\\])*")\)', body)
    response = "".join(ast.literal_eval(value) for value in strings)
    return json.loads(re.sub(r"%[dus]", "0", response))[command]


def main():
    document = (ROOT / "docs/grinder-tcp.md").read_text(encoding="utf-8")
    diagnostics = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_diagnostics.h").read_text(encoding="utf-8")
    recovery = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp_recovery.h").read_text(encoding="utf-8")
    driver = (ROOT / "tasmota/tasmota_xdrv_driver/xdrv_95_grinder_tcp.ino").read_text(encoding="utf-8")
    operational = document.split("## Network Recovery\n", 1)[1].split("## HDS Troubleshooting\n", 1)[0]
    assert "removed and re-added every 60 seconds" not in operational
    assert "no periodic 60-second refresh" in operational
    assert "listener startup flag" in operational
    assert "does not prove" in operational and "TCP" in operational
    assert "while the relay is off and no HELLO-completed client is active" in operational

    schemas = {command: response_schema(diagnostics, command) for command in ("GrinderStatus", "GrinderDiag")}
    for command, schema in schemas.items():
        section = operational.split(f"### {command}\n", 1)[1].split("\n### ", 1)[0]
        rows = re.findall(r"^\| `(\w+)` \| (.*?) \|$", section, re.MULTILINE)
        assert {group for group, _ in rows} == set(schema), command
        for group, contents in rows:
            fields = re.findall(r"`(\w+)`", contents)
            if fields:
                assert set(fields) == set(schema[group]), (command, group, fields)

    paths = re.findall(r"`(Grinder(?:Status|Diag)\.\w+\.\w+)`", operational)
    for path in paths:
        command, group, field = path.split(".")
        assert field in schemas[command][group], path
    assert "Count.BSSID" not in operational
    assert "subnet mask" not in operational and "gateway" not in operational

    txt = document.split("Expected TXT data:\n\n```text\n", 1)[1].split("\n```", 1)[0]
    txt_fields = dict(line.split("=", 1) for line in txt.splitlines())
    published_keys = re.findall(r'char key_\w+\[\] = "(\w+)";', recovery)
    assert set(txt_fields) == set(published_keys) == {"mac", "name", "model", "proto"}
    assert txt_fields["proto"] == re.search(r'char proto_version\[\] = "(\d+)";', recovery)[1]
    port = re.search(r"#define GRINDER_TCP_PORT (\d+)", driver)[1]
    assert f"SRV port is `{port}`" in document
    assert "GRINDER_TCP_MDNS_REFRESH" not in driver
    assert "mdns_refresh_at" not in driver
    print("test_grinder_operational_docs passed")


if __name__ == "__main__":
    main()
