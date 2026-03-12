#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <commit-msg-file>"
  exit 1
fi

msg_file="$1"
if [[ ! -f "${msg_file}" ]]; then
  echo "Commit message file not found: ${msg_file}"
  exit 1
fi

first_line="$(sed -n '1{s/[[:space:]]*$//;p;q}' "${msg_file}")"

# Allow Git-generated merge/revert commits.
if [[ "${first_line}" =~ ^Merge[[:space:]] ]] || [[ "${first_line}" =~ ^Revert[[:space:]] ]]; then
  exit 0
fi

readonly PATTERN='^(Feature|Fix|BugFix|Perf|Refactor|Docs|Test|Chore)\([a-zA-Z0-9._/-]+\): .+'

if [[ ! "${first_line}" =~ ${PATTERN} ]]; then
  echo "Invalid commit message format:"
  echo "  ${first_line}"
  echo
  echo "Expected:"
  echo "  Feature(scope): subject"
  echo "  Fix(scope): subject"
  echo "  BugFix(scope): subject"
  echo "  Perf(scope): subject"
  echo "  Refactor(scope): subject"
  echo "  Docs(scope): subject"
  echo "  Test(scope): subject"
  echo "  Chore(scope): subject"
  exit 1
fi

