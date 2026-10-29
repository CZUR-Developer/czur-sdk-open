"""Exercise the actual Jenkins cleanup shell in isolated checkout layouts."""
import pathlib
import subprocess
import tempfile
import unittest


HELPER = pathlib.Path(__file__).resolve().parents[2] / "jenkins/sdk_open_pipeline_helpers.groovy"
SHELL = HELPER.read_text().split("def cleanPreviousArtifacts(", 1)[1].split("scriptCtx.sh \'\'\'", 1)[1].split("\'\'\'", 1)[0]


class WorkspaceCleanupTest(unittest.TestCase):
    def test_checkout_layouts_and_idempotence(self):
        for checkout in ("src", "nested/custom checkout", "."):
            with self.subTest(checkout=checkout), tempfile.TemporaryDirectory() as tmp:
                workspace = pathlib.Path(tmp) / "workspace"
                root = workspace / checkout
                root.mkdir(parents=True)
                (root / ".git").mkdir()
                artifacts = [
                    "SHA256SUMS.txt", "deb-files.txt", "windows-files.txt", "esa-urls.txt",
                    "gitee_release_note.md", "release-assets.txt", "release-create.json",
                    "release-downloads.md", "release-update-result.json", "release-update.json",
                    "release_manifest.json", "release_note.md", "manifest.json", "manifest.json.sig",
                    "sdk-open_Windows_x64_v0.0.8.53.Release.exe",
                    "sdk-open_Windows_v0.0.2.5.Beta.exe",
                    "sdk-open_macos_arm64_v0.0.8.54.Release.pkg",
                    "sdk-open_macos_arm64_v0.0.8.54.Release.dSYM.zip",
                    "sdk_open_app_arm64_hash_timestamp.debug", "sdk-open_1_arm64_common.deb",
                    "scanner_1_amd64_common.deb", "upload-SHA256SUMS.txt.json",
                    "upload-sdk-open_0.0.1.3.Alpha_amd64_common.deb.json",
                    "build_package/old.deb", "release-assets/windows/old.exe",
                    "build_and_pack/tmp/sdk-open-windows-ecs/old.exe",
                ]
                keep = ["README.md", "src/main.cc", "src/sdk_open/source.cc",
                        "build/cache", "unrelated.exe", "upload-other.json"]
                for name in artifacts + keep:
                    target = root / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text("sentinel")
                outside = pathlib.Path(tmp) / "sdk-open_Windows_old.exe"
                outside.write_text("outside")
                workspace_sentinel = workspace / "sdk-open_Windows_old.exe"
                if checkout != ".":
                    workspace_sentinel.write_text("workspace")
                for _ in range(2):
                    subprocess.run(["sh", "-eu", "-c", SHELL], cwd=str(root), check=True,
                                   stdout=subprocess.PIPE)
                for name in artifacts:
                    self.assertFalse((root / name).exists(), name)
                for name in keep:
                    self.assertEqual((root / name).read_text(), "sentinel")
                self.assertTrue((root / ".git").is_dir())
                self.assertEqual(outside.read_text(), "outside")
                if checkout != ".":
                    self.assertEqual(workspace_sentinel.read_text(), "workspace")

    def test_missing_checkout_marker_fails_before_deletion(self):
        with tempfile.TemporaryDirectory() as tmp:
            artifact = pathlib.Path(tmp) / "SHA256SUMS.txt"
            artifact.write_text("keep")
            result = subprocess.run(["sh", "-eu", "-c", SHELL], cwd=tmp)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(artifact.read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
