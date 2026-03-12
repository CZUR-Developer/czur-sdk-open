# Contributing Guide

Thank you for contributing to `sdk_open`.

## License Header Policy (C/C++ Sources)

All C/C++ source and header files under `interfaces/`, `providers/`, and `runtime/` must include this exact two-line header at the top of the file:

```cpp
// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
```

Rules:

1. Keep the two lines verbatim.
2. Do not add an `Author` line.
3. Place the header before any include guards, includes, or code.

## Helpful Scripts

Run header check:

```bash
bash scripts/check_license_headers.sh
```

Auto-fix missing headers:

```bash
bash scripts/fix_license_headers.sh
```

Validate commit message:

```bash
bash scripts/check_commit_message.sh .git/COMMIT_EDITMSG
```

## Git Hooks

Install local git hooks:

```bash
bash scripts/install_git_hooks.sh
```

Installed hooks:

1. `pre-commit`: runs `scripts/check_license_headers.sh`
2. `commit-msg`: runs `scripts/check_commit_message.sh`

## Commit Message Convention

Use the following commit message formats:

```text
Feature(scope): subject
Fix(scope): subject
BugFix(scope): subject
Perf(scope): subject
Refactor(scope): subject
Docs(scope): subject
Test(scope): subject
Chore(scope): subject
```
