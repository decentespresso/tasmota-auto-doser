from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main():
    protocol = (ROOT / "docs/grinder-tcp.md").read_text(encoding="utf-8")
    setup = (ROOT / "docs/plug-setup.md").read_text(encoding="utf-8")
    readme = (ROOT / "README.md").read_text(encoding="utf-8")

    assert "physical relay state" not in protocol
    assert "The outlet is off" not in setup
    assert "software relay state" in protocol
    assert "Software commands the relay OFF" in setup
    assert "welded contacts" in protocol and "welded contacts" in setup
    assert "listener startup flag" in setup
    assert "do not prove that another device can connect or discover the plug" in setup

    for document in (protocol, setup, readme):
        assert "harmless" not in document.lower()

    commissioning = protocol.split("## Empty-Grinder Commissioning\n", 1)[1].split("\n## ", 1)[0]
    assert "lamp" not in commissioning.lower()
    for requirement in (
        "motor starting/inrush current",
        "no beans",
        "empty-running and duty-cycle limits",
        "independent mains disconnection",
        "after boot",
        "motor starts",
        "motor stops at cutoff",
        "over several seconds",
        "target - safety",
        "connection loss",
        "disconnect mains power and stop",
    ):
        assert requirement in commissioning, requirement

    assert "[empty-grinder commissioning](docs/grinder-tcp.md#empty-grinder-commissioning)" in readme
    assert "[empty-grinder commissioning](#empty-grinder-commissioning)" in protocol
    assert "[empty-grinder commissioning](grinder-tcp.md#empty-grinder-commissioning)" in setup
    assert "[step-by-step setup guide with screenshots](docs/plug-setup.md)" in readme
    for name in ("configuration", "firmware-upgrade", "main-menu", "wifi-parameters"):
        image = f"images/plug-setup/{name}.jpg"
        assert image in setup
        assert (ROOT / "docs" / image).is_file()

    print("test_grinder_commissioning_docs passed")


if __name__ == "__main__":
    main()
