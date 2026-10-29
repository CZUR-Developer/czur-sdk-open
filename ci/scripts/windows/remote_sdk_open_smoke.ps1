[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'x86')][string]$Architecture,
    [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
    [Parameter(Mandatory = $true)][string]$AutomationArchiveOssUri,
    [Parameter(Mandatory = $true)][string]$AutomationArchiveSha256,
    [Parameter(Mandatory = $true)][string]$AutomationEnvPath,
    [Parameter(Mandatory = $true)][string]$ArtifactOssUri,
    [Parameter(Mandatory = $true)][string]$ReportOssUri,
    [Parameter(Mandatory = $true)][string]$OssutilConfigPath,
    [Parameter(Mandatory = $true)][string]$SdkVersion,
    [Parameter(Mandatory = $true)][string]$SdkBuild,
    [Parameter(Mandatory = $true)][string]$SdkStage,
    [Parameter(Mandatory = $true)][string]$SmartEyeCommit,
    [switch]$SkipToolInstall,
    [ValidateRange(1, 86400)][int]$BootstrapTimeoutSeconds = 3600,
    [ValidateRange(1, 86400)][int]$TestTimeoutSeconds = 1200
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$AutomationDir = Join-Path $WorkspaceRoot 'automation'
$InputDir = Join-Path $WorkspaceRoot 'input'
$ReportDir = Join-Path $WorkspaceRoot 'report'
$AutomationArchive = Join-Path $InputDir 'smarteye-automation.tar.gz'
$ChecksumFile = Join-Path $InputDir 'SHA256SUMS.txt'
$InstallerName = "sdk-open_Windows_${Architecture}_v${SdkVersion}.${SdkBuild}.${SdkStage}.exe"
$InstallerPath = Join-Path $InputDir $InstallerName
$SmokeLog = Join-Path $ReportDir 'smoke-test.log'
$StartedAt = (Get-Date).ToUniversalTime().ToString('o')
$RunStatus = 0
$FailureMessage = ''

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Invoke-NativeChecked {
    param([string]$Command, [string[]]$Arguments, [string]$Description)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Write-SmokeEvent {
    param([Parameter(Mandatory = $true)][string]$Message)

    $line = "$(Get-Date -Format o) $Message"
    Write-Host $line
    Add-Content -LiteralPath $SmokeLog -Value $line -Encoding UTF8
}

function Copy-NewProcessOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$StreamLabel,
        [Parameter(Mandatory = $true)][ref]$LineOffset
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return
    }
    $lines = @(Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue)
    if ($lines.Count -le $LineOffset.Value) {
        return
    }
    for ($index = [int]$LineOffset.Value; $index -lt $lines.Count; $index++) {
        $line = "[$StreamLabel] $($lines[$index])"
        Write-Host $line
        Add-Content -LiteralPath $SmokeLog -Value $line -Encoding UTF8
    }
    $LineOffset.Value = $lines.Count
}

function Stop-ProcessTree {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)

    if ($Process.HasExited) {
        return
    }
    Write-SmokeEvent "process_tree pid=$($Process.Id) status=terminating"
    $taskKillOutput = (& taskkill.exe /PID $Process.Id /T /F 2>&1 | Out-String).Trim()
    if ($taskKillOutput) {
        Add-Content -LiteralPath $SmokeLog -Value $taskKillOutput -Encoding UTF8
    }
    if (-not $Process.WaitForExit(30000)) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        [void]$Process.WaitForExit(5000)
    }
}

function Invoke-LoggedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Label,
        [ValidateRange(1, 86400)][int]$TimeoutSeconds
    )

    $stdoutPath = Join-Path $ReportDir "$Label.stdout.log"
    $stderrPath = Join-Path $ReportDir "$Label.stderr.log"
    Write-SmokeEvent "stage=$Label status=started timeout_seconds=$TimeoutSeconds"
    $process = Start-Process -FilePath $FilePath `
        -ArgumentList $Arguments `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath
    $stdoutOffset = 0
    $stderrOffset = 0
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $timedOut = $false
    while (-not $process.HasExited) {
        Copy-NewProcessOutput -Path $stdoutPath -StreamLabel "$Label stdout" -LineOffset ([ref]$stdoutOffset)
        Copy-NewProcessOutput -Path $stderrPath -StreamLabel "$Label stderr" -LineOffset ([ref]$stderrOffset)
        if ([DateTime]::UtcNow -ge $deadline) {
            $timedOut = $true
            Write-SmokeEvent "stage=$Label status=timed_out pid=$($process.Id)"
            Stop-ProcessTree -Process $process
            break
        }
        Start-Sleep -Seconds 5
    }
    if ($process.HasExited) {
        $process.WaitForExit()
    }
    # Start-Process can retain a stale Process object after the child exits.
    # Refresh before reading ExitCode; otherwise a successful bootstrap may be
    # reported as a failed process with an empty exit code.
    $process.Refresh()
    Copy-NewProcessOutput -Path $stdoutPath -StreamLabel "$Label stdout" -LineOffset ([ref]$stdoutOffset)
    Copy-NewProcessOutput -Path $stderrPath -StreamLabel "$Label stderr" -LineOffset ([ref]$stderrOffset)
    if ($timedOut) {
        throw "$Label timed out after $TimeoutSeconds seconds"
    }
    $exitCode = [int]$process.ExitCode
    if ($exitCode -ne 0) {
        Write-SmokeEvent "stage=$Label status=failed exit_code=$exitCode"
        throw "$Label failed with exit code $exitCode"
    }
    Write-SmokeEvent "stage=$Label status=completed exit_code=0"
}

function Read-PlaywrightUnexpectedCount {
    param([Parameter(Mandatory = $true)][string]$ResultsPath)

    # Playwright results can contain large attachment payloads and non-ASCII
    # text.  PowerShell's ConvertFrom-Json is unreliable for this file on the
    # Windows images, so use the Node runtime that the smoke suite already
    # requires to parse it as UTF-8.
    # PowerShell 5.1 strips embedded double quotes when passing a -e script to
    # a native executable. Write the parser to a temporary file so Node gets
    # the JavaScript source unchanged. For a script file, the input path is
    # process.argv[2] (argv[1] is the script path itself).
    $nodeScript = 'const fs=require("fs"); const p=process.argv[2]; const r=JSON.parse(fs.readFileSync(p,"utf8")); if (!r.stats || !Number.isInteger(r.stats.unexpected)) process.exit(2); process.stdout.write(String(r.stats.unexpected));'
    $nodeScriptPath = Join-Path $env:TEMP ("sdk-open-playwright-parser-{0}.js" -f ([Guid]::NewGuid().ToString('N')))
    try {
        Set-Content -LiteralPath $nodeScriptPath -Value $nodeScript -Encoding UTF8
        $unexpectedOutput = & node.exe $nodeScriptPath $ResultsPath
        $nodeExitCode = $LASTEXITCODE
        if ($nodeExitCode -ne 0) {
            throw "Unable to parse Playwright smoke results with Node (exit code $nodeExitCode): $ResultsPath"
        }
        $unexpectedText = ($unexpectedOutput | Out-String).Trim()
        $unexpectedCount = 0
        if (-not [int]::TryParse($unexpectedText, [ref]$unexpectedCount)) {
            throw "Playwright smoke results did not contain an integer stats.unexpected: $ResultsPath"
        }
        return $unexpectedCount
    }
    finally {
        Remove-Item -LiteralPath $nodeScriptPath -Force -ErrorAction SilentlyContinue
    }
}

function Get-BoundedFailureMessage {
    param([Parameter(Mandatory = $true)][object]$ErrorRecord)

    $message = if ($ErrorRecord.Exception) {
        [string]$ErrorRecord.Exception.Message
    } else {
        [string]$ErrorRecord
    }
    $maxLength = 4096
    if ($message.Length -gt $maxLength) {
        return $message.Substring(0, $maxLength) + ' ... [truncated]'
    }
    return $message
}

function Publish-ReportDirectory {
    $files = @(Get-ChildItem -LiteralPath $ReportDir -Recurse -File)
    if ($files.Count -eq 0) {
        throw "SDK Open smoke report directory is empty: $ReportDir"
    }
    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($ReportDir.Length).TrimStart('\').Replace('\', '/')
        $targetUri = "$($ReportOssUri.TrimEnd('/'))/$relativePath"
        Invoke-NativeChecked 'ossutil' @('cp', $file.FullName, $targetUri, '-f', '-c', $OssutilConfigPath) "Upload smoke report $relativePath"
    }
}

Remove-Item -LiteralPath $WorkspaceRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $AutomationDir, $InputDir, $ReportDir -Force | Out-Null
Set-Content -LiteralPath $SmokeLog -Value "SDK Open Windows $Architecture smoke started: $StartedAt" -Encoding UTF8
Write-SmokeEvent "stage=initialize status=completed target=windows-$Architecture"

try {
    Write-SmokeEvent 'stage=preflight status=started'
    Require-Command 'ossutil'
    Require-Command 'tar.exe'
    if (-not (Test-Path -LiteralPath $OssutilConfigPath -PathType Leaf)) {
        throw "ossutil config not found: $OssutilConfigPath"
    }
    if (-not (Test-Path -LiteralPath $AutomationEnvPath -PathType Leaf)) {
        throw "SmartEye environment file not found: $AutomationEnvPath"
    }
    Write-SmokeEvent 'stage=preflight status=completed'

    Write-SmokeEvent 'stage=download-inputs status=started'
    Invoke-NativeChecked 'ossutil' @('cp', $AutomationArchiveOssUri, $AutomationArchive, '-f', '-c', $OssutilConfigPath) 'Download SmartEye automation archive'
    $actualAutomationHash = (Get-FileHash -LiteralPath $AutomationArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualAutomationHash -ne $AutomationArchiveSha256.ToLowerInvariant()) {
        throw "SmartEye automation archive SHA-256 mismatch: expected=$AutomationArchiveSha256, actual=$actualAutomationHash"
    }

    Invoke-NativeChecked 'ossutil' @('cp', "$($ArtifactOssUri.TrimEnd('/'))/SHA256SUMS.txt", $ChecksumFile, '-f', '-c', $OssutilConfigPath) 'Download Windows artifact checksums'
    Invoke-NativeChecked 'ossutil' @('cp', "$($ArtifactOssUri.TrimEnd('/'))/$InstallerName", $InstallerPath, '-f', '-c', $OssutilConfigPath) 'Download Windows SDK Open installer'
    $checksumLine = Get-Content -LiteralPath $ChecksumFile | Where-Object { $_ -match "\s+$([Regex]::Escape($InstallerName))$" } | Select-Object -First 1
    if (-not $checksumLine) {
        throw "Installer checksum not found in SHA256SUMS.txt: $InstallerName"
    }
    $expectedInstallerHash = ($checksumLine -split '\s+')[0].ToLowerInvariant()
    $actualInstallerHash = (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualInstallerHash -ne $expectedInstallerHash) {
        throw "Installer SHA-256 mismatch: expected=$expectedInstallerHash, actual=$actualInstallerHash"
    }
    Write-SmokeEvent 'stage=download-inputs status=completed'

    Write-SmokeEvent 'stage=extract-automation status=started'
    Invoke-NativeChecked 'tar.exe' @('-xzf', $AutomationArchive, '-C', $AutomationDir) 'Extract SmartEye automation archive'
    Copy-Item -LiteralPath $AutomationEnvPath -Destination (Join-Path $AutomationDir '.env') -Force
    Write-SmokeEvent 'stage=extract-automation status=completed'

    $env:SDK_EXPECTED_VERSION = $SdkVersion
    $env:SDK_EXPECTED_BUILD = $SdkBuild
    $bootstrapScript = Join-Path $AutomationDir 'scripts\bootstrap-windows.ps1'
    $bootstrapArguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $bootstrapScript,
        '-SdkInstallerPath', $InstallerPath,
        '-SdkInstallerSha256', $actualInstallerHash
    )
    if ($SkipToolInstall) {
        $bootstrapArguments += '-SkipToolInstall'
    }
    Invoke-LoggedProcess 'powershell.exe' $bootstrapArguments 'bootstrap' $BootstrapTimeoutSeconds

    Write-SmokeEvent 'stage=verify-service status=started'
    $service = Get-Service -Name 'sdk-open' -ErrorAction Stop
    if ($service.Status -ne 'Running') {
        throw "sdk-open service is not running after installation: $($service.Status)"
    }
    Write-SmokeEvent 'stage=verify-service status=completed'

    Push-Location $AutomationDir
    try {
        Invoke-LoggedProcess 'npm.cmd' @('run', 'test:full:no-device') 'test-full-no-device' $TestTimeoutSeconds
        $resultsPath = Join-Path $AutomationDir 'artifacts\results.json'
        if (-not (Test-Path -LiteralPath $resultsPath -PathType Leaf)) {
            throw "Smoke test results file is missing: $resultsPath"
        }
        $unexpectedCount = Read-PlaywrightUnexpectedCount -ResultsPath $resultsPath
        if ($unexpectedCount -gt 0) {
            throw "Smoke report contains $unexpectedCount unexpected test failure(s)"
        }
    }
    finally {
        Pop-Location
    }
}
catch {
    $RunStatus = 1
    $FailureMessage = Get-BoundedFailureMessage -ErrorRecord $_
    Add-Content -LiteralPath $SmokeLog -Value "SDK Open Windows smoke failed: $FailureMessage" -Encoding UTF8
    Add-Content -LiteralPath $SmokeLog -Value $_.ScriptStackTrace -Encoding UTF8
}
finally {
    try {
        Write-SmokeEvent 'stage=collect-report status=started'
        $service = Get-Service -Name 'sdk-open' -ErrorAction SilentlyContinue
        if ($service) {
            $service | Format-List * | Out-String | Add-Content -LiteralPath $SmokeLog -Encoding UTF8
            Get-WinEvent -LogName System -MaxEvents 120 -ErrorAction SilentlyContinue |
                Where-Object { $_.ProviderName -eq 'Service Control Manager' -and $_.Message -match 'sdk-open|sdk_open_app' } |
                Format-List TimeCreated, Id, LevelDisplayName, Message |
                Out-String |
                Add-Content -LiteralPath $SmokeLog -Encoding UTF8
        }

        $artifactsDir = Join-Path $AutomationDir 'artifacts'
        if (Test-Path -LiteralPath $artifactsDir -PathType Container) {
            Get-ChildItem -LiteralPath $artifactsDir -Recurse -File -Filter 'trace.zip' | Remove-Item -Force
            Remove-Item -LiteralPath (Join-Path $artifactsDir 'blob-report') -Recurse -Force -ErrorAction SilentlyContinue
            Copy-Item -LiteralPath $artifactsDir -Destination (Join-Path $ReportDir 'artifacts') -Recurse -Force
        }

        [ordered]@{
            target = "windows-$Architecture"
            sdk_version = $SdkVersion
            sdk_build = $SdkBuild
            sdk_stage = $SdkStage
            installer = $InstallerName
            installer_sha256 = if (Test-Path -LiteralPath $InstallerPath) { (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash.ToLowerInvariant() } else { '' }
            smarteye_branch = '3.2AI'
            smarteye_commit = $SmartEyeCommit
            started_at = $StartedAt
            finished_at = (Get-Date).ToUniversalTime().ToString('o')
            exit_code = $RunStatus
            status = if ($RunStatus -eq 0) { 'passed' } else { 'failed' }
            failure = $FailureMessage
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ReportDir 'metadata.json') -Encoding UTF8

        Write-SmokeEvent 'stage=collect-report status=completed'
        Write-SmokeEvent 'stage=publish-report status=started'
        Publish-ReportDirectory
        Write-Host "$(Get-Date -Format o) stage=publish-report status=completed uri=$ReportOssUri"
    }
    catch {
        $RunStatus = 1
        $FailureMessage = "Smoke report collection/upload failed: $($_.Exception.Message)"
        Write-Error $FailureMessage
    }
    finally {
        Remove-Item -LiteralPath $AutomationEnvPath -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $AutomationDir '.env') -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $OssutilConfigPath -Force -ErrorAction SilentlyContinue
    }
}

if ($RunStatus -ne 0) {
    throw "SDK Open Windows $Architecture smoke failed: $FailureMessage"
}

Write-Host "SDK Open Windows $Architecture smoke passed. Report: $ReportOssUri"
