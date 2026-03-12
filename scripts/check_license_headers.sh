#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

readonly HEADER_COPYRIGHT='// Copyright (c) 2026 CZUR Tech. All rights reserved.'
readonly HEADER_SPDX='// SPDX-License-Identifier: Apache-2.0'

mapfile -t CPP_FILES < <(find interfaces providers runtime -type f \
  \( -name '*.h' -o -name '*.hpp' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \) | sort)

if [[ ${#CPP_FILES[@]} -eq 0 ]]; then
  echo "No C++ files found under interfaces/providers/runtime."
  exit 0
fi

missing=0
for file in "${CPP_FILES[@]}"; do
  head_block="$(head -n 10 "${file}")"
  if ! grep -Fq "${HEADER_COPYRIGHT}" <<<"${head_block}" || ! grep -Fq "${HEADER_SPDX}" <<<"${head_block}"; then
    echo "MISSING_HEADER ${file}"
    missing=1
  fi
done

if [[ ${missing} -ne 0 ]]; then
  echo "License header check failed."
  exit 1
fi

echo "License header check passed for ${#CPP_FILES[@]} files."

