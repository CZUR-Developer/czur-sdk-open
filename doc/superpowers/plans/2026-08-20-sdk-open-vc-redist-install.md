# SDK Open VC++ Runtime Installation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make each SDK Open Windows installer package and silently install only the VC++ 14.44.35211 runtime matching the package architecture.

**Architecture:** The PowerShell packaging entry chooses one repository Redistributable from `-Architecture` and passes its source path and retained filename to NSIS. NSIS extracts that file into `$INSTDIR` and invokes the existing install helper before service registration; the helper normalizes successful/reboot/already-installed exit codes and turns every other result into an installer failure.

**Tech Stack:** PowerShell 5.1, NSIS, Python `unittest` static structure checks, Markdown.

---

### Task 1: Add static regression coverage

**Files:**
- Modify: `src/sdk_open/ci/scripts/tests/test_sdk_open_windows_installer.py`

- [x] Add `BUILD_INSTALLER_SCRIPT` for `build-sdk-open-nsis.ps1`.
- [x] Add a test asserting the build script maps x64 only to `VC_redist.x64 14.44.35211.exe`, x86 only to `VC_redist.x86 14.44.35211.exe`, requires the selected file, and passes `VC_REDIST_PATH` plus `VC_REDIST_FILE_NAME` to NSIS.
- [x] Add a test asserting NSIS retains `${VC_REDIST_FILE_NAME}` in `$INSTDIR`, calls helper action `InstallVCRedist` with `/install /quiet /norestart`, and does so before `--install-service`.
- [x] Add a test asserting the helper accepts `0`, `3010`, `1638`, and signed HRESULT `-2147023258`, while its catch path exits `1`.
- [x] Do not execute the test unless the user explicitly authorizes test execution.

### Task 2: Select the architecture-matched payload during packaging

**Files:**
- Modify: `src/sdk_open/packaging/windows/build-sdk-open-nsis.ps1`

- [x] Define the versioned x64 and x86 filenames next to the repository paths.
- [x] Select exactly one filename using the existing validated `-Architecture` value.
- [x] Call `Require-File` for the selected source before staging/building.
- [x] Append `/DVC_REDIST_PATH=<source>` and `/DVC_REDIST_FILE_NAME=<filename>` to `$makensisArgs`.
- [x] Preserve the existing runtime staging, TWAIN helper, PDB, signing, and uninstaller-only behavior.

### Task 3: Retain and silently install the runtime

**Files:**
- Modify: `src/sdk_open/packaging/windows/sdk-open-minimal.nsi`
- Modify: `src/sdk_open/packaging/windows/sdk-open-install-helper.ps1`

- [x] Require `VC_REDIST_PATH` and `VC_REDIST_FILE_NAME` NSIS defines.
- [x] In the normal install path, extract the selected payload with `File "/oname=${VC_REDIST_FILE_NAME}" "${VC_REDIST_PATH}"` after the existing helper is installed.
- [x] Invoke the helper with `-Action InstallVCRedist -InstallerPath "$INSTDIR\${VC_REDIST_FILE_NAME}"` before TLS setup and before service registration.
- [x] Extend the helper action set with `InstallVCRedist` and add optional `InstallerPath`.
- [x] Validate that `InstallerPath` exists, run it with `/install /quiet /norestart`, and treat `0`, `3010`, `1638`, and `-2147023258` as success.
- [x] Log the normalized result through `Write-InstallDiagnostic`; throw on every other exit code so the existing catch block exits `1`.
- [x] Leave `GenerateUninstallerOnly` before extraction so temporary signed-uninstaller generation does not run the prerequisite installer.

### Task 4: Document and statically review

**Files:**
- Modify: `src/sdk_open/packaging/windows/README.md`

- [x] Document the strict same-architecture mapping, retained installer filename, silent arguments, accepted results, install ordering, and uninstall boundary.
- [x] Inspect `git diff` for unrelated changes and whitespace errors.
- [x] Parse the changed PowerShell files with the PowerShell parser without executing them.
- [x] Do not run Python tests, NSIS builds, installers, services, or application smoke tests unless the user explicitly authorizes them.
