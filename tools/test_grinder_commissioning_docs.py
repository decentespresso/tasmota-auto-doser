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

    commissioning = protocol.split("## Harmless-Load Commissioning\n", 1)[1].split("\n## ", 1)[0]
    for requirement in (
        "motor starting/inrush current",
        "mains-rated",
        "independent mains disconnection",
        "after boot",
        "turns on",
        "turns off at cutoff",
        "connection loss",
        "disconnect mains power and stop",
    ):
        assert requirement in commissioning, requirement

    assert "[harmless-load commissioning](docs/grinder-tcp.md#harmless-load-commissioning)" in readme
    assert "[harmless-load commissioning](#harmless-load-commissioning)" in protocol
    assert "[harmless-load commissioning](grinder-tcp.md#harmless-load-commissioning)" in setup
    assert "[step-by-step setup guide with screenshots](docs/plug-setup.md)" in readme
    for name in ("configuration", "firmware-upgrade", "main-menu", "wifi-parameters"):
        image = f"images/plug-setup/{name}.jpg"
        assert image in setup
        assert (ROOT / "docs" / image).is_file()

    print("test_grinder_commissioning_docs passed")


if __name__ == "__main__":
    main()
