param(
    [string]$ExePath = "",
    [string]$ServiceName = "CZURSdkOpenAppTest",
    [string]$DisplayName = "CZUR SDK Open App Test",
    [string]$ConfigPath = "",
    [string]$AuthToken = "service-smoke-token",
    [int]$AdminPort = 17080,
    [int]$StartupTimeoutSec = 45,
    [switch]$KeepService
)

$ErrorActionPreference = "Stop"

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Resolve-DefaultExePath {
    $scriptDir = Split-Path -Parent $PSCommandPath
    $repoRoot = Resolve-Path (Join-Path $scriptDir "..\..\..")
    $candidates = @(
        (Join-Path $repoRoot "cmake-build-open_sdk_debug\Debug\sdk_open_app.exe"),
        (Join-Path $repoRoot "cmake-build-open_sdk_release\Release\sdk_open_app.exe"),
        (Join-Path $repoRoot "src\sdk_open\build\Debug\sdk_open_app.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }
    throw "sdk_open_app.exe not found. Pass -ExePath explicitly."
}

function Invoke-Step {
    param(
        [string]$Title,
        [scriptblock]$Action
    )
    Write-Host ""
    Write-Host "==> $Title"
    & $Action
}

if (-not (Test-IsAdmin)) {
    throw "This script must run in an elevated PowerShell terminal."
}

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Resolve-DefaultExePath
} else {
    $ExePath = (Resolve-Path $ExePath).Path
}

$workDir = Join-Path $env:TEMP "czur-sdk-open-service-smoke"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

$env:SDK_OPEN_WORK_DIR = $workDir
$env:SDK_AUTH_TOKEN = $AuthToken

$healthUrl = "http://127.0.0.1:$AdminPort/healthz"
$statusUrl = "http://127.0.0.1:$AdminPort/api/status"
$script:Installed = $false

try {
    Invoke-Step "Using executable" {
        Write-Host $ExePath
        Write-Host "ServiceName=$ServiceName"
        Write-Host "WorkDir=$workDir"
    }

    Invoke-Step "Remove stale service if present" {
        $existing = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
        if ($existing) {
            if ($existing.Status -ne "Stopped") {
                Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
                $existing.WaitForStatus("Stopped", [TimeSpan]::FromSeconds(20))
            }
            & $ExePath --uninstall-service --service-name $ServiceName
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to uninstall stale service. exit=$LASTEXITCODE"
            }
        } else {
            Write-Host "No stale service."
        }
    }

    Invoke-Step "Install service" {
        $args = @("--install-service", "--service-name", $ServiceName, "--display-name", $DisplayName)
        if (-not [string]::IsNullOrWhiteSpace($ConfigPath)) {
            $args += @("--config", (Resolve-Path $ConfigPath).Path)
        }
        & $ExePath @args
        if ($LASTEXITCODE -ne 0) {
            throw "Install failed. exit=$LASTEXITCODE"
        }
        $script:Installed = $true
    }

    Invoke-Step "Start service" {
        Start-Service -Name $ServiceName
    }

    Invoke-Step "Wait for WEB health" {
        $deadline = (Get-Date).AddSeconds($StartupTimeoutSec)
        $lastError = $null
        do {
            try {
                $health = Invoke-WebRequest -Uri $healthUrl -UseBasicParsing -TimeoutSec 3
                if ($health.StatusCode -eq 200) {
                    Write-Host "healthz=$($health.StatusCode) $($health.Content)"
                    $lastError = $null
                    break
                }
                $lastError = "Unexpected status: $($health.StatusCode)"
            } catch {
                $lastError = $_.Exception.Message
            }
            Start-Sleep -Seconds 1
        } while ((Get-Date) -lt $deadline)

        if ($lastError) {
            throw "WEB health check failed: $lastError"
        }
    }

    Invoke-Step "Check admin status API" {
        $headers = @{ Authorization = "Bearer $AuthToken" }
        $status = Invoke-WebRequest -Uri $statusUrl -Headers $headers -UseBasicParsing -TimeoutSec 5
        Write-Host "status_api=$($status.StatusCode)"
        Write-Host $status.Content.Substring(0, [Math]::Min(500, $status.Content.Length))
    }

    Invoke-Step "Stop service" {
        Stop-Service -Name $ServiceName -Force
        (Get-Service -Name $ServiceName).WaitForStatus("Stopped", [TimeSpan]::FromSeconds(30))
        Write-Host "Stopped."
    }
} finally {
    if ($script:Installed -and -not $KeepService) {
        Write-Host ""
        Write-Host "==> Uninstall service"
        try {
            $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
            if ($svc -and $svc.Status -ne "Stopped") {
                Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
                $svc.WaitForStatus("Stopped", [TimeSpan]::FromSeconds(20))
            }
            & $ExePath --uninstall-service --service-name $ServiceName
            if ($LASTEXITCODE -ne 0) {
                Write-Warning "Uninstall failed. exit=$LASTEXITCODE"
            }
        } catch {
            Write-Warning $_.Exception.Message
        }
    }

    Remove-Item Env:\SDK_OPEN_WORK_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\SDK_AUTH_TOKEN -ErrorAction SilentlyContinue
}
