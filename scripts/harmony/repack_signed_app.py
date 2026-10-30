#!/usr/bin/env python3
"""Repack a freshly signed Harmony HAP into the outer APP archive."""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile


def remove_if_present(path):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_hap(java, sign_tool, hap_path):
    with tempfile.TemporaryDirectory(prefix="sdkopen-hap-verify-") as temp_dir:
        command = [
            java,
            "-jar",
            sign_tool,
            "verify-app",
            "-inFile",
            hap_path,
            "-outCertChain",
            os.path.join(temp_dir, "cert-chain.cer"),
            "-outProfile",
            os.path.join(temp_dir, "profile.p7b"),
        ]
        subprocess.run(command, check=True)


def copy_zip_entry(source, target, info):
    with source.open(info, "r") as input_stream:
        with target.open(info, "w", force_zip64=True) as output_stream:
            shutil.copyfileobj(input_stream, output_stream, 1024 * 1024)


def repack(unsigned_app, signed_app, signed_hap, member_name):
    output_dir = os.path.dirname(os.path.abspath(signed_app))
    os.makedirs(output_dir, exist_ok=True)
    file_descriptor, temporary_app = tempfile.mkstemp(
        prefix=".sdkopen-signed-", suffix=".app", dir=output_dir
    )
    os.close(file_descriptor)
    try:
        found_member = False
        with zipfile.ZipFile(unsigned_app, "r") as source:
            with zipfile.ZipFile(temporary_app, "w", allowZip64=True) as target:
                target.comment = source.comment
                for info in source.infolist():
                    if info.filename == member_name:
                        if found_member:
                            raise RuntimeError(
                                "APP contains duplicate HAP member: {}".format(member_name)
                            )
                        found_member = True
                        replacement = zipfile.ZipInfo(member_name, info.date_time)
                        replacement.compress_type = info.compress_type
                        replacement.comment = info.comment
                        replacement.extra = info.extra
                        replacement.create_system = info.create_system
                        replacement.create_version = info.create_version
                        replacement.extract_version = info.extract_version
                        replacement.flag_bits = info.flag_bits
                        replacement.internal_attr = info.internal_attr
                        replacement.external_attr = info.external_attr
                        with open(signed_hap, "rb") as input_stream:
                            with target.open(
                                replacement, "w", force_zip64=True
                            ) as output_stream:
                                shutil.copyfileobj(
                                    input_stream, output_stream, 1024 * 1024
                                )
                    else:
                        copy_zip_entry(source, target, info)
        if not found_member:
            raise RuntimeError("APP does not contain HAP member: {}".format(member_name))
        os.replace(temporary_app, signed_app)
    except Exception:
        remove_if_present(temporary_app)
        raise


def verify_embedded_hap(signed_app, signed_hap, member_name, java, sign_tool):
    expected_digest = sha256_file(signed_hap)
    with tempfile.TemporaryDirectory(prefix="sdkopen-app-verify-") as temp_dir:
        embedded_hap = os.path.join(temp_dir, os.path.basename(member_name))
        with zipfile.ZipFile(signed_app, "r") as archive:
            matches = [info for info in archive.infolist() if info.filename == member_name]
            if len(matches) != 1:
                raise RuntimeError(
                    "signed APP must contain exactly one {} member".format(member_name)
                )
            with archive.open(matches[0], "r") as input_stream:
                with open(embedded_hap, "wb") as output_stream:
                    shutil.copyfileobj(input_stream, output_stream, 1024 * 1024)
        actual_digest = sha256_file(embedded_hap)
        if actual_digest != expected_digest:
            raise RuntimeError("embedded signed HAP does not match module output")
        verify_hap(java, sign_tool, embedded_hap)
    return expected_digest


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--unsigned-app", required=True)
    parser.add_argument("--signed-app", required=True)
    parser.add_argument("--signed-hap", required=True)
    parser.add_argument("--member", required=True)
    parser.add_argument("--java")
    parser.add_argument("--sign-tool")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.prepare:
        remove_if_present(args.signed_hap)
        remove_if_present(args.signed_app)
        return 0

    if not os.path.isfile(args.unsigned_app):
        raise RuntimeError("unsigned APP was not produced: {}".format(args.unsigned_app))

    # The only valid signed APP is the one atomically created below from the
    # signed HAP that passed verification during this invocation.
    remove_if_present(args.signed_app)
    if not os.path.isfile(args.signed_hap):
        # Hvigor may emit a misleading *-signed.app containing an unsigned HAP.
        print("No freshly signed HAP was produced; keeping unsigned APP only.")
        return 0

    if not args.java or not args.sign_tool:
        raise RuntimeError("--java and --sign-tool are required for signed output")
    if not os.path.isfile(args.sign_tool):
        raise RuntimeError("hap-sign-tool not found: {}".format(args.sign_tool))

    verify_hap(args.java, args.sign_tool, args.signed_hap)
    repack(args.unsigned_app, args.signed_app, args.signed_hap, args.member)
    digest = verify_embedded_hap(
        args.signed_app,
        args.signed_hap,
        args.member,
        args.java,
        args.sign_tool,
    )
    print("Signed APP verified: {} (HAP sha256={})".format(args.signed_app, digest))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print("Signed APP packaging failed: {}".format(error), file=sys.stderr)
        sys.exit(1)
