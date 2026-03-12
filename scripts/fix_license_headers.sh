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

updated=0
for file in "${CPP_FILES[@]}"; do
  head_block="$(head -n 10 "${file}")"
  has_copyright=0
  has_spdx=0

  if grep -Fq "${HEADER_COPYRIGHT}" <<<"${head_block}"; then
    has_copyright=1
  fi
  if grep -Fq "${HEADER_SPDX}" <<<"${head_block}"; then
    has_spdx=1
  fi

  if [[ ${has_copyright} -eq 1 && ${has_spdx} -eq 1 ]]; then
    continue
  fi

  tmp_file="$(mktemp)"
  {
    echo "${HEADER_COPYRIGHT}"
    echo "${HEADER_SPDX}"
    echo
    cat "${file}"
  } > "${tmp_file}"
  mv "${tmp_file}" "${file}"
  echo "UPDATED_HEADER ${file}"
  updated=$((updated + 1))
done

echo "Header fix completed. Updated ${updated} files out of ${#CPP_FILES[@]}."
