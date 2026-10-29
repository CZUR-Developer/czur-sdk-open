#!/usr/bin/env python3
"""SDK Open release helpers used by Jenkins.

Keep heavy release logic out of Jenkinsfile to avoid Groovy CPS method-size
limits.
"""

from __future__ import print_function

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import socket
import http.client
import time
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request


HTTP_REQUEST_TIMEOUT_SECONDS = 60
GITHUB_ASSET_UPLOAD_TIMEOUT_SECONDS = 600
GITHUB_ASSET_UPLOAD_MAX_ATTEMPTS = 5
RUNTIME_MANIFEST_PROTOCOL_VERSIONS = ["2.0.0"]
RUNTIME_ARTIFACT_ORDER = {
    ("linux", "x64"): 0,
    ("linux", "arm64"): 1,
    ("windows", "x86"): 2,
    ("windows", "x64"): 3,
    ("macos", "arm64"): 4,
    ("macos", "x86_64"): 5,
}


def env(name, default=""):
    return os.environ.get(name, default)


def require_env(name):
    value = env(name).strip()
    if not value:
        raise RuntimeError("{0} is required".format(name))
    return value


def run(args, **kwargs):
    print("+ {0}".format(" ".join(args)))
    subprocess.check_call(args, **kwargs)


def output(args):
    return subprocess.check_output(args, universal_newlines=True).strip()


def read_text(path):
    with open(path, "r", encoding="utf-8") as file_obj:
        return file_obj.read()


def write_text(path, content):
    with open(path, "w", encoding="utf-8", newline="\n") as file_obj:
        file_obj.write(content)


def http_request(
    url,
    method="GET",
    headers=None,
    data=None,
    timeout=HTTP_REQUEST_TIMEOUT_SECONDS,
):
    request = urllib.request.Request(
        url,
        data=data,
        headers=headers or {},
        method=method,
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8", "replace")
            return response.getcode(), body
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        return exc.code, body


def quote_path(value):
    return urllib.parse.quote(value, safe="")


def sdk_open_commit():
    commit = output(["git", "-C", "src/sdk_open", "rev-parse", "HEAD"])
    if not commit:
        raise RuntimeError("Failed to resolve src/sdk_open commit")
    return commit


def github_headers():
    return {
        "Authorization": "Bearer {0}".format(require_env("GITHUB_TOKEN")),
        "Accept": "application/vnd.github+json",
    }


def resolve_github_tag_commit(repo_api_url, release_tag, headers):
    status, body = http_request(
        "{0}/git/ref/tags/{1}".format(repo_api_url, quote_path(release_tag)),
        headers=headers,
    )
    if status == 404:
        return None
    if status != 200:
        raise RuntimeError(
            "Failed to check GitHub tag {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )

    ref = json.loads(body)
    object_type = ref.get("object", {}).get("type", "")
    object_sha = ref.get("object", {}).get("sha", "")
    if object_type == "commit":
        return object_sha
    if object_type != "tag":
        raise RuntimeError(
            "GitHub tag {0} has unsupported object type: {1}\n{2}".format(
                release_tag, object_type, body
            )
        )

    status, body = http_request(
        "{0}/git/tags/{1}".format(repo_api_url, object_sha),
        headers=headers,
    )
    if status != 200:
        raise RuntimeError(
            "Failed to read annotated GitHub tag object {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )
    tag_obj = json.loads(body)
    return tag_obj.get("object", {}).get("sha", "")


def preflight_github():
    repo = require_env("GITHUB_REPO")
    release_tag = require_env("RELEASE_TAG")
    expected_commit = sdk_open_commit()
    repo_api_url = "https://api.github.com/repos/{0}".format(repo)
    headers = github_headers()

    status, body = http_request(
        "{0}/releases/tags/{1}".format(repo_api_url, quote_path(release_tag)),
        headers=headers,
    )
    if status == 200:
        raise RuntimeError(
            "GitHub Release {0} already exists in {1}; refusing to overwrite.\n{2}".format(
                release_tag, repo, body
            )
        )
    if status != 404:
        raise RuntimeError(
            "Failed to check GitHub Release {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )

    tag_commit = resolve_github_tag_commit(repo_api_url, release_tag, headers)
    if tag_commit is None:
        print(
            "GitHub tag {0} does not exist yet; release creation may create it at {1}.".format(
                release_tag, expected_commit
            )
        )
    elif tag_commit != expected_commit:
        raise RuntimeError(
            "GitHub tag {0} points to {1}, expected current src/sdk_open commit {2}.".format(
                release_tag, tag_commit, expected_commit
            )
        )
    else:
        print(
            "GitHub tag {0} already exists and points to current src/sdk_open commit {1}.".format(
                release_tag, expected_commit
            )
        )

    print("GitHub Release {0} does not exist yet.".format(release_tag))


def gitee_repo_parts():
    repo = require_env("GITEE_REPO")
    parts = repo.split("/", 1)
    if len(parts) != 2 or not parts[0] or not parts[1]:
        raise RuntimeError("GITEE_REPO must use owner/repo format")
    return repo, parts[0], parts[1]


def gitee_url(path, params=None):
    token = require_env("GITEE_TOKEN")
    query = {"access_token": token}
    if params:
        query.update(params)
    return "https://gitee.com/api/v5/{0}?{1}".format(
        path.lstrip("/"), urllib.parse.urlencode(query)
    )


def resolve_gitee_tag_commit(repo_api_path, release_tag):
    status, body = http_request(
        gitee_url("{0}/git/refs/tags/{1}".format(repo_api_path, quote_path(release_tag)))
    )
    if status == 404:
        return None
    if status != 200:
        raise RuntimeError(
            "Failed to check Gitee tag {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )

    ref = json.loads(body)
    object_type = ref.get("object", {}).get("type", "")
    object_sha = ref.get("object", {}).get("sha", "")
    if object_type == "commit":
        return object_sha
    if object_type != "tag":
        raise RuntimeError(
            "Gitee tag {0} has unsupported object type: {1}\n{2}".format(
                release_tag, object_type, body
            )
        )

    status, body = http_request(
        gitee_url("{0}/git/tags/{1}".format(repo_api_path, object_sha))
    )
    if status != 200:
        raise RuntimeError(
            "Failed to read annotated Gitee tag object {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )
    tag_obj = json.loads(body)
    return tag_obj.get("object", {}).get("sha", "")


def check_gitee_release_absent(repo, repo_api_path, release_tag):
    status, body = http_request(
        gitee_url("{0}/releases/tags/{1}".format(repo_api_path, quote_path(release_tag)))
    )
    if status == 404:
        return
    if status == 200:
        try:
            release = json.loads(body)
        except ValueError:
            release = body
        if release is None:
            return
        raise RuntimeError(
            "Gitee Release {0} already exists in {1}; refusing to overwrite.\n{2}".format(
                release_tag, repo, body
            )
        )
    raise RuntimeError(
        "Failed to check Gitee Release {0}, HTTP {1}\n{2}".format(
            release_tag, status, body
        )
    )


def check_gitee_target_commit(repo, repo_api_path, expected_commit):
    status, body = http_request(
        gitee_url("{0}/commits/{1}".format(repo_api_path, quote_path(expected_commit)))
    )
    if status == 404:
        raise RuntimeError(
            "Gitee target commit {0} is not accessible in {1} (HTTP 404). "
            "Sync the exact src/sdk_open commit to this Gitee repository before retrying; "
            "also verify GITEE_REPO and token access.".format(expected_commit, repo)
        )
    if status != 200:
        raise RuntimeError(
            "Failed to check Gitee target commit {0} in {1}, HTTP {2}\n{3}".format(
                expected_commit, repo, status, body
            )
        )
    try:
        commit = json.loads(body)
    except ValueError:
        commit = None
    if not isinstance(commit, dict) or commit.get("sha") != expected_commit:
        raise RuntimeError(
            "Invalid Gitee commit response for {0} in {1}: "
            "expected the exact src/sdk_open commit SHA.".format(expected_commit, repo)
        )
    print("Gitee target commit {0} is available in {1}.".format(expected_commit, repo))


def preflight_gitee():
    repo, owner, repo_name = gitee_repo_parts()
    release_tag = require_env("RELEASE_TAG")
    expected_commit = sdk_open_commit()
    repo_api_path = "repos/{0}/{1}".format(quote_path(owner), quote_path(repo_name))

    check_gitee_target_commit(repo, repo_api_path, expected_commit)
    check_gitee_release_absent(repo, repo_api_path, release_tag)

    tag_commit = resolve_gitee_tag_commit(repo_api_path, release_tag)
    if tag_commit is None:
        print(
            "Gitee tag {0} does not exist yet; release creation may create it at {1}.".format(
                release_tag, expected_commit
            )
        )
    elif tag_commit != expected_commit:
        raise RuntimeError(
            "Gitee tag {0} points to {1}, expected current src/sdk_open commit {2}.".format(
                release_tag, tag_commit, expected_commit
            )
        )
    else:
        print(
            "Gitee tag {0} already exists and points to current src/sdk_open commit {1}.".format(
                release_tag, expected_commit
            )
        )

    print("Gitee Release {0} does not exist yet.".format(release_tag))


def preflight_oss_release_assets():
    oss_dir = require_env("OSS_RELEASE_DIR")
    oss_config = os.path.expanduser("~/oss/.ossutilnewconfig")
    stat_result = subprocess.call(
        ["ossutil", "stat", oss_dir + "SHA256SUMS.txt", "--config-file", oss_config],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if stat_result == 0:
        raise RuntimeError(
            "OSS Release path already exists: {0}\nRefusing to overwrite existing official release artifacts.".format(
                oss_dir
            )
        )
    print("OSS Release path is available: {0}".format(oss_dir))


def find_linux_assets():
    assets = []
    for name in os.listdir("."):
        if os.path.isfile(name) and (
            (name.startswith("sdk-open_") and name.endswith(".deb"))
            or (name.startswith("scanner_") and name.endswith(".deb"))
        ):
            assets.append(name)
    return sorted(assets)


def windows_architectures():
    # Windows 位数由独立参数控制，默认 x64 以兼容已有 Jenkins Job。
    architecture = env("WINDOWS_ARCH", "x64").strip().lower()
    if architecture == "all":
        return ["x86", "x64"]
    if architecture in ("x64", "x86"):
        return [architecture]
    raise RuntimeError(
        "Unsupported WINDOWS_ARCH: {0}. Expected x64, x86, or all.".format(
            architecture
        )
    )


def macos_architectures():
    architecture = env("MACOS_ARCH", "arm64").strip().lower()
    if architecture == "all":
        return ["arm64", "x86_64"]
    if architecture == "x86-64":
        return ["x86_64"]
    if architecture == "arm64":
        return ["arm64"]
    raise RuntimeError(
        "Unsupported MACOS_ARCH: {0}. Expected arm64, x86-64, or all.".format(
            architecture
        )
    )


def files_below(root, predicate):
    matches = []
    for current_root, _, files in os.walk(root):
        for name in files:
            if predicate(name):
                matches.append(os.path.join(current_root, name))
    return sorted(matches)


def expected_macos_package_name(architecture):
    return "sdk-open_macos_{0}_v{1}.{2}.{3}.pkg".format(
        architecture,
        require_env("SDK_OPEN_VERSION"),
        require_env("SDK_OPEN_BUILD"),
        require_env("SDK_OPEN_STAGE"),
    )


def expected_macos_symbol_archive_name(architecture):
    return expected_macos_package_name(architecture)[:-4] + ".dSYM.zip"


def expected_macos_symbols_manifest_name(architecture):
    return expected_macos_package_name(architecture)[:-4] + ".symbols.json"


def verify_macos_checksum(checksum_path, artifact_paths):
    entries = []
    for line in read_text(checksum_path).splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        parts = stripped.split(None, 1)
        if len(parts) != 2:
            raise RuntimeError("Invalid macOS checksum entry: {0}".format(line))
        listed_name = parts[1].lstrip("*")
        if listed_name != os.path.basename(listed_name):
            raise RuntimeError(
                "macOS checksum entries must use base filenames: {0}".format(
                    listed_name
                )
            )
        if not re.match(r"^[0-9a-fA-F]{64}$", parts[0]):
            raise RuntimeError("Invalid macOS SHA256 digest: {0}".format(parts[0]))
        entries.append((parts[0].lower(), listed_name))
    expected_entries = sorted(
        (sha256_file(path), os.path.basename(path)) for path in artifact_paths
    )
    normalized_entries = sorted(entries)
    if normalized_entries != expected_entries:
        raise RuntimeError(
            "macOS checksum mismatch: expected {0}, checksum entries={1}".format(
                expected_entries, normalized_entries
            )
        )
    return {name: digest for digest, name in normalized_entries}


def read_key_value_manifest(path):
    values = {}
    for line in read_text(path).splitlines():
        if not line.strip():
            continue
        if "=" not in line:
            raise RuntimeError("Invalid Jenkins manifest entry: {0}".format(line))
        key, value = line.split("=", 1)
        if not key or key in values:
            raise RuntimeError("Invalid or duplicate Jenkins manifest key: {0}".format(key))
        values[key] = value
    return values


def validate_macos_symbols_manifest(
    manifest_path,
    architecture,
    package_name,
    package_sha256,
    symbol_name,
    symbol_sha256,
    expected_parent_commit,
    expected_sdk_open_commit,
):
    try:
        manifest = json.loads(read_text(manifest_path))
    except (TypeError, ValueError) as exc:
        raise RuntimeError("Invalid macOS symbols manifest: {0}".format(exc))
    if not isinstance(manifest, dict):
        raise RuntimeError("Invalid macOS symbols manifest: root must be an object")

    expected_fields = {
        "version": require_env("SDK_OPEN_VERSION"),
        "build": require_env("SDK_OPEN_BUILD"),
        "stage": require_env("SDK_OPEN_STAGE"),
        "arch": architecture,
        "build_type": require_env("JENKINS_BUILD_TYPE"),
        "parent_build_commit": expected_parent_commit,
        "sdk_open_commit": expected_sdk_open_commit,
    }
    for key, expected_value in expected_fields.items():
        if manifest.get(key) != expected_value:
            raise RuntimeError(
                "macOS symbols manifest field {0} mismatch: expected {1!r}, got {2!r}".format(
                    key, expected_value, manifest.get(key)
                )
            )

    expected_package = {"name": package_name, "sha256": package_sha256}
    if manifest.get("package") != expected_package:
        raise RuntimeError(
            "macOS symbols manifest package metadata mismatch: expected {0}, got {1}".format(
                expected_package, manifest.get("package")
            )
        )
    symbols = manifest.get("symbols")
    if not isinstance(symbols, dict):
        raise RuntimeError("macOS symbols manifest symbols field must be an object")
    if symbols.get("name") != symbol_name or symbols.get("sha256") != symbol_sha256:
        raise RuntimeError("macOS symbols manifest archive metadata mismatch")

    binaries = symbols.get("binaries")
    expected_binary_names = {
        "sdk_open_app",
        "com.czur.device-helper",
        "libsdk_private_providers.dylib",
        "libsdk_private_auth_c_api.dylib",
    }
    if (
        not isinstance(binaries, list)
        or len(binaries) != len(expected_binary_names)
        or {
            item.get("name") for item in binaries if isinstance(item, dict)
        }
        != expected_binary_names
    ):
        raise RuntimeError("macOS symbols manifest binary list mismatch")
    uuid_pattern = re.compile(
        r"^[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}$"
    )
    if any(
        not isinstance(item, dict)
        or not uuid_pattern.match(item.get("uuid", ""))
        for item in binaries
    ):
        raise RuntimeError("macOS symbols manifest contains an invalid binary UUID")
    return manifest


def validate_macos_artifact_directory(artifact_dir, architecture):
    expected_package_name = expected_macos_package_name(architecture)
    expected_symbol_name = expected_macos_symbol_archive_name(architecture)
    expected_symbols_manifest = expected_macos_symbols_manifest_name(architecture)
    packages = files_below(artifact_dir, lambda name: name.endswith(".pkg"))
    if len(packages) != 1 or os.path.basename(packages[0]) != expected_package_name:
        raise RuntimeError(
            "Expected exactly one macOS package named {0} in {1}, found: {2}".format(
                expected_package_name, artifact_dir, packages
            )
        )

    symbol_archives = files_below(
        artifact_dir, lambda name: name.endswith(".dSYM.zip")
    )
    if (
        len(symbol_archives) != 1
        or os.path.basename(symbol_archives[0]) != expected_symbol_name
    ):
        raise RuntimeError(
            "Expected exactly one macOS dSYM archive named {0} in {1}, found: {2}".format(
                expected_symbol_name, artifact_dir, symbol_archives
            )
        )

    symbols_manifests = files_below(
        artifact_dir, lambda name: name.endswith(".symbols.json")
    )
    if (
        len(symbols_manifests) != 1
        or os.path.basename(symbols_manifests[0]) != expected_symbols_manifest
    ):
        raise RuntimeError(
            "Expected exactly one macOS symbols manifest named {0} in {1}, found: {2}".format(
                expected_symbols_manifest, artifact_dir, symbols_manifests
            )
        )

    manifests = files_below(
        artifact_dir,
        lambda name: name == "jenkins-sdk-open-macos-build-manifest.txt",
    )
    checksums = files_below(
        artifact_dir,
        lambda name: name == "SHA256SUMS.txt",
    )
    if len(manifests) != 1:
        raise RuntimeError(
            "Expected exactly one macOS Jenkins manifest in {0}, found: {1}".format(
                artifact_dir, manifests
            )
        )
    if len(checksums) != 1:
        raise RuntimeError(
            "Expected exactly one macOS checksum file in {0}, found: {1}".format(
                artifact_dir, checksums
            )
        )

    checksums_by_name = verify_macos_checksum(
        checksums[0], [packages[0], symbol_archives[0]]
    )
    parent_commit = require_env("BUILD_COMMIT")
    expected_sdk_open_commit = sdk_open_commit()
    jenkins_manifest = read_key_value_manifest(manifests[0])
    expected_manifest_fields = {
        "project": "sdk_open",
        "version": require_env("SDK_OPEN_VERSION"),
        "build": require_env("SDK_OPEN_BUILD"),
        "stage": require_env("SDK_OPEN_STAGE"),
        "arch": architecture,
        "build_type": require_env("JENKINS_BUILD_TYPE"),
        "git_commit": parent_commit,
        "parent_build_commit": parent_commit,
        "sdk_open_commit": expected_sdk_open_commit,
        "use_parent_build_metadata": "true",
        "package_name": expected_package_name,
        "symbol_archive_name": expected_symbol_name,
        "symbols_manifest_name": expected_symbols_manifest,
        "package_sha256": checksums_by_name[expected_package_name],
        "symbol_sha256": checksums_by_name[expected_symbol_name],
        "symbols_manifest_sha256": sha256_file(symbols_manifests[0]),
    }
    for key, expected_value in expected_manifest_fields.items():
        if jenkins_manifest.get(key) != expected_value:
            raise RuntimeError(
                "macOS Jenkins manifest field {0} mismatch: expected {1!r}, got {2!r}".format(
                    key, expected_value, jenkins_manifest.get(key)
                )
            )

    symbols_manifest = validate_macos_symbols_manifest(
        symbols_manifests[0],
        architecture,
        expected_package_name,
        checksums_by_name[expected_package_name],
        expected_symbol_name,
        checksums_by_name[expected_symbol_name],
        parent_commit,
        expected_sdk_open_commit,
    )
    return {
        "architecture": architecture,
        "package": packages[0],
        "symbols": symbol_archives[0],
        "symbols_manifest": symbols_manifests[0],
        "symbols_manifest_data": symbols_manifest,
        "package_sha256": checksums_by_name[expected_package_name],
        "symbol_sha256": checksums_by_name[expected_symbol_name],
    }


def prepare_macos_assets(include_symbol_records=False):
    artifact_root = require_env("MACOS_ARTIFACT_OSS_DIR").rstrip("/") + "/"
    oss_config = os.path.expanduser("~/oss/.ossutilnewconfig")
    targets = []
    symbol_records = []
    for architecture in macos_architectures():
        oss_target_dir = artifact_root + architecture + "/"
        local_dir = os.path.join("release-assets", "macos", architecture)
        os.makedirs(local_dir, exist_ok=True)
        print(
            "Downloading sdk-open macOS {0} release artifact from OSS: {1}".format(
                architecture, oss_target_dir
            )
        )
        run(
            [
                "ossutil",
                "cp",
                oss_target_dir,
                local_dir + "/",
                "-r",
                "--config-file",
                oss_config,
            ]
        )
        artifact = validate_macos_artifact_directory(local_dir, architecture)
        source = artifact["package"]
        target = os.path.basename(source)
        shutil.copyfile(source, target)
        targets.append(target)
        artifact["public_package"] = target
        artifact["source_oss_dir"] = oss_target_dir
        symbol_records.append(artifact)
    if include_symbol_records:
        return targets, symbol_records
    return targets


def prepare_windows_assets():
    os.makedirs("release-assets/windows", exist_ok=True)
    run(
        [
            "ossutil",
            "cp",
            require_env("WINDOWS_ARTIFACT_OSS_PREFIX_DIR") + "/",
            "release-assets/windows/",
            "-r",
            "--config-file",
            os.path.expanduser("~/oss/.ossutilnewconfig"),
        ]
    )
    # 正式发布必须按请求架构精确匹配，避免双架构产物遗漏或旧安装包混入。
    matches_by_arch = {architecture: [] for architecture in windows_architectures()}
    unexpected = []
    for root, _, files in os.walk("release-assets/windows"):
        for name in files:
            if name.startswith("sdk-open_Windows_") and name.endswith(".exe"):
                matched = False
                for architecture in matches_by_arch:
                    if name.startswith("sdk-open_Windows_{0}_".format(architecture)):
                        matches_by_arch[architecture].append(os.path.join(root, name))
                        matched = True
                        break
                if not matched:
                    unexpected.append(os.path.join(root, name))

    invalid_counts = {
        architecture: len(paths)
        for architecture, paths in matches_by_arch.items()
        if len(paths) != 1
    }
    if invalid_counts or unexpected:
        for root, _, files in os.walk("release-assets/windows"):
            for name in sorted(files):
                print(os.path.join(root, name))
        raise RuntimeError(
            "Expected exactly one sdk-open Windows installer for each requested architecture {0}; counts={1}, unexpected={2}".format(
                ",".join(matches_by_arch), invalid_counts, unexpected
            )
        )

    targets = []
    for architecture in windows_architectures():
        source = matches_by_arch[architecture][0]
        target = os.path.basename(source)
        with open(source, "rb") as src, open(target, "wb") as dst:
            dst.write(src.read())
        targets.append(target)
    return targets


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file_obj:
        for chunk in iter(lambda: file_obj.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def release_download_platform(asset):
    lower_name = asset.lower()
    if lower_name.endswith(".deb") and "_amd64_" in lower_name:
        return (
            "Linux x86 64bit / amd64",
            "适用于麒麟 Kylin、统信 UOS、Ubuntu 等 Debian 系 Linux 系统，CPU 为 Intel / AMD 64 位架构。",
        )
    if lower_name.endswith(".deb") and (
        "_arm64_" in lower_name or "_aarch64_" in lower_name
    ):
        return (
            "Linux ARM 64bit / arm64 / aarch64",
            "适用于麒麟 Kylin、统信 UOS、Ubuntu 等 Debian 系 Linux 系统，CPU 为 ARM 64 位架构。",
        )
    if lower_name.startswith("sdk-open_windows_x86_") and lower_name.endswith(".exe"):
        return ("Windows 32bit / x86", "适用于 Windows 32 位系统。")
    if lower_name.startswith("sdk-open_windows_x64_") and lower_name.endswith(".exe"):
        return ("Windows 64bit / x64", "适用于 Windows 64 位系统。")
    if lower_name.startswith("sdk-open_windows_") and lower_name.endswith(".exe"):
        return ("Windows 64bit", "适用于 Windows 系统。")
    if lower_name.startswith("sdk-open_macos_arm64_") and lower_name.endswith(".pkg"):
        return ("macOS Apple Silicon / arm64", "适用于 Apple Silicon 芯片的 macOS 系统。")
    if lower_name.startswith("sdk-open_macos_x86_64_") and lower_name.endswith(".pkg"):
        return ("macOS Intel / x86_64", "适用于 Intel 芯片的 macOS 系统。")
    return (asset, "请根据实际运行平台选择使用。")


def write_release_metadata(assets):
    sha_lines = ["{0}  {1}".format(sha256_file(asset), asset) for asset in assets]
    write_text("SHA256SUMS.txt", "\n".join(sha_lines) + "\n")

    base_url = require_env("RELEASE_DOWNLOAD_URL").rstrip("/") + "/"
    downloads = [
        {"name": asset, "url": base_url + urllib.parse.quote(asset)}
        for asset in assets
    ]

    lines = ["## 下载地址", ""]
    for index, item in enumerate(downloads, 1):
        platform, description = release_download_platform(item["name"])
        lines.extend(
            [
                "{0}. {1}  ".format(index, platform),
                "  {0}  ".format(description),
                "  下载：[{0}]({1})".format(item["name"], item["url"]),
                "",
            ]
        )
    lines.extend(
        [
            "## SHA256 校验",
            "",
            "完整校验文件：",
            "",
            base_url + "SHA256SUMS.txt",
            "",
            "```text",
            "\n".join(sha_lines),
            "```",
            "",
        ]
    )
    write_text("release-downloads.md", "\n".join(lines))

    release_note = read_text("release_note.md").rstrip()
    downloads_note = read_text("release-downloads.md").rstrip()
    write_text("gitee_release_note.md", release_note + "\n\n---\n\n" + downloads_note + "\n")

    manifest = {
        "release_tag": env("RELEASE_TAG").strip(),
        "oss_release_dir": require_env("OSS_RELEASE_DIR"),
        "release_download_url": base_url,
        "release_save_dir_name": require_env("RELEASE_SAVE_DIR_NAME"),
        "assets": downloads,
    }
    write_text(
        "release_manifest.json",
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
    )


def utc_iso8601_now():
    return (
        datetime.datetime.now(datetime.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def read_release_checksums(path, expected_names):
    checksums = {}
    for line in read_text(path).splitlines():
        if not line.strip():
            continue
        parts = line.split(None, 1)
        if len(parts) != 2 or not re.match(r"^[0-9a-fA-F]{64}$", parts[0]):
            raise RuntimeError("Invalid release checksum entry: {0}".format(line))
        name = parts[1].lstrip("*")
        if name != os.path.basename(name):
            raise RuntimeError(
                "Release checksum entries must use base filenames: {0}".format(name)
            )
        if name in checksums:
            raise RuntimeError("Duplicate release checksum entry: {0}".format(name))
        checksums[name] = parts[0].lower()

    expected = set(expected_names)
    actual = set(checksums)
    if actual != expected:
        raise RuntimeError(
            "Release checksum asset set mismatch: missing={0}, unexpected={1}".format(
                sorted(expected - actual), sorted(actual - expected)
            )
        )
    return checksums


def runtime_artifact_platform(filename):
    expected_metadata = "{0}.{1}.{2}".format(
        require_env("SDK_OPEN_VERSION"),
        require_env("SDK_OPEN_BUILD"),
        require_env("SDK_OPEN_STAGE"),
    )

    windows_match = re.match(
        r"^sdk-open_Windows_(?P<arch>.+)_v(?P<metadata>.+)\.exe$", filename
    )
    if windows_match:
        if windows_match.group("metadata") != expected_metadata:
            raise RuntimeError(
                "Runtime artifact version/build/stage mismatch: {0}; expected {1}".format(
                    filename, expected_metadata
                )
            )
        architecture = windows_match.group("arch")
        if architecture not in ("x86", "x64"):
            raise RuntimeError(
                "Unsupported Windows runtime artifact architecture: {0}".format(
                    architecture
                )
            )
        return {"os": "windows", "arch": architecture, "format": "exe"}

    macos_match = re.match(
        r"^sdk-open_macos_(?P<arch>.+)_v(?P<metadata>.+)\.pkg$", filename
    )
    if macos_match:
        if macos_match.group("metadata") != expected_metadata:
            raise RuntimeError(
                "Runtime artifact version/build/stage mismatch: {0}; expected {1}".format(
                    filename, expected_metadata
                )
            )
        architecture = macos_match.group("arch")
        if architecture not in ("arm64", "x86_64"):
            raise RuntimeError(
                "Unsupported macOS runtime artifact architecture: {0}".format(
                    architecture
                )
            )
        return {"os": "macos", "arch": architecture, "format": "pkg"}

    linux_match = re.match(
        r"^sdk-open_(?P<metadata>.+)_(?P<arch>[^_]+)_(?P<distro>[^_]+)\.deb$",
        filename,
    )
    if linux_match:
        if linux_match.group("metadata") != expected_metadata:
            raise RuntimeError(
                "Runtime artifact version/build/stage mismatch: {0}; expected {1}".format(
                    filename, expected_metadata
                )
            )
        architecture = linux_match.group("arch")
        distro = linux_match.group("distro")
        supported_distros = ("ubuntu", "kylin", "uos", "common")
        if distro not in supported_distros:
            raise RuntimeError(
                "Unsupported Linux runtime artifact distro: {0}".format(distro)
            )
        expected_distro = env("SDK_OPEN_DISTRO", "common").strip().lower()
        if distro != expected_distro:
            raise RuntimeError(
                "Linux runtime artifact distro mismatch: {0}; expected {1}".format(
                    distro, expected_distro
                )
            )
        normalized_arch = {
            "amd64": "x64",
            "arm64": "arm64",
            "aarch64": "arm64",
        }.get(architecture)
        if not normalized_arch:
            raise RuntimeError(
                "Unsupported Linux runtime artifact architecture: {0}".format(
                    architecture
                )
            )
        return {
            "os": "linux",
            "arch": normalized_arch,
            "os_family": "debian",
            "format": "deb",
        }

    raise RuntimeError("Unrecognized Runtime Manifest artifact: {0}".format(filename))


def expected_runtime_artifact_keys():
    expected = set()
    if env("RUN_LINUX_SDK_OPEN") == "true":
        selected = env("SDK_OPEN_LINUX_ARCHES") or env("LINUX_ARCH")
        for architecture in selected.split(","):
            normalized = {
                "x86-64": "x64",
                "amd64": "x64",
                "x64": "x64",
                "arm64": "arm64",
                "aarch64": "arm64",
            }.get(architecture.strip().lower())
            if not normalized:
                raise RuntimeError(
                    "Unsupported requested Linux runtime architecture: {0}".format(
                        architecture
                    )
                )
            expected.add(("linux", normalized))

    if env("RUN_WINDOWS_SDK_OPEN") == "true":
        expected.update(("windows", item) for item in windows_architectures())

    if env("RUN_MACOS_SDK_OPEN") == "true":
        expected.update(("macos", item) for item in macos_architectures())
    return expected


def generate_runtime_manifest(output_path="manifest.json", generated_at=None):
    release_manifest_path = "release_manifest.json"
    checksum_path = "SHA256SUMS.txt"
    if not os.path.isfile(release_manifest_path):
        raise RuntimeError(
            "release_manifest.json is required before Runtime Manifest generation"
        )
    if not os.path.isfile(checksum_path):
        raise RuntimeError(
            "SHA256SUMS.txt is required before Runtime Manifest generation"
        )

    try:
        release_manifest = json.loads(read_text(release_manifest_path))
    except (OSError, TypeError, ValueError) as exc:
        raise RuntimeError("Invalid release_manifest.json: {0}".format(exc))
    if not isinstance(release_manifest, dict):
        raise RuntimeError("Invalid release_manifest.json: root must be an object")

    expected_release_dir = require_env("OSS_RELEASE_DIR")
    if release_manifest.get("oss_release_dir") != expected_release_dir:
        raise RuntimeError(
            "Release manifest OSS directory mismatch: expected {0}, got {1}".format(
                expected_release_dir, release_manifest.get("oss_release_dir")
            )
        )
    expected_release_tag = require_env("RELEASE_TAG")
    if release_manifest.get("release_tag") != expected_release_tag:
        raise RuntimeError(
            "Release manifest tag mismatch: expected {0}, got {1}".format(
                expected_release_tag, release_manifest.get("release_tag")
            )
        )

    base_url = require_env("RELEASE_DOWNLOAD_URL").rstrip("/") + "/"
    if release_manifest.get("release_download_url") != base_url:
        raise RuntimeError(
            "Release manifest download URL mismatch: expected {0}, got {1}".format(
                base_url, release_manifest.get("release_download_url")
            )
        )
    assets = release_manifest.get("assets")
    if not isinstance(assets, list) or not assets:
        raise RuntimeError("Invalid release_manifest.json: assets must be a non-empty array")

    asset_names = []
    asset_urls = {}
    for item in assets:
        if not isinstance(item, dict):
            raise RuntimeError("Invalid release asset entry in release_manifest.json")
        name = item.get("name", "")
        if not isinstance(name, str) or not name or name != os.path.basename(name):
            raise RuntimeError(
                "Invalid release asset name in release_manifest.json: {0}".format(name)
            )
        if name in asset_urls:
            raise RuntimeError("Duplicate release asset name: {0}".format(name))
        expected_url = base_url + urllib.parse.quote(name, safe="")
        if item.get("url") != expected_url:
            raise RuntimeError(
                "Release asset URL mismatch for {0}: expected {1}, got {2}".format(
                    name, expected_url, item.get("url")
                )
            )
        asset_names.append(name)
        asset_urls[name] = expected_url

    checksums = read_release_checksums(checksum_path, asset_names)
    runtime_artifacts = []
    actual_keys = set()
    for name in asset_names:
        if not os.path.isfile(name):
            raise RuntimeError("Missing local Runtime Manifest artifact: {0}".format(name))
        platform = runtime_artifact_platform(name)
        platform_key = (platform["os"], platform["arch"])
        if platform_key in actual_keys:
            raise RuntimeError(
                "Duplicate Runtime Manifest platform/architecture: {0}/{1}".format(
                    platform_key[0], platform_key[1]
                )
            )
        actual_keys.add(platform_key)
        try:
            actual_sha256 = sha256_file(name)
            size_bytes = os.path.getsize(name)
        except OSError as exc:
            raise RuntimeError(
                "Failed to read Runtime Manifest artifact {0}: {1}".format(name, exc)
            )
        if actual_sha256 != checksums[name]:
            raise RuntimeError(
                "Runtime artifact SHA256 mismatch for {0}: expected {1}, got {2}".format(
                    name, checksums[name], actual_sha256
                )
            )
        artifact = dict(platform)
        artifact.update(
            {
                "filename": name,
                "url": asset_urls[name],
                "size_bytes": size_bytes,
                "sha256": actual_sha256,
            }
        )
        runtime_artifacts.append(artifact)

    expected_keys = expected_runtime_artifact_keys()
    if expected_keys and actual_keys != expected_keys:
        raise RuntimeError(
            "Runtime artifact set mismatch: missing={0}, unexpected={1}".format(
                sorted(expected_keys - actual_keys), sorted(actual_keys - expected_keys)
            )
        )
    runtime_artifacts.sort(
        key=lambda item: RUNTIME_ARTIFACT_ORDER[(item["os"], item["arch"])]
    )

    manifest = {
        "schema_version": 1,
        "channel": "stable",
        "generated_at": generated_at or utc_iso8601_now(),
        "releases": [
            {
                "version": require_env("SDK_OPEN_VERSION"),
                "supported_protocol_versions": RUNTIME_MANIFEST_PROTOCOL_VERSIONS,
                "artifacts": runtime_artifacts,
            }
        ],
    }
    write_text(
        output_path,
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
    )
    print("Runtime Manifest generated: {0}".format(output_path))
    return manifest


def expected_windows_package_name(architecture):
    return "sdk-open_Windows_{0}_v{1}.{2}.{3}.exe".format(
        architecture,
        require_env("SDK_OPEN_VERSION"),
        require_env("SDK_OPEN_BUILD"),
        require_env("SDK_OPEN_STAGE"),
    )


def read_release_manifest_asset_names():
    manifest_path = "release_manifest.json"
    if not os.path.isfile(manifest_path):
        raise RuntimeError(
            "release_manifest.json is required before Development staging cleanup"
        )
    try:
        manifest = json.loads(read_text(manifest_path))
    except (TypeError, ValueError) as exc:
        raise RuntimeError("Invalid release_manifest.json: {0}".format(exc))
    if not isinstance(manifest, dict):
        raise RuntimeError("Invalid release_manifest.json: root must be an object")

    expected_release_dir = require_env("OSS_RELEASE_DIR")
    if manifest.get("oss_release_dir") != expected_release_dir:
        raise RuntimeError(
            "Release manifest OSS directory mismatch: expected {0}, got {1}".format(
                expected_release_dir, manifest.get("oss_release_dir")
            )
        )

    manifest_assets = manifest.get("assets")
    if not isinstance(manifest_assets, list):
        raise RuntimeError("Invalid release_manifest.json: assets must be an array")

    names = []
    for item in manifest_assets:
        name = item.get("name", "") if isinstance(item, dict) else ""
        if not isinstance(name, str) or not name or name != os.path.basename(name):
            raise RuntimeError(
                "Invalid release asset name in release_manifest.json: {0}".format(name)
            )
        names.append(name)
    if len(names) != len(set(names)):
        raise RuntimeError("Duplicate release asset names in release_manifest.json")
    return names


def development_staging_cleanup_plan(asset_names):
    asset_name_set = set(asset_names)
    release_dir = require_env("OSS_RELEASE_DIR").rstrip("/") + "/"
    development_root = require_env("OSS_ARTIFACT_DIR").rstrip("/") + "/"
    if release_dir == development_root:
        raise RuntimeError("Release and Development OSS directories must be different")
    release_objects = []
    development_objects = []

    if env("RUN_WINDOWS_SDK_OPEN") == "true":
        windows_dir = require_env("WINDOWS_ARTIFACT_OSS_PREFIX_DIR").rstrip("/") + "/"
        expected_windows_dir = development_root + "windows/"
        if windows_dir != expected_windows_dir:
            raise RuntimeError(
                "Refusing cleanup for unexpected Windows Development directory: {0}".format(
                    windows_dir
                )
            )
        for architecture in windows_architectures():
            package_name = expected_windows_package_name(architecture)
            if package_name not in asset_name_set:
                raise RuntimeError(
                    "Windows package is missing from release_manifest.json: {0}".format(
                        package_name
                    )
                )
            release_objects.append(release_dir + package_name)
            development_objects.append(windows_dir + package_name)
        development_objects.append(windows_dir + "SHA256SUMS.txt")

    if env("RUN_MACOS_SDK_OPEN") == "true":
        macos_dir = require_env("MACOS_ARTIFACT_OSS_DIR").rstrip("/") + "/"
        symbol_root = require_env("OSS_SYMBOL_DIR").rstrip("/") + "/"
        expected_macos_dir = development_root + "macos/"
        if macos_dir != expected_macos_dir:
            raise RuntimeError(
                "Refusing cleanup for unexpected macOS Development directory: {0}".format(
                    macos_dir
                )
            )
        for architecture in macos_architectures():
            package_name = expected_macos_package_name(architecture)
            symbol_name = expected_macos_symbol_archive_name(architecture)
            if package_name not in asset_name_set:
                raise RuntimeError(
                    "macOS package is missing from release_manifest.json: {0}".format(
                        package_name
                    )
                )
            release_objects.append(release_dir + package_name)
            release_objects.append(
                "{0}macos/{1}/{2}".format(
                    symbol_root, architecture, symbol_name
                )
            )
            architecture_dir = macos_dir + architecture + "/"
            development_objects.extend(
                [
                    architecture_dir + package_name,
                    architecture_dir + symbol_name,
                    architecture_dir + "SHA256SUMS.txt",
                ]
            )

    return release_objects, development_objects


def oss_object_exists(uri, oss_config):
    return (
        subprocess.call(
            ["ossutil", "stat", uri, "--config-file", oss_config],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        == 0
    )


def cleanup_development_staging():
    if env("PUBLISH_OSS_RELEASE") != "true":
        raise RuntimeError(
            "Refusing to clean Development staging for a non-release build"
        )

    asset_names = read_release_manifest_asset_names()
    release_objects, development_objects = development_staging_cleanup_plan(asset_names)
    if not development_objects:
        print("No Windows or macOS Development package staging requires cleanup.")
        return

    oss_config = os.path.expanduser("~/oss/.ossutilnewconfig")
    for uri in release_objects:
        if not oss_object_exists(uri, oss_config):
            raise RuntimeError(
                "Refusing Development staging cleanup because a required published asset is missing: {0}".format(
                    uri
                )
            )

    for uri in development_objects:
        if not oss_object_exists(uri, oss_config):
            print("Development staging object is already absent: {0}".format(uri))
            continue
        print("Removing published Development staging object: {0}".format(uri))
        run(["ossutil", "rm", uri, "-f", "--config-file", oss_config])

    remaining = [
        uri for uri in development_objects if oss_object_exists(uri, oss_config)
    ]
    if remaining:
        raise RuntimeError(
            "Development staging cleanup verification failed; objects still exist: {0}".format(
                ", ".join(remaining)
            )
        )
    print("Published Windows/macOS Development package staging cleanup completed.")


def publish_macos_symbols(symbol_records):
    if not symbol_records:
        return

    symbol_root = require_env("OSS_SYMBOL_DIR").rstrip("/") + "/"
    development_root = require_env("OSS_ARTIFACT_DIR").rstrip("/") + "/"
    release_root = require_env("OSS_RELEASE_DIR").rstrip("/") + "/"
    if symbol_root in (development_root, release_root):
        raise RuntimeError(
            "macOS Symbols, Development, and Release OSS directories must be different"
        )

    oss_config = os.path.expanduser("~/oss/.ossutilnewconfig")
    for record in symbol_records:
        architecture = record["architecture"]
        symbol_name = os.path.basename(record["symbols"])
        target_dir = "{0}macos/{1}/".format(symbol_root, architecture)
        local_dir = os.path.join("release-assets", "macos-symbols", architecture)
        os.makedirs(local_dir, exist_ok=True)

        local_symbol = os.path.join(local_dir, symbol_name)
        local_checksum = os.path.join(local_dir, "SHA256SUMS.txt")
        local_manifest = os.path.join(local_dir, "symbols_manifest.json")
        shutil.copyfile(record["symbols"], local_symbol)
        write_text(
            local_checksum,
            "{0}  {1}\n".format(record["symbol_sha256"], symbol_name),
        )
        write_text(
            local_manifest,
            json.dumps(
                record["symbols_manifest_data"],
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )

        formal_files = [local_symbol, local_checksum, local_manifest]
        existing = [
            target_dir + os.path.basename(path)
            for path in formal_files
            if oss_object_exists(target_dir + os.path.basename(path), oss_config)
        ]
        if existing:
            raise RuntimeError(
                "macOS Symbols path already contains official artifacts; refusing to overwrite: {0}".format(
                    ", ".join(existing)
                )
            )

        for path in formal_files:
            print(
                "Uploading macOS {0} symbols artifact: {1} -> {2}".format(
                    architecture, os.path.basename(path), target_dir
                )
            )
            run(["ossutil", "cp", path, target_dir, "--config-file", oss_config])

        missing = [
            target_dir + os.path.basename(path)
            for path in formal_files
            if not oss_object_exists(target_dir + os.path.basename(path), oss_config)
        ]
        if missing:
            raise RuntimeError(
                "macOS Symbols upload verification failed; objects are missing: {0}".format(
                    ", ".join(missing)
                )
            )
        print("macOS {0} symbols archived to: {1}".format(architecture, target_dir))


def publish_oss_release_assets():
    run(["rm", "-rf", "release-assets"])
    os.makedirs("release-assets/windows", exist_ok=True)

    assets = []
    macos_symbol_records = []
    if env("RUN_LINUX_SDK_OPEN") == "true":
        assets.extend(find_linux_assets())
        if not assets:
            raise RuntimeError("No deb file found for OSS Release upload")

    if env("RUN_WINDOWS_SDK_OPEN") == "true":
        print(
            "Downloading sdk-open Windows release artifact from OSS: {0}".format(
                require_env("WINDOWS_ARTIFACT_OSS_PREFIX_DIR")
            )
        )
        assets.extend(prepare_windows_assets())

    if env("RUN_MACOS_SDK_OPEN") == "true":
        macos_assets, macos_symbol_records = prepare_macos_assets(
            include_symbol_records=True
        )
        assets.extend(macos_assets)

    if not assets:
        raise RuntimeError("No OSS Release asset found")
    if not os.path.isfile("release_note.md"):
        raise RuntimeError("release_note.md is required for OSS Release publishing")

    write_text("release-assets.txt", "\n".join(assets) + "\n")
    write_release_metadata(assets)

    print("OSS Release assets:")
    print(read_text("release-assets.txt"), end="")
    print(read_text("SHA256SUMS.txt"), end="")
    print(read_text("release-downloads.md"), end="")

    oss_dir = require_env("OSS_RELEASE_DIR")
    oss_config = os.path.expanduser("~/oss/.ossutilnewconfig")
    stat_result = subprocess.call(
        ["ossutil", "stat", oss_dir + "SHA256SUMS.txt", "--config-file", oss_config],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if stat_result == 0:
        raise RuntimeError(
            "OSS Release path already exists: {0}\nRefusing to overwrite existing official release artifacts.".format(
                oss_dir
            )
        )

    for asset in assets:
        if not os.path.isfile(asset):
            raise RuntimeError("Missing OSS Release asset: {0}".format(asset))
        print("Uploading OSS Release asset: {0}".format(asset))
        run(["ossutil", "cp", asset, oss_dir, "--config-file", oss_config])

    for asset in [
        "SHA256SUMS.txt",
        "release_note.md",
        "gitee_release_note.md",
        "release-downloads.md",
        "release_manifest.json",
    ]:
        if not os.path.isfile(asset):
            raise RuntimeError("Missing OSS Release metadata: {0}".format(asset))
        print("Uploading OSS Release metadata: {0}".format(asset))
        run(["ossutil", "cp", asset, oss_dir, "--config-file", oss_config])

    publish_macos_symbols(macos_symbol_records)

    print("OSS Release artifacts uploaded to: {0}".format(oss_dir))
    print("ESA Release download URL: {0}".format(require_env("RELEASE_DOWNLOAD_URL")))

    base_url = require_env("RELEASE_DOWNLOAD_URL").rstrip("/") + "/"
    urls = [base_url + urllib.parse.quote(asset) for asset in assets]
    urls.append(base_url + "SHA256SUMS.txt")
    write_text("esa-urls.txt", "\n".join(urls) + "\n")
    for url in urls:
        print("Verifying ESA URL: {0}".format(url))
        run(["curl", "-fIL", "--connect-timeout", "10", "--max-time", "60", url])


def publish_gitee_release():
    if not os.path.isfile("gitee_release_note.md"):
        raise RuntimeError("gitee_release_note.md is required for Gitee Release publishing")

    repo, owner, repo_name = gitee_repo_parts()
    release_tag = require_env("RELEASE_TAG")
    commit = sdk_open_commit()
    repo_api_path = "repos/{0}/{1}".format(quote_path(owner), quote_path(repo_name))

    check_gitee_release_absent(repo, repo_api_path, release_tag)

    data = urllib.parse.urlencode(
        {
            "access_token": require_env("GITEE_TOKEN"),
            "tag_name": release_tag,
            "target_commitish": commit,
            "name": release_tag,
            "body": read_text("gitee_release_note.md"),
        }
    ).encode("utf-8")
    status, body = http_request(
        "https://gitee.com/api/v5/{0}/releases".format(repo_api_path),
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        data=data,
    )
    if status not in (200, 201):
        raise RuntimeError(
            "Failed to create Gitee Release {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )
    print("Gitee Release {0} created in {1}.".format(release_tag, repo))


def prepare_release_assets_for_platform(platform_name):
    run(["rm", "-rf", "release-assets"])
    os.makedirs("release-assets/windows", exist_ok=True)

    assets = []
    if env("RUN_LINUX_SDK_OPEN") == "true":
        assets.extend(find_linux_assets())
        if not assets:
            raise RuntimeError("No deb file found for {0} Release upload".format(platform_name))

    if env("RUN_WINDOWS_SDK_OPEN") == "true":
        print(
            "Downloading sdk-open Windows release artifact from OSS: {0}".format(
                require_env("WINDOWS_ARTIFACT_OSS_PREFIX_DIR")
            )
        )
        assets.extend(prepare_windows_assets())

    if env("RUN_MACOS_SDK_OPEN") == "true":
        assets.extend(prepare_macos_assets())

    if not assets:
        raise RuntimeError("No {0} Release asset found".format(platform_name))
    if not os.path.isfile("release_note.md"):
        raise RuntimeError("release_note.md is required for {0} Release publishing".format(platform_name))

    write_text("release-assets.txt", "\n".join(assets) + "\n")
    write_release_metadata(assets)
    print("{0} Release assets:".format(platform_name))
    print(read_text("release-assets.txt"), end="")
    print(read_text("SHA256SUMS.txt"), end="")
    return assets


def reconcile_github_asset(release_api_url, headers, asset_name, data):
    """Recover a lost upload response without overwriting a completed asset."""
    page = 1
    while True:
        status, body = http_request(
            "{0}/assets?per_page=100&page={1}".format(release_api_url, page),
            headers=headers,
        )
        if status in (408, 429, 500, 502, 503, 504):
            raise urllib.error.URLError("GitHub asset reconciliation HTTP {0}".format(status))
        if status != 200:
            raise RuntimeError("Cannot reconcile GitHub asset {0}: HTTP {1}".format(asset_name, status))
        assets = json.loads(body)
        for remote in assets:
            if remote.get("name") != asset_name:
                continue
            if remote.get("state") == "uploaded":
                digest = "sha256:" + hashlib.sha256(data).hexdigest()
                if remote.get("size") == len(data) and remote.get("digest") == digest:
                    return True
                raise RuntimeError("Refusing to overwrite completed GitHub asset {0}: digest/size not verified".format(asset_name))
            if remote.get("state") != "starter":
                raise RuntimeError("Unexpected GitHub asset state for {0}".format(asset_name))
            status, _ = http_request(
                "{0}/assets/{1}".format(release_api_url.rsplit("/", 1)[0], int(remote["id"])),
                method="DELETE", headers=headers,
            )
            if status in (408, 429, 500, 502, 503, 504):
                raise urllib.error.URLError("GitHub incomplete asset cleanup HTTP {0}".format(status))
            if status not in (204, 404):
                raise RuntimeError("Cannot remove incomplete GitHub asset {0}: HTTP {1}".format(asset_name, status))
            return False
        if len(assets) < 100:
            return False
        page += 1


def upload_github_asset(upload_url, token, asset, release_api_url):
    asset_name = os.path.basename(asset)
    with open(asset, "rb") as file_obj:
        data = file_obj.read()
    headers = {
        "Authorization": "Bearer {0}".format(token),
        "Accept": "application/vnd.github+json",
    }
    transient_errors = (urllib.error.URLError, socket.timeout, ConnectionError, http.client.HTTPException)
    last_error = "unknown"
    for attempt in range(1, GITHUB_ASSET_UPLOAD_MAX_ATTEMPTS + 2):
        try:
            if attempt > 1:
                if reconcile_github_asset(release_api_url, headers, asset_name, data):
                    print("GitHub asset verified after interrupted upload: {0}".format(asset_name), flush=True)
                    return
            # One final reconciliation after the last upload; no additional POST.
            if attempt > GITHUB_ASSET_UPLOAD_MAX_ATTEMPTS:
                break
            print("Uploading GitHub asset {0} ({1} bytes), attempt {2}/{3}".format(
                asset_name, len(data), attempt, GITHUB_ASSET_UPLOAD_MAX_ATTEMPTS), flush=True)
            status, body = http_request(
                "{0}?name={1}".format(upload_url, urllib.parse.quote(asset_name)),
                method="POST",
                headers=dict(headers, **{"Content-Type": "application/octet-stream"}),
                data=data, timeout=GITHUB_ASSET_UPLOAD_TIMEOUT_SECONDS,
            )
            if status == 201:
                print("GitHub asset uploaded: {0}".format(asset_name), flush=True)
                return
            if status not in (408, 429, 500, 502, 503, 504):
                raise RuntimeError("Failed to upload GitHub Release asset {0}, HTTP {1}\n{2}".format(asset_name, status, body))
            last_error = "HTTP {0}".format(status)
        except transient_errors as exc:
            last_error = str(exc)
        if attempt <= GITHUB_ASSET_UPLOAD_MAX_ATTEMPTS:
            print("GitHub asset {0}: {1}; reconciling before retry".format(asset_name, last_error), flush=True)
            time.sleep(min(5 * attempt, 15))
    raise RuntimeError("GitHub asset {0} upload failed after bounded retries: {1}".format(asset_name, last_error))


def publish_github_release():
    assets = prepare_release_assets_for_platform("GitHub")
    assets.append("SHA256SUMS.txt")

    repo = require_env("GITHUB_REPO")
    release_tag = require_env("RELEASE_TAG")
    token = require_env("GITHUB_TOKEN")
    commit = sdk_open_commit()
    repo_api_url = "https://api.github.com/repos/{0}".format(repo)
    releases_api_url = repo_api_url + "/releases"
    headers = github_headers()

    status, body = http_request(
        "{0}/tags/{1}".format(releases_api_url, quote_path(release_tag)),
        headers=headers,
    )
    if status == 200:
        raise RuntimeError(
            "GitHub Release {0} already exists in {1}; refusing to overwrite.\n{2}".format(
                release_tag, repo, body
            )
        )
    if status != 404:
        raise RuntimeError(
            "Failed to check GitHub Release {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )

    payload = json.dumps(
        {
            "tag_name": release_tag,
            "target_commitish": commit,
            "name": release_tag,
            "body": read_text("release_note.md"),
        }
    ).encode("utf-8")
    status, body = http_request(
        releases_api_url,
        method="POST",
        headers={
            "Authorization": "Bearer {0}".format(token),
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
        },
        data=payload,
    )
    if status != 201:
        raise RuntimeError(
            "Failed to create GitHub Release {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )

    release = json.loads(body)
    release_id = release.get("id")
    if not release_id:
        raise RuntimeError("Failed to get GitHub Release id for {0}\n{1}".format(release_tag, body))

    status, body = http_request(
        "{0}/tags/{1}".format(releases_api_url, quote_path(release_tag)),
        headers=headers,
    )
    if status != 200:
        raise RuntimeError(
            "Failed to refresh GitHub Release {0}, HTTP {1}\n{2}".format(
                release_tag, status, body
            )
        )
    release = json.loads(body)
    upload_url = release.get("upload_url", "").split("{", 1)[0]
    if not upload_url:
        raise RuntimeError("Failed to get GitHub Release upload_url for {0}\n{1}".format(release_tag, body))

    existing_assets = {
        asset.get("name"): asset.get("id")
        for asset in release.get("assets", [])
        if asset.get("name")
    }
    for asset in assets:
        if not os.path.isfile(asset):
            raise RuntimeError("Missing GitHub Release asset: {0}".format(asset))
        asset_name = os.path.basename(asset)
        if asset_name in existing_assets:
            raise RuntimeError(
                "GitHub Release asset {0} already exists on {1}; refusing to overwrite it.".format(
                    asset_name, release_tag
                )
            )
        upload_github_asset(upload_url, token, asset, "{0}/{1}".format(releases_api_url, release_id))


def parse_args(argv):
    parser = argparse.ArgumentParser(description="SDK Open release helper")
    subparsers = parser.add_subparsers(dest="command")
    subparsers.required = True
    subparsers.add_parser("preflight-github")
    subparsers.add_parser("preflight-gitee")
    subparsers.add_parser("preflight-oss")
    subparsers.add_parser("publish-oss-release-assets")
    subparsers.add_parser("publish-github")
    subparsers.add_parser("publish-gitee")
    subparsers.add_parser("cleanup-development-staging")
    runtime_manifest_parser = subparsers.add_parser("generate-runtime-manifest")
    runtime_manifest_parser.add_argument("--output", default="manifest.json")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    try:
        if args.command == "preflight-github":
            preflight_github()
        elif args.command == "preflight-gitee":
            preflight_gitee()
        elif args.command == "preflight-oss":
            preflight_oss_release_assets()
        elif args.command == "publish-oss-release-assets":
            publish_oss_release_assets()
        elif args.command == "publish-github":
            publish_github_release()
        elif args.command == "publish-gitee":
            publish_gitee_release()
        elif args.command == "cleanup-development-staging":
            cleanup_development_staging()
        elif args.command == "generate-runtime-manifest":
            generate_runtime_manifest(args.output)
        else:
            raise RuntimeError("Unsupported command: {0}".format(args.command))
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
