import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


FIRMWARE = ("tasmota32-grinder.bin", "tasmota32-nous-a6t-grinder.bin")


def workflow_variants(workflow):
    declaration = next(line for line in workflow.splitlines() if line.startswith("  GRINDER_VARIANTS: "))
    return declaration.split(": ", 1)[1][1:-1]


class GhStub:
    def __init__(self, directory, exists=False, draft=True, fail_upload=False):
        self.directory = directory
        self.exists = exists
        self.draft = draft
        self.fail_upload = fail_upload
        self.corrupt_download = False
        self.calls = ()

    def __call__(self, command, *, check=True, **kwargs):
        self.calls += (tuple(command),)
        operation = command[2]
        stdout = ""
        returncode = 0
        if operation == "view":
            returncode = 0 if self.exists else 1
            stdout = json.dumps({
                "isDraft": self.draft,
                "assets": [{"name": path.name} for path in self.directory.iterdir()],
            })
        elif operation == "create":
            self.exists = True
            self.draft = "--draft" in command
        elif operation == "upload":
            paths = command[4:command.index("--clobber")]
            for index, path in enumerate(paths):
                shutil.copyfile(path, self.directory / Path(path).name)
                if self.fail_upload and index == 0:
                    self.fail_upload = False
                    returncode = 1
                    break
        elif operation == "download":
            destination = Path(command[command.index("--dir") + 1])
            for path in self.directory.iterdir():
                shutil.copyfile(path, destination / path.name)
            if self.corrupt_download:
                (destination / FIRMWARE[0]).write_bytes(b"corrupted")
        elif operation == "edit":
            if "--draft=false" not in command:
                raise AssertionError(command)
            self.draft = False
        else:
            raise AssertionError(command)
        if check and returncode:
            raise subprocess.CalledProcessError(returncode, command, stderr="interrupted")
        return subprocess.CompletedProcess(command, returncode, stdout=stdout, stderr="")


class ReleasePublicationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.local = Path(self.temporary.name) / "local"
        self.remote = Path(self.temporary.name) / "remote"
        self.local.mkdir()
        self.remote.mkdir()
        for name in FIRMWARE:
            (self.local / name).write_bytes(name.encode("ascii"))
        self.gh = GhStub(self.remote)

    def publish(self):
        from grinder_release_publish import publish

        with patch("subprocess.run", self.gh):
            publish("owner/repository", "v1.0.0", self.local, FIRMWARE)

    def remote_bytes(self):
        return {path.name: path.read_bytes() for path in self.remote.iterdir()}

    def test_complete_draft_is_verified_before_publication(self):
        self.publish()
        self.assertFalse(self.gh.draft)
        self.assertEqual(set(self.remote_bytes()), {*FIRMWARE, "SHA256SUMS.txt"})
        operations = tuple(command[2] for command in self.gh.calls)
        self.assertLess(operations.index("download"), operations.index("edit"))

    def test_interrupted_upload_stays_draft_and_retry_completes(self):
        self.gh.fail_upload = True
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertTrue(self.gh.draft)
        self.assertEqual(len(self.remote_bytes()), 1)
        self.assertNotIn("edit", tuple(command[2] for command in self.gh.calls))
        self.publish()
        self.assertFalse(self.gh.draft)
        self.assertEqual(set(self.remote_bytes()), {*FIRMWARE, "SHA256SUMS.txt"})

    def test_identical_published_rerun_does_not_write(self):
        self.publish()
        before = self.remote_bytes()
        self.gh.calls = ()
        self.publish()
        self.assertEqual(self.remote_bytes(), before)
        self.assertEqual(tuple(command[2] for command in self.gh.calls), ("view", "view", "download"))

    def test_differing_published_rerun_does_not_write(self):
        self.publish()
        before = self.remote_bytes()
        (self.local / FIRMWARE[0]).write_bytes(b"rebuilt")
        self.gh.calls = ()
        with self.assertRaisesRegex(ValueError, "differ"):
            self.publish()
        self.assertEqual(self.remote_bytes(), before)
        self.assertTrue(all(command[2] in ("view", "download") for command in self.gh.calls))

    def test_checksum_mismatch_does_not_modify_published_release(self):
        self.publish()
        (self.remote / "SHA256SUMS.txt").write_text("wrong\n", encoding="ascii")
        before = self.remote_bytes()
        with self.assertRaisesRegex(ValueError, "differ"):
            self.publish()
        self.assertEqual(self.remote_bytes(), before)

    def test_incomplete_published_set_is_not_repaired_in_place(self):
        self.publish()
        (self.remote / FIRMWARE[0]).unlink()
        before = self.remote_bytes()
        with self.assertRaisesRegex(ValueError, "asset set"):
            self.publish()
        self.assertEqual(self.remote_bytes(), before)

    def test_download_verification_failure_stays_draft(self):
        self.gh.corrupt_download = True
        with self.assertRaisesRegex(ValueError, "differ"):
            self.publish()
        self.assertTrue(self.gh.draft)
        self.assertNotIn("edit", tuple(command[2] for command in self.gh.calls))

    def test_missing_local_firmware_stops_before_github(self):
        (self.local / FIRMWARE[0]).unlink()
        with self.assertRaisesRegex(ValueError, "firmware set"):
            self.publish()
        self.assertEqual(self.gh.calls, ())

    def test_extra_draft_asset_is_not_published(self):
        self.gh.exists = True
        (self.remote / "unexpected.bin").write_bytes(b"old")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            self.publish()
        self.assertTrue(self.gh.draft)

    def test_workflow_matrix_and_publisher_share_two_or_three_variants(self):
        from grinder_release_publish import main

        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/grinder_release.yml").read_text()
        declared_json = workflow_variants(workflow)
        development = tuple(name.removesuffix(".bin") for name in FIRMWARE)
        overlays = (json.loads(declared_json), development, (*development, "tasmota32c3-athom-eu-v3-grinder"))
        for index, overlay in enumerate(overlays):
            with self.subTest(variants=len(overlay)):
                overlay_workflow = workflow.replace(declared_json, json.dumps(overlay))
                self.assertIn('run: echo "variants=$GRINDER_VARIANTS" >> "$GITHUB_OUTPUT"', overlay_workflow)
                self.assertIn("variants: ${{ steps.variants.outputs.variants }}", overlay_workflow)
                self.assertIn("variant: ${{ fromJSON(needs.host-tests.outputs.variants) }}", overlay_workflow)
                self.assertIn('release "$GRINDER_VARIANTS"', overlay_workflow)
                local = Path(self.temporary.name) / f"local-{index}"
                remote = Path(self.temporary.name) / f"remote-{index}"
                local.mkdir()
                remote.mkdir()
                expected = {f"{variant}.bin" for variant in overlay}
                for name in expected:
                    (local / name).write_bytes(name.encode("ascii"))
                stub = GhStub(remote)
                arguments = ("publisher", "owner/repository", "v1.0.0", str(local), workflow_variants(overlay_workflow))
                with patch("sys.argv", list(arguments)), patch("subprocess.run", stub):
                    main()
                self.assertFalse(stub.draft)
                self.assertEqual({path.name for path in remote.iterdir()}, {*expected, "SHA256SUMS.txt"})
                self.assertEqual(
                    {line.split("  ", 1)[1] for line in (remote / "SHA256SUMS.txt").read_text().splitlines()},
                    expected,
                )


if __name__ == "__main__":
    unittest.main()
