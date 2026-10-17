import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def gh(repository, *arguments, check=True):
    return subprocess.run(
        ("gh", *arguments, "--repo", repository),
        check=check,
        capture_output=True,
        text=True,
    )


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_assets(repository, tag, files):
    release = json.loads(gh(repository, "release", "view", tag, "--json", "isDraft,assets").stdout)
    if {asset["name"] for asset in release["assets"]} != {path.name for path in files}:
        raise ValueError("Release asset set does not match the complete local set")
    with tempfile.TemporaryDirectory() as temporary:
        gh(repository, "release", "download", tag, "--dir", temporary)
        if any(digest(Path(temporary) / path.name) != digest(path) for path in files):
            raise ValueError("Release assets differ from local firmware/checksums; use a new tag")


def publish(repository, tag, directory, firmware):
    expected = frozenset(firmware)
    if len(expected) != len(firmware) or any(
        Path(name).name != name or not name.endswith(".bin") for name in expected
    ):
        raise ValueError("Expected firmware names must be unique .bin filenames")
    actual = {path.name for path in directory.iterdir() if path.name != "SHA256SUMS.txt"}
    if actual != expected or not all((directory / name).is_file() for name in expected):
        raise ValueError("Local firmware set does not match the expected complete set")
    binaries = tuple(directory / name for name in sorted(expected))
    checksums = directory / "SHA256SUMS.txt"
    checksums.write_text(
        "".join(f"{digest(path)}  {path.name}\n" for path in binaries),
        encoding="ascii",
        newline="\n",
    )
    files = (*binaries, checksums)
    result = gh(repository, "release", "view", tag, "--json", "isDraft,assets", check=False)
    release = json.loads(result.stdout) if result.returncode == 0 else None
    if release is not None and not release["isDraft"]:
        verify_assets(repository, tag, files)
        print(f"{tag} already contains identical published assets")
        return
    if release is None:
        gh(
            repository, "release", "create", tag, "--draft", "--verify-tag",
            "--title", tag, "--notes", "Grinder TCP firmware release.",
        )
    elif {asset["name"] for asset in release["assets"]} - {path.name for path in files}:
        raise ValueError("Draft contains unexpected assets; inspect it before retrying")
    gh(repository, "release", "upload", tag, *(str(path) for path in files), "--clobber")
    verify_assets(repository, tag, files)
    gh(repository, "release", "edit", tag, "--draft=false")
    print(f"Published {tag} with verified firmware and checksums")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("repository")
    parser.add_argument("tag")
    parser.add_argument("directory", type=Path)
    parser.add_argument("firmware", nargs="+")
    arguments = parser.parse_args()
    try:
        publish(arguments.repository, arguments.tag, arguments.directory, arguments.firmware)
    except subprocess.CalledProcessError as error:
        parser.exit(1, error.stderr or f"{error}\n")
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
