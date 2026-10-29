import ast
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[5]
SCRIPT_PATH = REPO_ROOT / "src/sdk_open/ci/scripts/update_sdk_open_docs_changelog.py"


class UpdateSdkOpenDocsChangelogTest(unittest.TestCase):
    def test_remaining_jenkins_release_scripts_use_python36_syntax(self):
        paths = [
            SCRIPT_PATH,
            REPO_ROOT / "src/sdk_open/ci/scripts/sdk_open_release.py",
        ]
        for path in paths:
            with self.subTest(path=str(path)):
                source = path.read_text(encoding="utf-8")
                try:
                    ast.parse(source, filename=str(path), feature_version=(3, 6))
                except TypeError:
                    # Python 3.6 does not expose feature_version; its own parser
                    # is already the compatibility baseline.
                    ast.parse(source, filename=str(path))

    def test_updates_both_changelogs_with_lf_on_python36(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            english_source = root / "CHANGELOG.md"
            chinese_source = root / "CHANGELOG_ZH.md"
            english_target = root / "english.md"
            chinese_target = root / "chinese.md"

            english_source.write_text(
                "# Changelog\n\n## [0.0.7]\n\n- English change\n\n## [0.0.6]\n\n- Old\n",
                encoding="utf-8",
            )
            chinese_source.write_text(
                "# 更新日志\n\n## [0.0.7]\n\n- 中文变更\n\n## [0.0.6]\n\n- 旧版本\n",
                encoding="utf-8",
            )
            english_target.write_bytes(
                b"# Changelog\r\n\r\n## [0.0.6]\r\n\r\n- Old\r\n"
            )
            chinese_target.write_bytes(
                "# 更新日志\r\n\r\n## [0.0.6]\r\n\r\n- 旧版本\r\n".encode("utf-8")
            )

            subprocess.check_call(
                [
                    sys.executable,
                    str(SCRIPT_PATH),
                    "--version",
                    "v0.0.7",
                    "--date",
                    "2026-09-16",
                    "--english-source",
                    str(english_source),
                    "--chinese-source",
                    str(chinese_source),
                    "--english-target",
                    str(english_target),
                    "--chinese-target",
                    str(chinese_target),
                ]
            )

            english_bytes = english_target.read_bytes()
            chinese_bytes = chinese_target.read_bytes()
            self.assertNotIn(b"\r\n", english_bytes)
            self.assertNotIn(b"\r\n", chinese_bytes)
            self.assertIn(
                b"## [v0.0.7] - 2026-09-16\n\n- English change\n\n",
                english_bytes,
            )
            self.assertIn(
                "## [v0.0.7] - 2026-09-16\n\n- 中文变更\n\n".encode("utf-8"),
                chinese_bytes,
            )

    def test_does_not_use_python310_path_write_text_newline(self):
        tree = ast.parse(SCRIPT_PATH.read_text(encoding="utf-8"))
        incompatible_calls = []
        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue
            if not isinstance(node.func, ast.Attribute) or node.func.attr != "write_text":
                continue
            if any(keyword.arg == "newline" for keyword in node.keywords):
                incompatible_calls.append(node.lineno)
        self.assertEqual([], incompatible_calls)


if __name__ == "__main__":
    unittest.main()
