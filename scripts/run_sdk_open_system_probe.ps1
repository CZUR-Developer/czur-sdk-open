$ErrorActionPreference = 'Stop'

$TaskName = 'SDKOpenSystemProbe'
$ProbeScript = Join-Path $PSScriptRoot 'sdk_open_system_probe.ps1'
$ProbeOutput = 'C:\Windows\Temp\sdk-open-system-probe.txt'
$RunAt = (Get-Date).AddMinutes(1).ToString('HH:mm')

if (-not (Test-Path -LiteralPath $ProbeScript -PathType Leaf)) {
    throw "Probe script not found: $ProbeScript"
}

# The task may not exist on the first run. Use cmd redirection so PowerShell
# does not promote schtasks' expected error text into a terminating exception.
cmd.exe /c "schtasks.exe /Delete /TN $TaskName /F >nul 2>nul"

$taskCommand = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$ProbeScript`""
schtasks.exe /Create /TN $TaskName /TR $taskCommand /SC ONCE /ST $RunAt /RU SYSTEM /F
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create scheduled task: $TaskName"
}

schtasks.exe /Run /TN $TaskName
if ($LASTEXITCODE -ne 0) {
    throw "Failed to run scheduled task: $TaskName"
}

Write-Host "Waiting for SYSTEM probe output: $ProbeOutput"
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 2
    if (Test-Path -LiteralPath $ProbeOutput -PathType Leaf) {
        Write-Host "===== SYSTEM probe output ====="
        Get-Content -LiteralPath $ProbeOutput
        exit 0
    }
}

throw "Timed out waiting for SYSTEM probe output: $ProbeOutput"
