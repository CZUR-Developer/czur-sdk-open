#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[5]
ROOT_CMAKE = REPO_ROOT / "CMakeLists.txt"
REMOTE_WINDOWS_BUILD = (
    REPO_ROOT
    / "src/sdk_open/ci"
    / "scripts"
    / "windows"
    / "remote_sdk_open_build.ps1"
)
SDK_OPEN_VERSION = REPO_ROOT / "src/sdk_open/version/version.ini"


class SdkOpenCmakeIsolationTest(unittest.TestCase):
    def test_isolated_build_uses_sdk_version_without_product_sku(self):
        cmake = ROOT_CMAKE.read_text(encoding="utf-8")
        version_block = re.search(
            r"if\(CZUR_SDK_OPEN_ISOLATED_BUILD\)\s*"
            r"(?P<sdk_branch>.*?)\s*"
            r"else\(\)\s*"
            r"(?P<product_branch>.*?)\s*"
            r"endif\(\)\s*"
            r"# 包含版本解析脚本",
            cmake,
            flags=re.DOTALL,
        )

        self.assertIsNotNone(version_block)
        sdk_branch = version_block.group("sdk_branch")
        product_branch = version_block.group("product_branch")
        self.assertIn(
            'set(VERSION_FILE "${CMAKE_SOURCE_DIR}/src/sdk_open/version/version.ini")',
            sdk_branch,
        )
        self.assertNotIn("CZUR_PRODUCT_SKU", sdk_branch)
        self.assertNotIn("czur_generate_product_config", sdk_branch)
        self.assertIn("CZUR_PRODUCT_SKU", product_branch)
        self.assertIn("czur_generate_product_config", product_branch)
        self.assertIn("message(DEPRECATION", product_branch)

    def test_isolated_build_skips_client_generated_config(self):
        cmake = ROOT_CMAKE.read_text(encoding="utf-8")

        self.assertRegex(
            cmake,
            r"if\(NOT CZUR_SDK_OPEN_ISOLATED_BUILD\)\s*"
            r"include\(\"\$\{CMAKE_SOURCE_DIR\}/cmake/GenerateVipConfig\.cmake\"\)",
        )
        self.assertRegex(
            cmake,
            r"if\(NOT CZUR_SDK_OPEN_ISOLATED_BUILD\)\s*"
            r"include_directories\(\"\$\{CZUR_PRODUCT_GENERATED_DIR\}\"\)",
        )

    def test_windows_sdk_build_does_not_pass_a_client_sku(self):
        remote_build = REMOTE_WINDOWS_BUILD.read_text(encoding="utf-8")

        self.assertIn("'-DBUILD_SDK_OPEN=ON'", remote_build)
        self.assertNotIn("-DCZUR_PRODUCT_SKU", remote_build)

    def test_sdk_open_version_source_exists(self):
        version_text = SDK_OPEN_VERSION.read_text(encoding="utf-8")

        self.assertRegex(version_text, r"(?m)^version=\S+$")
        self.assertRegex(version_text, r"(?m)^build=\d+$")
        self.assertRegex(version_text, r"(?m)^stage=\S+$")


if __name__ == "__main__":
    unittest.main()
