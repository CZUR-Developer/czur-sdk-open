import hashlib
import os
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile


SDK_OPEN_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPACK_TOOL = os.path.join(
    SDK_OPEN_ROOT, "scripts", "harmony", "repack_signed_app.py"
)
MEMBER = "sdkopen_tray-default.hap"


def file_digest(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        digest.update(stream.read())
    return digest.hexdigest()


class HarmonySignedAppPackagingTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = self.temp_dir.name
        self.unsigned_app = os.path.join(self.root, "harmony-default-unsigned.app")
        self.signed_app = os.path.join(self.root, "harmony-default-signed.app")
        self.signed_hap = os.path.join(self.root, "sdkopen_tray-default-signed.hap")
        self.sign_tool = os.path.join(self.root, "hap-sign-tool.jar")
        self.fake_java = os.path.join(self.root, "java")
        with open(self.sign_tool, "wb") as stream:
            stream.write(b"test jar")
        with open(self.fake_java, "w", encoding="utf-8") as stream:
            stream.write(
                "#!/usr/bin/env python3\n"
                "import pathlib, sys\n"
                "args = sys.argv[1:]\n"
                "hap = pathlib.Path(args[args.index('-inFile') + 1])\n"
                "if not hap.read_bytes().startswith(b'SIGNED:'):\n"
                "    sys.exit(7)\n"
                "pathlib.Path(args[args.index('-outCertChain') + 1]).write_bytes(b'cert')\n"
                "pathlib.Path(args[args.index('-outProfile') + 1]).write_bytes(b'profile')\n"
            )
        os.chmod(self.fake_java, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
        with zipfile.ZipFile(self.unsigned_app, "w") as archive:
            archive.writestr("pac.json", b"{}")
            archive.writestr(MEMBER, b"UNSIGNED")

    def tearDown(self):
        self.temp_dir.cleanup()

    def command(self, *extra):
        return [
            sys.executable,
            REPACK_TOOL,
            "--unsigned-app",
            self.unsigned_app,
            "--signed-app",
            self.signed_app,
            "--signed-hap",
            self.signed_hap,
            "--member",
            MEMBER,
        ] + list(extra)

    def test_prepare_removes_only_stale_signed_outputs(self):
        with open(self.signed_hap, "wb") as stream:
            stream.write(b"old hap")
        with open(self.signed_app, "wb") as stream:
            stream.write(b"old app")

        subprocess.run(self.command("--prepare"), check=True)

        self.assertFalse(os.path.exists(self.signed_hap))
        self.assertFalse(os.path.exists(self.signed_app))
        self.assertTrue(os.path.isfile(self.unsigned_app))

    def test_unsigned_build_does_not_leave_misleading_signed_app(self):
        with open(self.signed_app, "wb") as stream:
            stream.write(b"hvigor app with unsigned hap")

        subprocess.run(self.command(), check=True)

        self.assertFalse(os.path.exists(self.signed_app))
        self.assertTrue(os.path.isfile(self.unsigned_app))

    def test_repack_uses_and_reverifies_exact_signed_hap(self):
        with open(self.signed_hap, "wb") as stream:
            stream.write(b"SIGNED:fresh module output")

        subprocess.run(
            self.command(
                "--java",
                self.fake_java,
                "--sign-tool",
                self.sign_tool,
            ),
            check=True,
        )

        with zipfile.ZipFile(self.signed_app, "r") as archive:
            embedded = archive.read(MEMBER)
            self.assertEqual(archive.read("pac.json"), b"{}")
        self.assertEqual(embedded, b"SIGNED:fresh module output")
        extracted = os.path.join(self.root, "embedded.hap")
        with open(extracted, "wb") as stream:
            stream.write(embedded)
        self.assertEqual(file_digest(extracted), file_digest(self.signed_hap))

    def test_invalid_signed_hap_fails_without_creating_signed_app(self):
        with open(self.signed_hap, "wb") as stream:
            stream.write(b"not signed")
        with open(self.signed_app, "wb") as stream:
            stream.write(b"stale signed app")

        result = subprocess.run(
            self.command(
                "--java",
                self.fake_java,
                "--sign-tool",
                self.sign_tool,
            ),
            check=False,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(os.path.exists(self.signed_app))


if __name__ == "__main__":
    unittest.main()
