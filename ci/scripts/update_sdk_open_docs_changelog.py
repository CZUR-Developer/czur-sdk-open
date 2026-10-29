#!/usr/bin/env python3
"""Prepend an SDK Open changelog section to the open-docs site changelogs."""
import argparse
import re
from pathlib import Path

HEADING_RE = re.compile(r"^\s*##\s+(.+?)\s*$")

def normalize(value):
    value = value.strip()
    if value.startswith("[") and "]" in value:
        value = value[1:value.find("]")]
    else:
        value = re.split(r"\s+[-(]", value, 1)[0]
    return value[1:] if value[:1].lower() == "v" else value

def extract(path, version):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    found, body = False, []
    for line in lines:
        match = HEADING_RE.match(line)
        if match:
            if found:
                break
            found = normalize(match.group(1)) == normalize(version)
            continue
        if found:
            body.append(line)
    while body and not body[0].strip(): body.pop(0)
    while body and not body[-1].strip(): body.pop()
    if not found or not body:
        raise RuntimeError("missing changelog section for {} in {}".format(version, path))
    return body

def update(path, version, date, body):
    text = path.read_text(encoding="utf-8")
    if re.search(r"^##\s+\[?v?{}\]?\b".format(re.escape(version)), text, re.M | re.I):
        raise RuntimeError("tag {} already exists in {}".format(version, path))
    section = "## [{}] - {}\n\n{}\n\n".format(version, date, "\n".join(body))
    text = text.lstrip("\ufeff")
    first_version = re.search(r"^##\s+", text, re.M)
    if first_version is None:
        raise RuntimeError("missing version heading in {}".format(path))
    text = text[:first_version.start()] + section + text[first_version.start():]
    # Path.write_text() only accepts newline= on Python 3.10+. Jenkins uses
    # Python 3.6, while the built-in open() newline argument is available there.
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(text)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--date", required=True)
    parser.add_argument("--english-source", required=True)
    parser.add_argument("--chinese-source", required=True)
    parser.add_argument("--english-target", required=True)
    parser.add_argument("--chinese-target", required=True)
    args = parser.parse_args()
    english = extract(Path(args.english_source), args.version)
    chinese = extract(Path(args.chinese_source), args.version)
    update(Path(args.english_target), args.version, args.date, english)
    update(Path(args.chinese_target), args.version, args.date, chinese)

if __name__ == "__main__":
    main()
