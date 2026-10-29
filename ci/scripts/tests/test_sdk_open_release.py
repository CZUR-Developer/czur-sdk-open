#!/usr/bin/env python3

import hashlib
import importlib.util
import json
import os
import re
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "sdk_open_release.py"
SPEC = importlib.util.spec_from_file_location("sdk_open_release", SCRIPT_PATH)
SDK_OPEN_RELEASE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK_OPEN_RELEASE)


class GiteePreflightTest(unittest.TestCase):
    def setUp(self):
        self.commit = "a" * 40
        patches = [
            mock.patch.dict(os.environ, {
                "GITEE_REPO": "owner/sdk", "GITEE_TOKEN": "test-token",
                "RELEASE_TAG": "v0.0.9",
            }),
            mock.patch.object(SDK_OPEN_RELEASE, "sdk_open_commit", return_value=self.commit),
        ]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)

    def test_available_commit_and_absent_tag_pass(self):
        with mock.patch.object(SDK_OPEN_RELEASE, "http_request", side_effect=[
            (200, json.dumps({"sha": self.commit})), (404, ""), (404, ""),
        ]) as request:
            SDK_OPEN_RELEASE.preflight_gitee()
        self.assertEqual(request.call_count, 3)
        self.assertIn("/repos/owner/sdk/commits/" + self.commit, request.call_args_list[0][0][0])

    def test_missing_commit_fails_before_release_and_tag_checks(self):
        with mock.patch.object(SDK_OPEN_RELEASE, "http_request", return_value=(404, "")) as request:
            with self.assertRaisesRegex(RuntimeError, "Sync the exact src/sdk_open commit"):
                SDK_OPEN_RELEASE.preflight_gitee()
        request.assert_called_once()

    def test_api_errors_are_not_reported_as_missing_commit(self):
        for status in (401, 403, 429, 500):
            with self.subTest(status=status):
                with mock.patch.object(SDK_OPEN_RELEASE, "http_request", return_value=(status, "error")):
                    with self.assertRaisesRegex(RuntimeError, "Failed to check.*HTTP " + str(status)):
                        SDK_OPEN_RELEASE.preflight_gitee()

    def test_invalid_or_mismatched_commit_response_fails(self):
        for body in ("not json", "null", "[]", "{}", json.dumps({"sha": "b" * 40})):
            with self.subTest(body=body):
                with mock.patch.object(SDK_OPEN_RELEASE, "http_request", return_value=(200, body)):
                    with self.assertRaisesRegex(RuntimeError, "Invalid Gitee commit response"):
                        SDK_OPEN_RELEASE.preflight_gitee()

    def test_existing_release_is_still_rejected(self):
        with mock.patch.object(SDK_OPEN_RELEASE, "http_request", side_effect=[
            (200, json.dumps({"sha": self.commit})), (200, '{"id": 123}'),
        ]):
            with self.assertRaisesRegex(RuntimeError, "refusing to overwrite"):
                SDK_OPEN_RELEASE.preflight_gitee()

    def test_existing_tag_with_different_commit_is_still_rejected(self):
        with mock.patch.object(SDK_OPEN_RELEASE, "http_request", return_value=(200, json.dumps({"sha": self.commit}))), \
                mock.patch.object(SDK_OPEN_RELEASE, "check_gitee_release_absent"), \
                mock.patch.object(SDK_OPEN_RELEASE, "resolve_gitee_tag_commit", return_value="b" * 40):
            with self.assertRaisesRegex(RuntimeError, "points to.*expected current"):
                SDK_OPEN_RELEASE.preflight_gitee()


class ReleaseDownloadPlatformTest(unittest.TestCase):
    def test_recognizes_windows_x86_installer_as_32bit(self):
        self.assertEqual(
            ("Windows 32bit / x86", "适用于 Windows 32 位系统。"),
            SDK_OPEN_RELEASE.release_download_platform(
                "sdk-open_Windows_x86_v0.0.4.8.Release.exe"
            ),
        )

    def test_recognizes_windows_x64_installer_as_64bit(self):
        self.assertEqual(
            ("Windows 64bit / x64", "适用于 Windows 64 位系统。"),
            SDK_OPEN_RELEASE.release_download_platform(
                "sdk-open_Windows_x64_v0.0.4.8.Release.exe"
            ),
        )

    def test_recognizes_macos_packages(self):
        self.assertEqual(
            ("macOS Apple Silicon / arm64", "适用于 Apple Silicon 芯片的 macOS 系统。"),
            SDK_OPEN_RELEASE.release_download_platform(
                "sdk-open_macos_arm64_v0.0.6.27.Release.pkg"
            ),
        )
        self.assertEqual(
            ("macOS Intel / x86_64", "适用于 Intel 芯片的 macOS 系统。"),
            SDK_OPEN_RELEASE.release_download_platform(
                "sdk-open_macos_x86_64_v0.0.6.27.Release.pkg"
            ),
        )


class MacosArtifactValidationTest(unittest.TestCase):
    def setUp(self):
        self.parent_commit = "a" * 40
        self.sdk_open_commit = "b" * 40
        self.environment = {
            "SDK_OPEN_VERSION": "0.0.6",
            "SDK_OPEN_BUILD": "27",
            "SDK_OPEN_STAGE": "Release",
            "JENKINS_BUILD_TYPE": "Release",
            "BUILD_COMMIT": self.parent_commit,
        }

    def create_artifacts(
        self,
        artifact_dir,
        package_checksum=None,
        symbol_checksum=None,
        jenkins_manifest_overrides=None,
    ):
        package_name = "sdk-open_macos_arm64_v0.0.6.27.Release.pkg"
        package_path = Path(artifact_dir) / package_name
        package_path.write_bytes(b"signed macOS package")
        symbol_name = package_name[:-4] + ".dSYM.zip"
        symbol_path = Path(artifact_dir) / symbol_name
        symbol_path.write_bytes(b"macOS debug symbols")
        package_digest = package_checksum or SDK_OPEN_RELEASE.sha256_file(
            str(package_path)
        )
        symbol_digest = symbol_checksum or SDK_OPEN_RELEASE.sha256_file(
            str(symbol_path)
        )
        (Path(artifact_dir) / "SHA256SUMS.txt").write_text(
            "{0}  {1}\n{2}  {3}\n".format(
                package_digest, package_name, symbol_digest, symbol_name
            ),
            encoding="utf-8",
        )
        symbols_manifest_name = package_name[:-4] + ".symbols.json"
        symbols_manifest_path = Path(artifact_dir) / symbols_manifest_name
        symbols_manifest_path.write_text(
            json.dumps(
                {
                    "version": "0.0.6",
                    "build": "27",
                    "stage": "Release",
                    "arch": "arm64",
                    "build_type": "Release",
                    "parent_build_commit": self.parent_commit,
                    "sdk_open_commit": self.sdk_open_commit,
                    "package": {"name": package_name, "sha256": package_digest},
                    "symbols": {
                        "name": symbol_name,
                        "sha256": symbol_digest,
                        "binaries": [
                            {"name": name, "uuid": "12345678-1234-1234-1234-123456789ABC"}
                            for name in (
                                "sdk_open_app",
                                "com.czur.device-helper",
                                "libsdk_private_providers.dylib",
                                "libsdk_private_auth_c_api.dylib",
                            )
                        ],
                    },
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        manifest_values = {
            "project": "sdk_open",
            "version": "0.0.6",
            "build": "27",
            "stage": "Release",
            "arch": "arm64",
            "build_type": "Release",
            "git_commit": self.parent_commit,
            "parent_build_commit": self.parent_commit,
            "sdk_open_commit": self.sdk_open_commit,
            "use_parent_build_metadata": "true",
            "package_name": package_name,
            "symbol_archive_name": symbol_name,
            "symbols_manifest_name": symbols_manifest_name,
            "package_sha256": package_digest,
            "symbol_sha256": symbol_digest,
            "symbols_manifest_sha256": SDK_OPEN_RELEASE.sha256_file(
                str(symbols_manifest_path)
            ),
        }
        manifest_values.update(jenkins_manifest_overrides or {})
        (Path(artifact_dir) / "jenkins-sdk-open-macos-build-manifest.txt").write_text(
            "".join("{0}={1}\n".format(key, value) for key, value in manifest_values.items()),
            encoding="utf-8",
        )
        return package_path, symbol_path, symbols_manifest_path

    def validate(self, artifact_dir):
        with mock.patch.dict(os.environ, self.environment, clear=False), mock.patch.object(
            SDK_OPEN_RELEASE, "sdk_open_commit", return_value=self.sdk_open_commit
        ):
            return SDK_OPEN_RELEASE.validate_macos_artifact_directory(
                artifact_dir, "arm64"
            )

    def test_accepts_expected_package_symbols_manifests_and_checksum(self):
        with tempfile.TemporaryDirectory() as artifact_dir:
            package, symbols, symbols_manifest = self.create_artifacts(artifact_dir)
            result = self.validate(artifact_dir)
            self.assertEqual(str(package), result["package"])
            self.assertEqual(str(symbols), result["symbols"])
            self.assertEqual(str(symbols_manifest), result["symbols_manifest"])

    def test_rejects_checksum_with_more_than_the_expected_artifacts(self):
        with tempfile.TemporaryDirectory() as artifact_dir:
            self.create_artifacts(artifact_dir)
            checksum_path = Path(artifact_dir) / "SHA256SUMS.txt"
            checksum_path.write_text(
                checksum_path.read_text(encoding="utf-8")
                + "{0}  unexpected.dSYM.zip\n".format("0" * 64),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "macOS checksum mismatch"):
                self.validate(artifact_dir)

    def test_rejects_invalid_symbol_checksum(self):
        with tempfile.TemporaryDirectory() as artifact_dir:
            self.create_artifacts(artifact_dir, symbol_checksum="0" * 64)
            with self.assertRaisesRegex(RuntimeError, "macOS checksum mismatch"):
                self.validate(artifact_dir)

    def test_rejects_missing_dsym_archive(self):
        with tempfile.TemporaryDirectory() as artifact_dir:
            _, symbol_path, _ = self.create_artifacts(artifact_dir)
            symbol_path.unlink()
            with self.assertRaisesRegex(RuntimeError, "dSYM archive"):
                self.validate(artifact_dir)

    def test_rejects_mismatched_parent_commit_in_jenkins_manifest(self):
        with tempfile.TemporaryDirectory() as artifact_dir:
            self.create_artifacts(
                artifact_dir,
                jenkins_manifest_overrides={"parent_build_commit": "c" * 40},
            )
            with self.assertRaisesRegex(
                RuntimeError, "Jenkins manifest field parent_build_commit mismatch"
            ):
                self.validate(artifact_dir)

    def test_public_macos_asset_list_contains_pkg_only(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                artifact_dir = Path("release-assets/macos/arm64")
                artifact_dir.mkdir(parents=True)
                package_path, symbol_path, _ = self.create_artifacts(artifact_dir)
                environment = dict(
                    self.environment,
                    MACOS_ARCH="arm64",
                    MACOS_ARTIFACT_OSS_DIR="oss://test/development/macos/",
                )
                with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
                    SDK_OPEN_RELEASE, "run"
                ), mock.patch.object(
                    SDK_OPEN_RELEASE,
                    "sdk_open_commit",
                    return_value=self.sdk_open_commit,
                ):
                    assets = SDK_OPEN_RELEASE.prepare_macos_assets()

                self.assertEqual([package_path.name], assets)
                self.assertTrue(Path(package_path.name).is_file())
                self.assertFalse(Path(symbol_path.name).exists())
            finally:
                os.chdir(previous_dir)

    def test_maps_parent_architecture_values_to_child_artifact_names(self):
        for value, expected in (
            ("arm64", ["arm64"]),
            ("x86-64", ["x86_64"]),
            ("all", ["arm64", "x86_64"]),
        ):
            with self.subTest(value=value), mock.patch.dict(
                os.environ, {"MACOS_ARCH": value}, clear=False
            ):
                self.assertEqual(expected, SDK_OPEN_RELEASE.macos_architectures())


class WriteReleaseMetadataTest(unittest.TestCase):
    def test_writes_platform_specific_markdown_download_links(self):
        assets = [
            "sdk-open_0.0.3.7.Beta_amd64_common.deb",
            "sdk-open_0.0.3.7.Beta_arm64_common.deb",
            "sdk-open_Windows_x86_v0.0.3.7.Beta.exe",
            "sdk-open_Windows_x64_v0.0.3.7.Beta.exe",
            "sdk-open_macos_arm64_v0.0.3.7.Beta.pkg",
            "sdk-open_macos_x86_64_v0.0.3.7.Beta.pkg",
        ]
        base_url = (
            "https://release.czur.com/sdk-open/Package/Release/"
            "v0.0.3-20260710_1631-official-release-0.0.3_7_Beta_all/"
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                for asset in assets:
                    Path(asset).write_bytes(asset.encode("utf-8"))
                Path("release_note.md").write_text("# SDK Open\n", encoding="utf-8")

                with mock.patch.dict(
                    os.environ,
                    {
                        "RELEASE_DOWNLOAD_URL": base_url,
                        "OSS_RELEASE_DIR": "oss://test/release/",
                        "RELEASE_SAVE_DIR_NAME": "test-release",
                        "RELEASE_TAG": "v0.0.3",
                    },
                    clear=False,
                ):
                    SDK_OPEN_RELEASE.write_release_metadata(assets)

                downloads = Path("release-downloads.md").read_text(encoding="utf-8")
            finally:
                os.chdir(previous_dir)

        expected_downloads = """## 下载地址

1. Linux x86 64bit / amd64{br}
  适用于麒麟 Kylin、统信 UOS、Ubuntu 等 Debian 系 Linux 系统，CPU 为 Intel / AMD 64 位架构。{br}
  下载：[sdk-open_0.0.3.7.Beta_amd64_common.deb]({0}sdk-open_0.0.3.7.Beta_amd64_common.deb)

2. Linux ARM 64bit / arm64 / aarch64{br}
  适用于麒麟 Kylin、统信 UOS、Ubuntu 等 Debian 系 Linux 系统，CPU 为 ARM 64 位架构。{br}
  下载：[sdk-open_0.0.3.7.Beta_arm64_common.deb]({0}sdk-open_0.0.3.7.Beta_arm64_common.deb)

3. Windows 32bit / x86{br}
  适用于 Windows 32 位系统。{br}
  下载：[sdk-open_Windows_x86_v0.0.3.7.Beta.exe]({0}sdk-open_Windows_x86_v0.0.3.7.Beta.exe)

4. Windows 64bit / x64{br}
  适用于 Windows 64 位系统。{br}
  下载：[sdk-open_Windows_x64_v0.0.3.7.Beta.exe]({0}sdk-open_Windows_x64_v0.0.3.7.Beta.exe)

5. macOS Apple Silicon / arm64{br}
  适用于 Apple Silicon 芯片的 macOS 系统。{br}
  下载：[sdk-open_macos_arm64_v0.0.3.7.Beta.pkg]({0}sdk-open_macos_arm64_v0.0.3.7.Beta.pkg)

6. macOS Intel / x86_64{br}
  适用于 Intel 芯片的 macOS 系统。{br}
  下载：[sdk-open_macos_x86_64_v0.0.3.7.Beta.pkg]({0}sdk-open_macos_x86_64_v0.0.3.7.Beta.pkg)

## SHA256 校验
""".format(base_url, br="  ")
        self.assertIn(expected_downloads, downloads)


class RuntimeManifestGenerationTest(unittest.TestCase):
    def setUp(self):
        self.version = "0.0.6+rc"
        self.build = "27"
        self.stage = "Release"
        self.base_url = "https://release.example/sdk-open/release-27/"
        self.environment = {
            "SDK_OPEN_VERSION": self.version,
            "SDK_OPEN_BUILD": self.build,
            "SDK_OPEN_STAGE": self.stage,
            "SDK_OPEN_DISTRO": "common",
            "RELEASE_TAG": "v0.0.6",
            "OSS_RELEASE_DIR": "oss://test/release/release-27/",
            "RELEASE_DOWNLOAD_URL": self.base_url,
            "RUN_LINUX_SDK_OPEN": "true",
            "SDK_OPEN_LINUX_ARCHES": "x86-64,arm64",
            "RUN_WINDOWS_SDK_OPEN": "true",
            "WINDOWS_ARCH": "all",
            "RUN_MACOS_SDK_OPEN": "true",
            "MACOS_ARCH": "all",
        }
        metadata = "{0}.{1}.{2}".format(
            self.version, self.build, self.stage
        )
        self.asset_names = [
            "sdk-open_{0}_amd64_common.deb".format(metadata),
            "sdk-open_{0}_arm64_common.deb".format(metadata),
            "sdk-open_Windows_x86_v{0}.exe".format(metadata),
            "sdk-open_Windows_x64_v{0}.exe".format(metadata),
            "sdk-open_macos_arm64_v{0}.pkg".format(metadata),
            "sdk-open_macos_x86_64_v{0}.pkg".format(metadata),
        ]

    def write_inputs(self, asset_names=None, checksum_overrides=None):
        names = list(asset_names if asset_names is not None else self.asset_names)
        checksum_overrides = checksum_overrides or {}
        for index, name in enumerate(names):
            Path(name).write_bytes(("artifact-{0}-{1}".format(index, name)).encode("utf-8"))
        assets = [
            {
                "name": name,
                "url": self.base_url
                + SDK_OPEN_RELEASE.urllib.parse.quote(name, safe=""),
            }
            for name in reversed(names)
        ]
        Path("release_manifest.json").write_text(
            json.dumps(
                {
                    "release_tag": self.environment["RELEASE_TAG"],
                    "oss_release_dir": self.environment["OSS_RELEASE_DIR"],
                    "release_download_url": self.base_url,
                    "assets": assets,
                }
            ),
            encoding="utf-8",
        )
        checksum_lines = []
        for name in names:
            digest = checksum_overrides.get(
                name, SDK_OPEN_RELEASE.sha256_file(name)
            )
            checksum_lines.append("{0}  {1}".format(digest, name))
        Path("SHA256SUMS.txt").write_text(
            "\n".join(checksum_lines) + "\n", encoding="utf-8"
        )

    def generate(self, generated_at="2026-09-15T01:02:03Z"):
        return SDK_OPEN_RELEASE.generate_runtime_manifest(
            generated_at=generated_at
        )

    def test_maps_all_platforms_in_stable_order_with_hashes_and_urls(self):
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ, self.environment, clear=False
        ):
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                self.write_inputs()
                manifest = self.generate()
                serialized = Path("manifest.json").read_bytes()
            finally:
                os.chdir(previous_dir)

        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("stable", manifest["channel"])
        self.assertEqual("2026-09-15T01:02:03Z", manifest["generated_at"])
        self.assertEqual(1, len(manifest["releases"]))
        release = manifest["releases"][0]
        self.assertEqual(self.version, release["version"])
        self.assertEqual(["2.0.0"], release["supported_protocol_versions"])
        self.assertEqual(
            [
                ("linux", "x64"),
                ("linux", "arm64"),
                ("windows", "x86"),
                ("windows", "x64"),
                ("macos", "arm64"),
                ("macos", "x86_64"),
            ],
            [(item["os"], item["arch"]) for item in release["artifacts"]],
        )
        for artifact in release["artifacts"]:
            self.assertEqual(
                len(("artifact-{0}-{1}".format(
                    self.asset_names.index(artifact["filename"]),
                    artifact["filename"],
                )).encode("utf-8")),
                artifact["size_bytes"],
            )
            self.assertRegex(artifact["sha256"], r"^[0-9a-f]{64}$")
            self.assertIn("%2Brc", artifact["url"])
            if artifact["os"] == "linux":
                self.assertEqual("debian", artifact["os_family"])
            else:
                self.assertNotIn("os_family", artifact)
        self.assertTrue(serialized.endswith(b"\n"))
        self.assertEqual(
            (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8"),
            serialized,
        )

    def test_generated_timestamp_is_utc_iso8601(self):
        self.assertRegex(
            SDK_OPEN_RELEASE.utc_iso8601_now(),
            r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$",
        )

    def test_accepts_the_selected_linux_distro_suffix(self):
        environment = dict(self.environment, SDK_OPEN_DISTRO="ubuntu")
        with mock.patch.dict(os.environ, environment, clear=False):
            platform = SDK_OPEN_RELEASE.runtime_artifact_platform(
                "sdk-open_{0}.{1}.{2}_amd64_ubuntu.deb".format(
                    self.version, self.build, self.stage
                )
            )
        self.assertEqual(
            {
                "os": "linux",
                "arch": "x64",
                "os_family": "debian",
                "format": "deb",
            },
            platform,
        )

    def test_rejects_missing_local_artifact(self):
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ, self.environment, clear=False
        ):
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                self.write_inputs()
                Path(self.asset_names[0]).unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing local"):
                    self.generate()
            finally:
                os.chdir(previous_dir)

    def test_rejects_duplicate_platform_architecture(self):
        duplicate = self.asset_names[1].replace("_arm64_", "_aarch64_")
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ, self.environment, clear=False
        ):
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                self.write_inputs(self.asset_names + [duplicate])
                with self.assertRaisesRegex(RuntimeError, "Duplicate Runtime Manifest"):
                    self.generate()
            finally:
                os.chdir(previous_dir)

    def test_rejects_unknown_architecture(self):
        invalid = self.asset_names[2].replace("Windows_x86", "Windows_arm64")
        names = [invalid if name == self.asset_names[2] else name for name in self.asset_names]
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ, self.environment, clear=False
        ):
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                self.write_inputs(names)
                with self.assertRaisesRegex(RuntimeError, "Unsupported Windows"):
                    self.generate()
            finally:
                os.chdir(previous_dir)

    def test_rejects_unknown_artifact_filename(self):
        invalid = "scanner_{0}.{1}.{2}_amd64_common.deb".format(
            self.version, self.build, self.stage
        )
        names = [invalid if name == self.asset_names[0] else name for name in self.asset_names]
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ, self.environment, clear=False
        ):
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                self.write_inputs(names)
                with self.assertRaisesRegex(RuntimeError, "Unrecognized Runtime Manifest"):
                    self.generate()
            finally:
                os.chdir(previous_dir)

    def test_rejects_filename_version_build_or_stage_mismatch(self):
        invalid = self.asset_names[3].replace(".27.Release", ".26.Beta")
        names = [invalid if name == self.asset_names[3] else name for name in self.asset_names]
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ, self.environment, clear=False
        ):
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                self.write_inputs(names)
                with self.assertRaisesRegex(RuntimeError, "version/build/stage mismatch"):
                    self.generate()
            finally:
                os.chdir(previous_dir)

    def test_rejects_checksum_mismatch(self):
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ, self.environment, clear=False
        ):
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                self.write_inputs(
                    checksum_overrides={self.asset_names[0]: "0" * 64}
                )
                with self.assertRaisesRegex(RuntimeError, "SHA256 mismatch"):
                    self.generate()
            finally:
                os.chdir(previous_dir)

    def test_rejects_missing_requested_platform_artifact(self):
        names = self.asset_names[:-1]
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ, self.environment, clear=False
        ):
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                self.write_inputs(names)
                with self.assertRaisesRegex(RuntimeError, "artifact set mismatch"):
                    self.generate()
            finally:
                os.chdir(previous_dir)


class DevelopmentStagingCleanupTest(unittest.TestCase):
    def setUp(self):
        self.environment = {
            "PUBLISH_OSS_RELEASE": "true",
            "RUN_WINDOWS_SDK_OPEN": "true",
            "WINDOWS_ARCH": "all",
            "OSS_ARTIFACT_DIR": "oss://test/development/",
            "WINDOWS_ARTIFACT_OSS_PREFIX_DIR": "oss://test/development/windows",
            "RUN_MACOS_SDK_OPEN": "true",
            "MACOS_ARCH": "all",
            "MACOS_ARTIFACT_OSS_DIR": "oss://test/development/macos/",
            "OSS_RELEASE_DIR": "oss://test/release/",
            "OSS_SYMBOL_DIR": "oss://test/symbols/",
            "SDK_OPEN_VERSION": "0.0.6",
            "SDK_OPEN_BUILD": "27",
            "SDK_OPEN_STAGE": "Release",
        }
        self.asset_names = [
            "sdk-open_Windows_x86_v0.0.6.27.Release.exe",
            "sdk-open_Windows_x64_v0.0.6.27.Release.exe",
            "sdk-open_macos_arm64_v0.0.6.27.Release.pkg",
            "sdk-open_macos_x86_64_v0.0.6.27.Release.pkg",
        ]

    def test_builds_exact_cleanup_plan_for_requested_platforms(self):
        with mock.patch.dict(os.environ, self.environment, clear=False):
            release_objects, development_objects = (
                SDK_OPEN_RELEASE.development_staging_cleanup_plan(self.asset_names)
            )

        self.assertEqual(
            [
                "oss://test/release/sdk-open_Windows_x86_v0.0.6.27.Release.exe",
                "oss://test/release/sdk-open_Windows_x64_v0.0.6.27.Release.exe",
                "oss://test/release/sdk-open_macos_arm64_v0.0.6.27.Release.pkg",
                "oss://test/symbols/macos/arm64/sdk-open_macos_arm64_v0.0.6.27.Release.dSYM.zip",
                "oss://test/release/sdk-open_macos_x86_64_v0.0.6.27.Release.pkg",
                "oss://test/symbols/macos/x86_64/sdk-open_macos_x86_64_v0.0.6.27.Release.dSYM.zip",
            ],
            release_objects,
        )
        self.assertEqual(
            [
                "oss://test/development/windows/sdk-open_Windows_x86_v0.0.6.27.Release.exe",
                "oss://test/development/windows/sdk-open_Windows_x64_v0.0.6.27.Release.exe",
                "oss://test/development/windows/SHA256SUMS.txt",
                "oss://test/development/macos/arm64/sdk-open_macos_arm64_v0.0.6.27.Release.pkg",
                "oss://test/development/macos/arm64/sdk-open_macos_arm64_v0.0.6.27.Release.dSYM.zip",
                "oss://test/development/macos/arm64/SHA256SUMS.txt",
                "oss://test/development/macos/x86_64/sdk-open_macos_x86_64_v0.0.6.27.Release.pkg",
                "oss://test/development/macos/x86_64/sdk-open_macos_x86_64_v0.0.6.27.Release.dSYM.zip",
                "oss://test/development/macos/x86_64/SHA256SUMS.txt",
            ],
            development_objects,
        )

    def test_cleanup_refuses_to_delete_when_release_asset_is_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                SDK_OPEN_RELEASE.write_text(
                    "release_manifest.json",
                    json.dumps(
                        {
                            "oss_release_dir": self.environment["OSS_RELEASE_DIR"],
                            "assets": [
                                {"name": name, "url": "https://example/" + name}
                                for name in self.asset_names
                            ],
                        }
                    ),
                )
                with mock.patch.dict(os.environ, self.environment, clear=False), mock.patch.object(
                    SDK_OPEN_RELEASE, "oss_object_exists", return_value=False
                ), mock.patch.object(SDK_OPEN_RELEASE, "run") as run_command:
                    with self.assertRaisesRegex(RuntimeError, "required published asset is missing"):
                        SDK_OPEN_RELEASE.cleanup_development_staging()
                    run_command.assert_not_called()
            finally:
                os.chdir(previous_dir)

    def test_cleanup_refuses_to_delete_when_formal_dsym_is_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                SDK_OPEN_RELEASE.write_text(
                    "release_manifest.json",
                    json.dumps(
                        {
                            "oss_release_dir": self.environment["OSS_RELEASE_DIR"],
                            "assets": [
                                {"name": name, "url": "https://example/" + name}
                                for name in self.asset_names
                            ],
                        }
                    ),
                )
                with mock.patch.dict(os.environ, self.environment, clear=False):
                    required_objects, _ = (
                        SDK_OPEN_RELEASE.development_staging_cleanup_plan(
                            self.asset_names
                        )
                    )
                    missing_symbol = next(
                        uri for uri in required_objects if "/symbols/macos/" in uri
                    )

                    def object_exists(uri, _config):
                        return uri != missing_symbol

                    with mock.patch.object(
                        SDK_OPEN_RELEASE,
                        "oss_object_exists",
                        side_effect=object_exists,
                    ), mock.patch.object(SDK_OPEN_RELEASE, "run") as run_command:
                        with self.assertRaisesRegex(
                            RuntimeError, re.escape(missing_symbol)
                        ):
                            SDK_OPEN_RELEASE.cleanup_development_staging()
                        run_command.assert_not_called()
            finally:
                os.chdir(previous_dir)

    def test_cleanup_removes_only_development_packages_and_checksums(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                SDK_OPEN_RELEASE.write_text(
                    "release_manifest.json",
                    json.dumps(
                        {
                            "oss_release_dir": self.environment["OSS_RELEASE_DIR"],
                            "assets": [
                                {"name": name, "url": "https://example/" + name}
                                for name in self.asset_names
                            ],
                        }
                    ),
                )
                with mock.patch.dict(os.environ, self.environment, clear=False):
                    release_objects, development_objects = (
                        SDK_OPEN_RELEASE.development_staging_cleanup_plan(
                            self.asset_names
                        )
                    )
                    existing = set(release_objects + development_objects)

                    def object_exists(uri, _config):
                        return uri in existing

                    def remove_object(args, **_kwargs):
                        self.assertEqual("ossutil", args[0])
                        self.assertEqual("rm", args[1])
                        existing.remove(args[2])

                    with mock.patch.object(
                        SDK_OPEN_RELEASE, "oss_object_exists", side_effect=object_exists
                    ), mock.patch.object(
                        SDK_OPEN_RELEASE, "run", side_effect=remove_object
                    ) as run_command:
                        SDK_OPEN_RELEASE.cleanup_development_staging()
                        first_cleanup_call_count = run_command.call_count
                        SDK_OPEN_RELEASE.cleanup_development_staging()
                        self.assertEqual(first_cleanup_call_count, run_command.call_count)

                # unittest.mock.call.args is only available on newer Python;
                # Jenkins uses Python 3.6, where calls expose (args, kwargs).
                removed = [call[0][0][2] for call in run_command.call_args_list]
                self.assertTrue(all("/development/" in uri for uri in removed))
                self.assertTrue(
                    all(
                        uri.endswith((".exe", ".pkg", ".dSYM.zip", "/SHA256SUMS.txt"))
                        for uri in removed
                    )
                )
            finally:
                os.chdir(previous_dir)

    def test_cleanup_rejects_unexpected_development_directory(self):
        environment = dict(
            self.environment,
            WINDOWS_ARTIFACT_OSS_PREFIX_DIR="oss://test/release/windows",
        )
        with mock.patch.dict(os.environ, environment, clear=False):
            with self.assertRaisesRegex(RuntimeError, "unexpected Windows"):
                SDK_OPEN_RELEASE.development_staging_cleanup_plan(self.asset_names)

    def test_cleanup_rejects_non_release_build(self):
        environment = dict(self.environment, PUBLISH_OSS_RELEASE="false")
        with mock.patch.dict(os.environ, environment, clear=False):
            with self.assertRaisesRegex(RuntimeError, "non-release build"):
                SDK_OPEN_RELEASE.cleanup_development_staging()


class MacosSymbolsPublishTest(unittest.TestCase):
    def test_uploads_only_private_symbol_archive_checksum_and_manifest(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            previous_dir = os.getcwd()
            try:
                os.chdir(temp_dir)
                source_dir = Path(temp_dir) / "source"
                source_dir.mkdir()
                symbol_name = (
                    "sdk-open_macos_arm64_v0.0.6.27.Release.dSYM.zip"
                )
                symbol_path = source_dir / symbol_name
                symbol_path.write_bytes(b"macOS debug symbols")
                symbol_sha256 = SDK_OPEN_RELEASE.sha256_file(str(symbol_path))
                manifest_data = {
                    "arch": "arm64",
                    "package": {
                        "name": "sdk-open_macos_arm64_v0.0.6.27.Release.pkg",
                        "sha256": "a" * 64,
                    },
                    "symbols": {
                        "name": symbol_name,
                        "sha256": symbol_sha256,
                        "binaries": [],
                    },
                }
                record = {
                    "architecture": "arm64",
                    "symbols": str(symbol_path),
                    "symbol_sha256": symbol_sha256,
                    "symbols_manifest_data": manifest_data,
                }
                environment = {
                    "OSS_SYMBOL_DIR": "oss://test/symbols/release-1/",
                    "OSS_ARTIFACT_DIR": "oss://test/development/build-1/",
                    "OSS_RELEASE_DIR": "oss://test/release/release-1/",
                }
                uploaded = set()

                def object_exists(uri, _config):
                    return uri in uploaded

                def upload(args, **_kwargs):
                    self.assertEqual(["ossutil", "cp"], args[:2])
                    uploaded.add(args[3] + os.path.basename(args[2]))

                with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
                    SDK_OPEN_RELEASE,
                    "oss_object_exists",
                    side_effect=object_exists,
                ), mock.patch.object(
                    SDK_OPEN_RELEASE, "run", side_effect=upload
                ):
                    SDK_OPEN_RELEASE.publish_macos_symbols([record])

                target_dir = "oss://test/symbols/release-1/macos/arm64/"
                self.assertEqual(
                    {
                        target_dir + symbol_name,
                        target_dir + "SHA256SUMS.txt",
                        target_dir + "symbols_manifest.json",
                    },
                    uploaded,
                )
                self.assertFalse(any(uri.endswith(".pkg") for uri in uploaded))
                checksum = Path(
                    "release-assets/macos-symbols/arm64/SHA256SUMS.txt"
                ).read_text(encoding="utf-8")
                self.assertEqual(
                    "{0}  {1}\n".format(symbol_sha256, symbol_name), checksum
                )
            finally:
                os.chdir(previous_dir)


class GithubAssetUploadTest(unittest.TestCase):
    def test_uses_extended_timeout_for_asset_upload(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            asset = Path(temp_dir) / "sdk-open.deb"
            asset.write_bytes(b"release-asset")

            with mock.patch.object(
                SDK_OPEN_RELEASE,
                "http_request",
                return_value=(201, "{}"),
            ) as request:
                SDK_OPEN_RELEASE.upload_github_asset(
                    "https://uploads.github.com/repos/test/sdk/releases/1/assets",
                    "test-token",
                    str(asset),
                    "https://api.github.com/repos/test/sdk/releases/1",
                )

        self.assertEqual(
            SDK_OPEN_RELEASE.GITHUB_ASSET_UPLOAD_TIMEOUT_SECONDS,
            request.call_args[1]["timeout"],
        )
        self.assertEqual(600, request.call_args[1]["timeout"])

    def run_upload(self, responses):
        with tempfile.TemporaryDirectory() as directory:
            asset = Path(directory) / "sdk-open.deb"
            asset.write_bytes(b"release-asset")
            with mock.patch.object(SDK_OPEN_RELEASE, "http_request", side_effect=responses) as request, mock.patch.object(SDK_OPEN_RELEASE.time, "sleep"):
                SDK_OPEN_RELEASE.upload_github_asset(
                    "https://uploads.github.com/repos/test/sdk/releases/1/assets",
                    "test-token", str(asset), "https://api.github.com/repos/test/sdk/releases/1",
                )
            return request.call_args_list

    def test_timeout_then_retry(self):
        calls = self.run_upload([
            SDK_OPEN_RELEASE.urllib.error.URLError("write timed out"),
            (200, "[]"), (201, "{}"),
        ])
        self.assertEqual([c.kwargs.get("method", "GET") for c in calls], ["POST", "GET", "POST"])

    def test_server_failure_cleans_starter_before_retry(self):
        calls = self.run_upload([
            (502, "bad gateway"),
            (200, json.dumps([{"id": 42, "name": "sdk-open.deb", "state": "starter"}])),
            (204, ""), (201, "{}"),
        ])
        self.assertEqual(calls[2].args[0], "https://api.github.com/repos/test/sdk/releases/assets/42")
        self.assertEqual(calls[2].kwargs["method"], "DELETE")

    def test_lost_response_recognizes_completed_upload(self):
        remote = {"id": 42, "name": "sdk-open.deb", "state": "uploaded",
                  "size": 13, "digest": "sha256:" + hashlib.sha256(b"release-asset").hexdigest()}
        calls = self.run_upload([SDK_OPEN_RELEASE.socket.timeout("timeout"), (200, json.dumps([remote]))])
        self.assertEqual(len(calls), 2)

    def test_refuses_completed_unverified_asset(self):
        remote = {"id": 42, "name": "sdk-open.deb", "state": "uploaded", "size": 13}
        with self.assertRaisesRegex(RuntimeError, "Refusing to overwrite"):
            self.run_upload([(503, "unavailable"), (200, json.dumps([remote]))])

    def test_permission_failure_is_not_retried(self):
        with self.assertRaisesRegex(RuntimeError, "HTTP 403"):
            self.run_upload([(403, "forbidden")])

    def test_exhaustion_checks_remote_after_last_attempt(self):
        with self.assertRaisesRegex(RuntimeError, "bounded retries"):
            self.run_upload([(503, "busy"), (200, "[]")] * 5)

    def test_reconciliation_paginates(self):
        with mock.patch.object(SDK_OPEN_RELEASE, "http_request", side_effect=[
            (200, json.dumps([{"name": "other"}] * 100)), (200, "[]")
        ]) as request:
            self.assertFalse(SDK_OPEN_RELEASE.reconcile_github_asset(
                "https://api.github.com/repos/test/sdk/releases/1", {}, "sdk-open.deb", b"data"))
        self.assertTrue(request.call_args.args[0].endswith("page=2"))

    def test_reconciliation_service_failure_is_retried(self):
        calls = self.run_upload([(503, "busy"), (502, "busy"),
                                 (200, "[]"), (201, "{}")])
        self.assertEqual([c.kwargs.get("method", "GET") for c in calls],
                         ["POST", "GET", "GET", "POST"])

    def test_failed_cleanup_does_not_upload_over_placeholder(self):
        with self.assertRaisesRegex(RuntimeError, "Cannot remove incomplete"):
            self.run_upload([(502, "busy"), (200, json.dumps([
                {"id": 42, "name": "sdk-open.deb", "state": "starter"}
            ])), (403, "forbidden")])

    def test_final_reconciliation_can_recover_fifth_upload(self):
        remote = {"name": "sdk-open.deb", "state": "uploaded", "size": 13,
                  "digest": "sha256:" + hashlib.sha256(b"release-asset").hexdigest()}
        calls = self.run_upload([(503, "busy"), (200, "[]")] * 4 + [
                                 SDK_OPEN_RELEASE.socket.timeout("timeout"),
                                 (200, json.dumps([remote]))])
        self.assertEqual(sum(c.kwargs.get("method") == "POST" for c in calls), 5)


if __name__ == "__main__":
    unittest.main()
