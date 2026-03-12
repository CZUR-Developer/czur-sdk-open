#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOOKS_DIR="${ROOT_DIR}/.githooks"

if [[ ! -d "${HOOKS_DIR}" ]]; then
  echo "Hooks directory not found: ${HOOKS_DIR}"
  exit 1
fi

chmod +x "${HOOKS_DIR}/pre-commit" "${HOOKS_DIR}/commit-msg"
chmod +x "${ROOT_DIR}/scripts/check_license_headers.sh" "${ROOT_DIR}/scripts/check_commit_message.sh"

git -C "${ROOT_DIR}" config core.hooksPath .githooks

echo "Git hooks installed successfully."
echo "core.hooksPath=$(git -C "${ROOT_DIR}" config --get core.hooksPath)"

