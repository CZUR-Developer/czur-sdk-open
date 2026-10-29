param(
    [string]$RuntimeDir = '',
    [string]$OutputDir = '',
    [string]$BuildType = 'Release',
    [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
    [string]$MakeNsis = '',
    [string]$PdbOutputDir = '',
    [string]$SignedUninstallerPath = '',
    [switch]$GenerateUninstallerOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRoot = Split-Path -Parent (Split-Path -Parent $ScriptRoot)
$ProjectRoot = if ($env:SDK_OPEN_HOST_SOURCE_ROOT) { $env:SDK_OPEN_HOST_SOURCE_ROOT } else {
    Split-Path -Parent (Split-Path -Parent $SdkRoot)
}
$InstallHelperScript = Join-Path $ScriptRoot 'sdk-open-install-helper.ps1'
$VcRedistFileNames = @{
    x64 = 'VC_redist.x64 14.44.35211.exe'
    x86 = 'VC_redist.x86 14.44.35211.exe'
}
$VcRedistFileName = $VcRedistFileNames[$Architecture]
$VcRedistPath = Join-Path (Join-Path $ProjectRoot 'third_party_repo') $VcRedistFileName

if (-not $RuntimeDir -or $RuntimeDir.Trim().Length -eq 0) {
    $RuntimeDir = Join-Path (Join-Path $ProjectRoot 'output') $BuildType
}
if (-not $OutputDir -or $OutputDir.Trim().Length -eq 0) {
    $OutputDir = Join-Path $ScriptRoot 'output'
}
if (-not $MakeNsis -or $MakeNsis.Trim().Length -eq 0) {
    $MakeNsis = Join-Path $ProjectRoot 'packaging\windows\NSIS\makensis.exe'
}

function Require-File {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Require-Directory {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description not found: $Path"
    }
}

function Remove-SdkOpenBuildArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$Directory
    )

    # The CMake build output intentionally places archives beside the runtime
    # binaries. They are useful to the build, but must never enter the runtime
    # installer or be mistaken for deployable SDK dependencies.
    $forbiddenExtensions = @(
        '.lib',
        '.exp',
        '.pdb',
        '.ilk',
        '.obj',
        '.iobj',
        '.ipdb',
        '.map'
    )

    $buildArtifacts = @(
        Get-ChildItem -LiteralPath $Directory -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $forbiddenExtensions -contains $_.Extension.ToLowerInvariant() }
    )
    foreach ($buildArtifact in $buildArtifacts) {
        Write-Host "[sdk-open] Remove build artifact from runtime staging: $($buildArtifact.FullName)"
        Remove-Item -LiteralPath $buildArtifact.FullName -Force
    }

    $remainingArtifacts = @(
        Get-ChildItem -LiteralPath $Directory -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $forbiddenExtensions -contains $_.Extension.ToLowerInvariant() }
    )
    if ($remainingArtifacts.Count -gt 0) {
        throw "Build artifacts must not be included in SDK Open NSIS staging: $($remainingArtifacts.FullName -join '; ')"
    }
}

function Assert-BinaryContainsAscii {
    param(
        [string]$Path,
        [string[]]$RequiredText
    )

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $content = [System.Text.Encoding]::ASCII.GetString($bytes)
    $missingText = @()
    foreach ($text in $RequiredText) {
        if (-not $content.Contains($text)) {
            $missingText += $text
        }
    }

    if ($missingText.Count -gt 0) {
        Write-Warning "sdk_open_app.exe binary text scan did not find service argument(s): $($missingText -join ', '). Installer verification will validate service install/start behavior: $Path"
    }
    else {
        Write-Host "[sdk-open] sdk_open_app.exe service argument text scan passed."
    }
}

function Read-VersionInfo {
    $versionFile = Join-Path $SdkRoot 'version\version.ini'
    Require-File $versionFile 'src/sdk_open/version/version.ini'

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $versionFile) {
        if ($line -match '^\s*([^#;][^=]*)=(.*)$') {
            $values[$matches[1].Trim()] = $matches[2].Trim()
        }
    }

    foreach ($key in @('version', 'build', 'stage')) {
        if (-not $values.ContainsKey($key) -or $values[$key].Length -eq 0) {
            throw "src/sdk_open/version/version.ini must contain $key="
        }
    }

    return @{
        Version = $values['version']
        Build = $values['build']
        Stage = ($values['stage'] -replace '[^A-Za-z0-9._-]', '_')
    }
}

$RuntimeDir = (Resolve-Path -LiteralPath $RuntimeDir).Path
Require-File (Join-Path $RuntimeDir 'sdk_open_app.exe') 'sdk_open_app.exe'
Require-File (Join-Path $RuntimeDir 'sdk_private_auth_c_api.dll') 'sdk_private_auth_c_api.dll'
Require-File (Join-Path $RuntimeDir 'sdk_private_providers.dll') 'sdk_private_providers.dll'
Require-File $InstallHelperScript 'sdk-open installer helper script'
Require-File $VcRedistPath "Microsoft VC++ Redistributable $Architecture"
Require-File $MakeNsis 'makensis.exe'
Assert-BinaryContainsAscii -Path (Join-Path $RuntimeDir 'sdk_open_app.exe') -RequiredText @(
    '--install-service',
    '--uninstall-service',
    '--service-name',
    '--service'
)

if (-not (Test-Path -LiteralPath $OutputDir -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

if ($PdbOutputDir -and $PdbOutputDir.Trim().Length -gt 0) {
    Ensure-Directory $PdbOutputDir
    $PdbOutputDir = (Resolve-Path -LiteralPath $PdbOutputDir).Path
    Get-ChildItem -LiteralPath $RuntimeDir -Recurse -File -Filter '*.pdb' -ErrorAction SilentlyContinue |
        ForEach-Object {
            $relativePath = $_.FullName.Substring($RuntimeDir.Length).TrimStart('\', '/')
            $targetPath = Join-Path $PdbOutputDir $relativePath
            $targetDir = Split-Path -Parent $targetPath
            Ensure-Directory $targetDir
            Copy-Item -LiteralPath $_.FullName -Destination $targetPath -Force
            Write-Host "[sdk-open] PDB copied for symbol backup: $($_.FullName) -> $targetPath"
        }
}

$stagingDir = Join-Path $OutputDir 'staging'
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

Write-Host "[sdk-open] Stage runtime from $RuntimeDir"
Get-ChildItem -LiteralPath $RuntimeDir -Force |
    # 根目录 helper 是 CMake 目标的构建产物；SDK Open 运行时只从隔离目录启动，避免安装包重复携带两组文件 (SDK Open).
    Where-Object { $_.Name -notin @('twain_helper_x86.exe', 'twain_helper_x64.exe') } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $stagingDir $_.Name) -Recurse -Force
    }
Remove-SdkOpenBuildArtifacts -Directory $stagingDir

# SDK Open 安装包只交付隔离目录中的 helper，避免根目录副本造成两组文件和签名维护歧义 (SDK Open).
foreach ($helperName in @('twain_helper_x86.exe', 'twain_helper_x64.exe')) {
    if (Test-Path -LiteralPath (Join-Path $stagingDir $helperName)) {
        throw "Root TWAIN helper must not be included in NSIS staging: $helperName"
    }
}
$stagingHelperDir = Join-Path $stagingDir 'twain_helper_runtime'
Require-File (Join-Path $stagingHelperDir 'twain_helper_x86.exe') 'staged x86 TWAIN helper'
if ($Architecture -eq 'x64') {
    Require-File (Join-Path $stagingHelperDir 'twain_helper_x64.exe') 'staged x64 TWAIN helper'
}
elseif (Test-Path -LiteralPath (Join-Path $stagingHelperDir 'twain_helper_x64.exe')) {
    throw 'x86 NSIS staging must not contain x64 TWAIN helper'
}

$stagingX86DsmDir = Join-Path $stagingHelperDir 'twain\x86'
Require-File (Join-Path $stagingX86DsmDir 'TWAINDSM.dll') 'staged x86 TWAIN DSM'
Require-File (Join-Path $stagingX86DsmDir 'TWAINDSM-x86-LICENSE.txt') 'staged x86 TWAIN DSM license'
Require-File (Join-Path $stagingX86DsmDir 'TWAINDSM-x86-VERSION-SHA256.txt') 'staged x86 TWAIN DSM manifest'
if ($Architecture -eq 'x64') {
    Require-File (Join-Path $stagingDir 'TWAINDSM.dll') 'staged x64 TWAIN DSM'
    Require-File (Join-Path $stagingDir 'TWAINDSM-x64-LICENSE.txt') 'staged x64 TWAIN DSM license'
    Require-File (Join-Path $stagingDir 'TWAINDSM-x64-VERSION-SHA256.txt') 'staged x64 TWAIN DSM manifest'
    Require-File (Join-Path $stagingHelperDir 'TWAINDSM.dll') 'staged isolated x64 TWAIN DSM'
}
elseif ((Test-Path -LiteralPath (Join-Path $stagingDir 'TWAINDSM.dll')) -or
        (Test-Path -LiteralPath (Join-Path $stagingHelperDir 'TWAINDSM.dll'))) {
    throw 'x86 NSIS staging must not contain an x64 TWAIN DSM'
}

$version = Read-VersionInfo
$installerName = "sdk-open_Windows_${Architecture}_v$($version.Version).$($version.Build).$($version.Stage).exe"
$installerPath = Join-Path $OutputDir $installerName
if (Test-Path -LiteralPath $installerPath) {
    Remove-Item -LiteralPath $installerPath -Force
}

$nsiPath = Join-Path $ScriptRoot 'sdk-open-minimal.nsi'
Require-File $nsiPath 'sdk-open-minimal.nsi'

Write-Host "[sdk-open] Build NSIS installer: $installerPath"
$makensisArgs = @(
    "/DRUNTIME_DIR=$stagingDir",
    "/DOUTPUT_FILE=$installerPath",
    "/DPRODUCT_VERSION=$($version.Version).$($version.Build)",
    "/DSDK_ARCH=$Architecture",
    "/DINSTALL_HELPER_SCRIPT=$InstallHelperScript",
    "/DVC_REDIST_PATH=$VcRedistPath",
    "/DVC_REDIST_FILE_NAME=$VcRedistFileName",
    $nsiPath
)

if ($GenerateUninstallerOnly) {
    $makensisArgs = @('/DGENERATE_UNINSTALLER_ONLY') + $makensisArgs
}
elseif ($SignedUninstallerPath -and $SignedUninstallerPath.Trim().Length -gt 0) {
    Require-File $SignedUninstallerPath 'signed sdk-open uninstall.exe payload'
    $makensisArgs = @("/DSIGNED_UNINSTALLER_PATH=$SignedUninstallerPath") + $makensisArgs
}

& $MakeNsis @makensisArgs
if ($LASTEXITCODE -ne 0) {
    throw "makensis failed with exit code $LASTEXITCODE"
}
Require-File $installerPath 'generated sdk-open NSIS installer'

Write-Host "[sdk-open] Installer written to $installerPath"
if ($GenerateUninstallerOnly) {
    if (-not $SignedUninstallerPath -or $SignedUninstallerPath.Trim().Length -eq 0) {
        throw '-SignedUninstallerPath is required when -GenerateUninstallerOnly is used.'
    }

    $tempInstallDir = Join-Path $OutputDir 'uninstaller-work\install'
    if (Test-Path -LiteralPath $tempInstallDir) {
        Remove-Item -LiteralPath $tempInstallDir -Recurse -Force
    }
    Ensure-Directory $tempInstallDir

    Write-Host "[sdk-open] Run temporary installer to generate uninstall.exe: $tempInstallDir"
    $process = Start-Process -FilePath $installerPath -ArgumentList @('/S', "/D=$tempInstallDir") -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Temporary sdk-open installer failed with exit code $($process.ExitCode)"
    }

    $generatedUninstaller = Join-Path $tempInstallDir 'uninstall.exe'
    Require-File $generatedUninstaller 'temporary generated sdk-open uninstall.exe'
    $signedUninstallerDir = Split-Path -Parent $SignedUninstallerPath
    Ensure-Directory $signedUninstallerDir
    Copy-Item -LiteralPath $generatedUninstaller -Destination $SignedUninstallerPath -Force
    Require-File $SignedUninstallerPath 'sdk-open uninstall.exe payload'
    Write-Host "[sdk-open] Generated uninstall.exe payload: $SignedUninstallerPath"
}
Write-Output $installerPath
