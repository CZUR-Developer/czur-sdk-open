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
        if "libz.so" not in packaged:
            errors.append("libz.so is missing; CZOCR runtime loads this SONAME directly")
        if "libs/arm64-v8a/libczcv_ocr.so" not in members:
            errors.append("libczcv_ocr.so is missing from the HAP native library directory")
        if any(m.startswith("libs/arm64-v8a/models/") for m in hap.namelist()):
            errors.append("OCR model data must be packaged only as rawfile, not duplicated in libs/")
        for name in ("license.lic", "1.dat", "3_1_1.model", "3_1_2.model",
                     "3_1_3.model", "3_2_1.model", "3_2_6.model", "6.dat", "3_3_1.model", "3_4_1.model",
                     "3_5_1.model", "4_2_2.model"):
            member = "resources/rawfile/sdkopen_czocr_model/" + name
            if member not in hap.namelist() or hap.getinfo(member).file_size == 0:
                errors.append(f"CZOCR model resource is missing or empty: {name}")
        if "resources/rawfile/sdkopen_ocr_fonts/SourceHanSansCN-Normal.otf" not in hap.namelist():
            errors.append("OCR PDF text-layer font is missing")
        version_member = "resources/rawfile/sdkopen_czocr_model_version.txt"
        if version_member not in hap.namelist():
            errors.append("CZOCR model resource version is missing")
        elif not re.fullmatch(rb"[a-f0-9]{64}", hap.read(version_member)):
            errors.append("CZOCR model resource version is invalid")
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
