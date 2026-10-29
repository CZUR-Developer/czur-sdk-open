#!/usr/bin/env python3
"""Generate SDK Open release notes from English and Chinese changelogs."""

from __future__ import print_function

import argparse
import io
import os
import re
import sys


HEADING_RE = re.compile(r"^\s*##\s+(.+?)\s*$")


def normalize_heading(raw_heading):
    text = raw_heading.strip()
    if text.startswith("["):
        closing = text.find("]")
        if closing != -1:
            text = text[1:closing]
    else:
        text = re.split(r"\s+[-(]", text, 1)[0]
    if text.startswith("v") or text.startswith("V"):
        text = text[1:]
    return text.strip()


def read_text(path):
    try:
        with io.open(path, "r", encoding="utf-8-sig", errors="replace") as file_obj:
            return file_obj.read().splitlines()
    except IOError as exc:
        raise RuntimeError("Failed to read {0}: {1}".format(path, exc))


def collect_headings(lines):
    headings = []
    for line_number, line in enumerate(lines, 1):
        match = HEADING_RE.match(line)
        if match:
            raw_heading = match.group(1)
            headings.append((line_number, raw_heading, normalize_heading(raw_heading)))
    return headings


def extract_changelog_section(path, version):
    if not os.path.isfile(path):
        raise RuntimeError("Missing changelog file: {0}".format(path))

    lines = read_text(path)
    found = False
    section = []

    for line in lines:
        match = HEADING_RE.match(line)
        if match:
            if found:
                break
            if normalize_heading(match.group(1)) == version:
                found = True
                continue
        elif found:
            section.append(line)

    while section and not section[0].strip():
        section.pop(0)
    while section and not section[-1].strip():
        section.pop()

    if found and section:
        return "\n".join(section)

    headings = collect_headings(lines)
    message = [
        "No changelog section found for version {0} in {1}".format(version, path),
        "Available changelog headings in {0}:".format(path),
    ]
    if headings:
        for line_number, raw_heading, normalized in headings:
            message.append(
                "  {0}: {1} (normalized: {2!r})".format(
                    line_number, raw_heading, normalized
                )
            )
    else:
        message.append("  <none>")

    message.append("First lines of {0}:".format(path))
    for line_number, line in enumerate(lines[:40], 1):
        message.append("  {0}: {1}".format(line_number, line))
    raise RuntimeError("\n".join(message))


def write_release_note(args):
    version = args.version.strip()
    build = args.build.strip()
    stage = args.stage.strip()
    if not version:
        raise RuntimeError("--version must not be empty")
    if not build:
        raise RuntimeError("--build must not be empty")
    if not stage:
        raise RuntimeError("--stage must not be empty")

    english_note = extract_changelog_section(args.english_changelog, version)
    chinese_note = extract_changelog_section(args.chinese_changelog, version)

    content = "\n".join(
        [
            "# SDK Open {0}.{1}.{2}".format(version, build, stage),
            "",
            "## English",
            english_note,
            "",
            "---",
            "",
            "## 中文",
            chinese_note,
            "",
        ]
    )
    with io.open(args.output, "w", encoding="utf-8", newline="\n") as file_obj:
        file_obj.write(content)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Generate SDK Open release_note.md from changelog sections."
    )
    parser.add_argument("--version", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--stage", required=True)
    parser.add_argument("--english-changelog", required=True)
    parser.add_argument("--chinese-changelog", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    try:
        write_release_note(args)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
