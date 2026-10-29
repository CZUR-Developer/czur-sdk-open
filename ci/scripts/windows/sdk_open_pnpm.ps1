# Dot-source from remote_sdk_open_build.ps1. All tools run as the build account,
# including SYSTEM when invoked through Cloud Assistant.
function Invoke-SdkOpenPnpmTool {
    param([string]$Command, [string[]]$Arguments)

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "SDK Open pnpm tool failed: $Command (exit code $LASTEXITCODE)"
    }
}

function Initialize-SdkOpenPnpm {
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string[]]$ProjectDirs
    )

    # Read the committed descriptor, including its integrity hash; do not silently
    # fall back to whichever pnpm version happens to be installed in the image.
    $packageManager = $null
    $expectedVersion = $null
    foreach ($projectDir in $ProjectDirs) {
        $manifestPath = Join-Path $projectDir 'package.json'
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $property = $manifest.PSObject.Properties['packageManager']
        if (-not $property -or $property.Value -notmatch '^pnpm@(\d+\.\d+\.\d+)(\+sha\d+\.[a-fA-F0-9]+)?$') {
            throw "SDK Open requires an exact pnpm packageManager in $manifestPath"
        }
        $version = $Matches[1]
        if ($packageManager -and $packageManager -ne $property.Value) {
            throw "SDK Open frontend packageManager declarations must match: $manifestPath"
        }
        $packageManager = [string]$property.Value
        $expectedVersion = $version
    }

    # Keep the cache outside the repository and independent of user-profile caches.
    # Corepack's registry and pnpm's dependency registry are separate settings.
    if (-not $env:COREPACK_HOME) {
        $env:COREPACK_HOME = Join-Path $WorkspaceRoot 'cache/corepack'
    }
    if (-not $env:COREPACK_NPM_REGISTRY) {
        $env:COREPACK_NPM_REGISTRY = 'https://registry.npmmirror.com'
    }
    if (-not $env:npm_config_registry) {
        $env:npm_config_registry = $env:COREPACK_NPM_REGISTRY
    }
    $env:COREPACK_ENABLE_DOWNLOAD_PROMPT = '0'
    $env:COREPACK_DEFAULT_TO_LATEST = '0'
    $env:COREPACK_ENABLE_AUTO_PIN = '0'

    $binDir = Join-Path $WorkspaceRoot 'tools/pnpm'
    New-Item -ItemType Directory -Path $env:COREPACK_HOME -Force | Out-Null
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    # Install only workspace-local shims, leaving Program Files and global defaults alone.
    Invoke-SdkOpenPnpmTool 'corepack.cmd' @('enable', '--install-directory', $binDir, 'pnpm') | Out-Host
    $pnpmCommand = Join-Path $binDir 'pnpm.cmd'
    if (-not (Test-Path -LiteralPath $pnpmCommand -PathType Leaf)) {
        throw "Corepack did not create $pnpmCommand"
    }
    $env:PATH = $binDir + [IO.Path]::PathSeparator + $env:PATH

    Write-Host "[sdk-open pnpm] Preparing pnpm $expectedVersion; cache=$env:COREPACK_HOME"
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-SdkOpenPnpmTool 'corepack.cmd' @('prepare', $packageManager) | Out-Host
            break
        }
        catch {
            if ($attempt -eq 3) {
                throw "SDK Open pnpm preparation failed after 3 attempts. Check COREPACK_NPM_REGISTRY and connectivity under the build account. $($_.Exception.Message)"
            }
            Write-Warning "SDK Open pnpm preparation attempt $attempt failed; retrying in 5 seconds."
            Start-Sleep -Seconds 5
        }
    }

    # Prove both projects can use the cached manager without another Corepack
    # download. This flag does not disable pnpm's dependency downloads.
    $env:COREPACK_ENABLE_NETWORK = '0'
    foreach ($projectDir in $ProjectDirs) {
        Push-Location -LiteralPath $projectDir
        try {
            $actualVersion = ((Invoke-SdkOpenPnpmTool $pnpmCommand @('--version')) -join "`n").Trim()
            if ($actualVersion -ne $expectedVersion) {
                throw "SDK Open pnpm version mismatch in ${projectDir}: expected $expectedVersion, got $actualVersion"
            }
            Write-Host "[sdk-open pnpm] Verified pnpm $actualVersion in $projectDir (Corepack offline)"
        }
        finally {
            Pop-Location
        }
    }
    return $pnpmCommand
}
