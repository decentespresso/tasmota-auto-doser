# Releasing

This project publishes grinder firmware from GitHub Releases.

## Version

Use semantic version tags:

```text
v1.0.0
v1.0.1
v1.1.0
```

The release tag is the public project version. The firmware version shown in Tasmota still includes the upstream Tasmota base version and image label.

## Checklist

Before tagging:

1. Confirm the branch is the intended public release state.
2. Confirm `README.md` and `docs/grinder-tcp.md` match the supported hardware list.
3. Run the host protocol and driver simulation tests.
4. Build both firmware environments.
5. Smoke-test the NOUS A6T image on real hardware with no grinder load.
6. Confirm Web/API `Power1 ON` cannot keep the relay on.
7. Confirm TCP `HELLO`, `ON`, `OFF`, `!`, `STATE`, `BYE`, second-client `BUSY`, dropped-client fail-safe, and heartbeat timeout.

## Release Assets

Normal releases attach:

```text
tasmota32-nous-a6t-grinder.bin
tasmota32-grinder.bin
SHA256SUMS.txt
```

These `.bin` files are for Tasmota web UI file upload.

Factory images are not published as normal release assets. Maintainers can build them from source for serial recovery when needed.

## Publish

Create and push a tag:

```powershell
git tag v1.0.0
git push origin development
git push origin v1.0.0
```

The workflow builds only the two grinder images and publishes the release when the tag build passes.
