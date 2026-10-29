"""Run the Windows pnpm bootstrap logic in PowerShell with offline tool doubles."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[5]
WINDOWS_DIR = REPO_ROOT / "src/sdk_open/ci/scripts/windows"


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell is required")
class SdkOpenPnpmTest(unittest.TestCase):
    def run_powershell(self, body, *, mock_tools=True):
        with tempfile.TemporaryDirectory(prefix="sdk pnpm test ") as workspace:
            env = os.environ.copy()
            for key in list(env):
                if key.upper().startswith("COREPACK_") or key.lower() == "npm_config_registry":
                    del env[key]
            env["SDK_PNPM_TEST_ROOT"] = workspace
            env["SDK_PNPM_HELPER"] = str(WINDOWS_DIR / "sdk_open_pnpm.ps1")
            env["SDK_PNPM_BUILD"] = str(WINDOWS_DIR / "remote_sdk_open_build.ps1")
            setup = r'''
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. $env:SDK_PNPM_HELPER
function Assert($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
$workspace = $env:SDK_PNPM_TEST_ROOT
$projects = @((Join-Path $workspace 'admin site'), (Join-Path $workspace 'demo site'))
$descriptor = 'pnpm@9.15.9+sha512.1234abcd'
foreach ($project in $projects) {
    New-Item -ItemType Directory -Path $project | Out-Null
    @{packageManager=$descriptor} | ConvertTo-Json | Set-Content (Join-Path $project 'package.json')
}
$script:prepareCalls = 0
$script:verifyCalls = 0
$script:sleeps = 0
$script:failures = 0
$script:reportedVersion = '9.15.9'
$script:seenProjects = @()
$originalLocation = (Get-Location).Path
'''
            mock = r'''
function Start-Sleep { param([int]$Seconds); $script:sleeps++ }
function Invoke-SdkOpenPnpmTool {
    param([string]$Command, [string[]]$Arguments)
    if ($Command -eq 'corepack.cmd') {
        switch ($Arguments[0]) {
            'enable' {
                Assert ($Arguments[1] -eq '--install-directory') 'Must use a local shim directory'
                Set-Content (Join-Path $Arguments[2] 'pnpm.cmd') 'fake shim'
            }
            'prepare' {
                $script:prepareCalls++
                Assert ($Arguments[1] -eq $descriptor) 'Integrity descriptor was changed'
                Assert ($Arguments.Count -eq 2) 'Must not activate a global default'
                if ($script:prepareCalls -le $script:failures) { throw 'simulated connection timeout' }
            }
            default { throw "Unexpected Corepack command: $Arguments" }
        }
    } else {
        $script:verifyCalls++
        Assert ($Command -eq (Join-Path $workspace 'tools/pnpm/pnpm.cmd')) 'Wrong pnpm shim'
        Assert ($Arguments[0] -eq '--version') 'Must actually run pnpm'
        Assert ($env:COREPACK_ENABLE_NETWORK -eq '0') 'Preflight must use the cache'
        $script:seenProjects += (Get-Location).Path
        return $script:reportedVersion
    }
}
'''
            result = subprocess.run(
                ["pwsh", "-NoProfile", "-NonInteractive", "-Command", setup + (mock if mock_tools else "") + body],
                env=env,
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_bootstrap_uses_build_cache_and_checks_both_projects(self):
        self.run_powershell(r'''
$before = Get-Content (Join-Path $projects[0] 'package.json') -Raw
$command = Initialize-SdkOpenPnpm -WorkspaceRoot $workspace -ProjectDirs $projects
Assert ($command -is [string]) 'Tool output leaked into the returned command'
Assert ($command -eq (Join-Path $workspace 'tools/pnpm/pnpm.cmd')) 'Unexpected command'
Assert ($script:prepareCalls -eq 1 -and $script:verifyCalls -eq 2) 'Incorrect tool calls'
Assert (($script:seenProjects -join '|') -eq ($projects -join '|')) 'Wrong project context'
Assert ($env:COREPACK_HOME -eq (Join-Path $workspace 'cache/corepack')) 'Wrong cache'
Assert ($env:COREPACK_NPM_REGISTRY -eq 'https://registry.npmmirror.com') 'Wrong Corepack registry'
Assert ($env:npm_config_registry -eq $env:COREPACK_NPM_REGISTRY) 'Wrong dependency registry'
Assert ($env:COREPACK_ENABLE_DOWNLOAD_PROMPT -eq '0') 'Interactive download prompt enabled'
Assert ($env:COREPACK_ENABLE_AUTO_PIN -eq '0') 'Auto pin enabled'
Assert ($env:COREPACK_DEFAULT_TO_LATEST -eq '0') 'Unexpected latest-version lookup enabled'
Assert ($env:PATH.StartsWith((Split-Path $command) + [IO.Path]::PathSeparator)) 'Shim not on PATH'
Assert ((Get-Location).Path -eq $originalLocation) 'Location was not restored'
Assert ((Get-Content (Join-Path $projects[0] 'package.json') -Raw) -eq $before) 'Manifest modified'
''')

    def test_existing_registry_and_cache_overrides_are_preserved(self):
        self.run_powershell(r'''
$env:COREPACK_HOME = Join-Path $workspace 'custom cache'
$env:COREPACK_NPM_REGISTRY = 'https://corepack.example.test'
$env:npm_config_registry = 'https://dependencies.example.test'
Initialize-SdkOpenPnpm -WorkspaceRoot $workspace -ProjectDirs $projects | Out-Null
Assert ($env:COREPACK_HOME -eq (Join-Path $workspace 'custom cache')) 'Cache override lost'
Assert ($env:COREPACK_NPM_REGISTRY -eq 'https://corepack.example.test') 'Corepack override lost'
Assert ($env:npm_config_registry -eq 'https://dependencies.example.test') 'Dependency override lost'
''')

    def test_transient_download_failure_is_retried(self):
        self.run_powershell(r'''
$script:failures = 2
Initialize-SdkOpenPnpm -WorkspaceRoot $workspace -ProjectDirs $projects | Out-Null
Assert ($script:prepareCalls -eq 3 -and $script:sleeps -eq 2) 'Retry policy not applied'
Assert ($script:verifyCalls -eq 2) 'Did not verify after retry'
''')

    def test_persistent_failure_stops_before_preflight(self):
        self.run_powershell(r'''
$script:failures = 3
$message = ''
try { Initialize-SdkOpenPnpm -WorkspaceRoot $workspace -ProjectDirs $projects | Out-Null }
catch { $message = $_.Exception.Message }
Assert ($message -like '*preparation failed after 3 attempts*') 'Missing actionable failure'
Assert ($script:prepareCalls -eq 3 -and $script:verifyCalls -eq 0) 'Continued after failure'
''')

    def test_wrong_version_fails_and_restores_location(self):
        self.run_powershell(r'''
$script:reportedVersion = '10.0.0'
$message = ''
try { Initialize-SdkOpenPnpm -WorkspaceRoot $workspace -ProjectDirs $projects | Out-Null }
catch { $message = $_.Exception.Message }
Assert ($message -like '*version mismatch*expected 9.15.9, got 10.0.0*') 'Wrong version accepted'
Assert ((Get-Location).Path -eq $originalLocation) 'Location was not restored after failure'
''')

    def test_missing_or_conflicting_version_fails_before_download(self):
        for manifest in ('{}', '{"packageManager":"pnpm@10.0.0"}', '{"packageManager":"pnpm@latest"}'):
            with self.subTest(manifest=manifest):
                self.run_powershell("Set-Content (Join-Path $projects[1] 'package.json') '" + manifest + "'\n" + r'''
$message = ''
try { Initialize-SdkOpenPnpm -WorkspaceRoot $workspace -ProjectDirs $projects | Out-Null }
catch { $message = $_.Exception.Message }
Assert ($message -like '*packageManager*') 'Invalid version declaration accepted'
Assert ($script:prepareCalls -eq 0 -and $script:verifyCalls -eq 0) 'Started tools on invalid manifests'
''')

    def test_native_nonzero_exit_is_propagated(self):
        self.run_powershell(r'''
$message = ''
try { Invoke-SdkOpenPnpmTool (Get-Command pwsh).Source @('-NoProfile', '-Command', 'exit 7') }
catch { $message = $_.Exception.Message }
Assert ($message -like '*exit code 7*') 'Native failure was ignored'
''', mock_tools=False)

    def test_powershell_syntax(self):
        self.run_powershell(r'''
foreach ($path in @($env:SDK_PNPM_HELPER, $env:SDK_PNPM_BUILD)) {
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$null, [ref]$parseErrors) | Out-Null
    Assert ($parseErrors.Count -eq 0) "PowerShell syntax errors: $parseErrors"
}
''', mock_tools=False)

    def test_build_prepares_before_cmake_and_overrides_cached_pnpm_paths(self):
        build = (WINDOWS_DIR / "remote_sdk_open_build.ps1").read_text()
        self.assertLess(build.index("Initialize-SdkOpenPnpm"), build.index("Invoke-CheckedCommand 'cmake'"))
        self.assertIn('"-DPNPM_CMD_EXECUTABLE:FILEPATH=$SdkOpenPnpmCommand"', build)
        self.assertIn('"-DPNPM_EXECUTABLE:FILEPATH=$SdkOpenPnpmCommand"', build)


if __name__ == "__main__":
    unittest.main()
