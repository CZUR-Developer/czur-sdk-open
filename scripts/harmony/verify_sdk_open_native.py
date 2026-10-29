#!/usr/bin/env python3
"""Reject missing SONAMEs, non-OHOS dependencies and codec symbol collisions."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hap", type=Path)
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get(
        "DEVECO_SDK_HOME", "/Applications/DevEco-Studio.app/Contents/sdk")))
    args = parser.parse_args()
    native = args.sdk / "default/openharmony/native"
    readelf = native / "llvm/bin/llvm-readelf"
    system_libs = {p.name for p in (native / "sysroot/usr/lib/aarch64-linux-ohos").glob("*.so")}
    if not system_libs or not readelf.is_file():
        parser.error("SDK must contain llvm-readelf and the OHOS ARM64 sysroot")

    errors = []
    with zipfile.ZipFile(args.hap) as hap, tempfile.TemporaryDirectory(prefix="sdkopen-elf-") as temp:
        members = [m for m in hap.namelist() if m.startswith("libs/arm64-v8a/") and ".so" in m]
        packaged = {Path(m).name for m in members}
        if "libsdkopen_harmony.so" not in packaged:
            errors.append("libsdkopen_harmony.so is missing")
        for member in members:
            path = Path(temp) / Path(member).name
            path.write_bytes(hap.read(member))
            output = subprocess.check_output([str(readelf), "-h", "-d", str(path)], text=True)
            if not re.search(r"Machine:\s+AArch64", output):
                errors.append(f"{path.name}: not an AArch64 ELF")
            needed = re.findall(r"Shared library: \[(.*?)\]", output)
            if path.name in {"libsdkopen_harmony.so", "libdocx.so"}:
                symbols = subprocess.check_output(
                    [str(readelf), "--dyn-syms", "--wide", str(path)], text=True)
                for line in symbols.splitlines():
                    fields = line.split()
                    if (len(fields) >= 8 and fields[4] in {"GLOBAL", "WEAK"}
                            and fields[5] in {"DEFAULT", "PROTECTED"} and fields[6] != "UND"
                            and fields[7].startswith("jpeg_")):
                        errors.append(f"{path.name}: exports private JPEG symbol {fields[7]}; "
                                      "MuPDF codecs must not override OpenCV's libjpeg")
            for dependency in needed:
                if dependency not in packaged and dependency not in system_libs:
                    errors.append(f"{path.name}: unresolved dependency {dependency}")
                if dependency in {"libc.so.6", "libstdc++.so.6", "ld-linux-aarch64.so.1"}:
                    errors.append(f"{path.name}: Linux ABI dependency {dependency}")
    if errors:
        parser.exit(1, "\n".join(errors) + "\n")
    print(f"[sdk-open-harmony] PASS native ABI/NEEDED: {len(members)} libraries")


if __name__ == "__main__":
    main()
