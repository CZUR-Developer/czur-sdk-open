param(
    [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
    [Parameter(Mandatory = $true)][string]$ArtifactOssUri,
    [Parameter(Mandatory = $true)][string]$OssutilConfigPath,
    [Parameter(Mandatory = $true)][string]$BuildConfig,
    [ValidateSet('x64', 'x86', 'all')][string]$WindowsArch = 'x64',
    [string]$BuildNumber = 'local',
    [string]$VerifyInstallerText = 'true',
    [string]$SymbolOssUri = '',
    [string]$CodeSigningEnabledText = 'false',
    [string]$CodeSigningBrokerUrl = '',
    [string]$CodeSigningProject = '',
    [string]$CodeSigningTokenPath = '',
    [string]$CodeSigningVersion = '',
    [string]$CodeSigningSubmittedBy = 'jenkins',
    [int]$CodeSigningTimeoutSeconds = 1800,
    [int]$CodeSigningPollIntervalSeconds = 5
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Join-Path $WorkspaceRoot 'src'
$ArtifactDir = Join-Path $WorkspaceRoot 'sdk-open-windows-artifacts'
$SymbolDir = Join-Path $WorkspaceRoot 'sdk-open-windows-symbols'
$PythonCommand = $null
$ScriptStartedAt = Get-Date

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Require-File {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Require-Directory {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description not found: $Path"
    }
}

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Format-Duration {
    param([TimeSpan]$Duration)
    return ('{0:00}:{1:00}:{2:00}.{3:000}' -f [Math]::Floor($Duration.TotalHours), $Duration.Minutes, $Duration.Seconds, $Duration.Milliseconds)
}

function Invoke-TimedStep {
    param(
        [string]$Name,
        [scriptblock]$ScriptBlock
    )

    $startedAt = Get-Date
    Write-Host "[TIMING] START: $Name"
    try {
        & $ScriptBlock
        $elapsed = (Get-Date) - $startedAt
        Write-Host "[TIMING] DONE: $Name duration=$(Format-Duration $elapsed) seconds=$([Math]::Round($elapsed.TotalSeconds, 3))"
    }
    catch {
        $elapsed = (Get-Date) - $startedAt
        Write-Host "[TIMING] FAILED: $Name duration=$(Format-Duration $elapsed) seconds=$([Math]::Round($elapsed.TotalSeconds, 3))"
        throw
    }
}

function Invoke-CheckedCommand {
    param(
        [string]$Command,
        [string[]]$Arguments,
        [string]$Description
    )

    Invoke-TimedStep $Description {
        & $Command @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Description failed with exit code $LASTEXITCODE"
        }
    }
}

function Invoke-TimedDetail {
    param(
        [string]$Name,
        [scriptblock]$ScriptBlock
    )

    $startedAt = Get-Date
    Write-Host "[TIMING_DETAIL] START: $Name"
    try {
        & $ScriptBlock
        $elapsed = (Get-Date) - $startedAt
        Write-Host "[TIMING_DETAIL] DONE: $Name duration=$(Format-Duration $elapsed) seconds=$([Math]::Round($elapsed.TotalSeconds, 3))"
    }
    catch {
        $elapsed = (Get-Date) - $startedAt
        Write-Host "[TIMING_DETAIL] FAILED: $Name duration=$(Format-Duration $elapsed) seconds=$([Math]::Round($elapsed.TotalSeconds, 3))"
        throw
    }
}

function ConvertTo-Bool {
    param([string]$Value)
    if (-not $Value) {
        return $false
    }
    return @('1', 'true', 'yes', 'on') -contains $Value.Trim().ToLowerInvariant()
}

function Get-CodeSigningToken {
    if (-not (Test-Path -LiteralPath $CodeSigningTokenPath -PathType Leaf)) {
        throw "Windows signing token file not found: $CodeSigningTokenPath"
    }

    $token = (Get-Content -LiteralPath $CodeSigningTokenPath -Raw).Trim()
    if (-not $token) {
        throw "Windows signing token file is empty: $CodeSigningTokenPath"
    }
    return $token
}

function New-CodeSigningHttpClient {
    param([string]$Token)

    Add-Type -AssemblyName System.Net.Http
    $client = [System.Net.Http.HttpClient]::new()
    $client.Timeout = [TimeSpan]::FromSeconds([Math]::Max(300, $CodeSigningTimeoutSeconds))
    $client.DefaultRequestHeaders.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $Token)
    return $client
}

function Read-HttpContentString {
    param($Response)

    return $Response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
}

function Test-CodeSigningBrokerHealth {
    param([string]$Token)

    $baseUrl = $CodeSigningBrokerUrl.TrimEnd('/')
    $client = New-CodeSigningHttpClient $Token
    try {
        $response = $client.GetAsync("$baseUrl/health").GetAwaiter().GetResult()
        $body = Read-HttpContentString $response
        if (-not $response.IsSuccessStatusCode) {
            throw "Windows signing broker health check failed: status=$([int]$response.StatusCode), body=$body"
        }
        Write-Host "Windows signing broker health check passed: $body"
    }
    finally {
        $client.Dispose()
    }
}

function Test-RetriableCodeSigningStatus {
    param([int]$StatusCode)

    return @(408, 429, 502, 503, 504) -contains $StatusCode
}

function Invoke-CodeSigningSubmit {
    param(
        [string]$Path,
        [string]$UploadFileName,
        [string]$Token,
        [string]$FileName
    )

    $baseUrl = $CodeSigningBrokerUrl.TrimEnd('/')
    $maxAttempts = 3
    for ($attempt = 1; $attempt -le $maxAttempts; $attempt++) {
        $client = New-CodeSigningHttpClient $Token
        $fileStream = $null
        $multipart = $null
        try {
            $multipart = [System.Net.Http.MultipartFormDataContent]::new()
            $multipart.Add([System.Net.Http.StringContent]::new($CodeSigningProject), 'project')
            $multipart.Add([System.Net.Http.StringContent]::new($CodeSigningVersion), 'version')
            $multipart.Add([System.Net.Http.StringContent]::new($CodeSigningSubmittedBy), 'submitted_by')
            $multipart.Add([System.Net.Http.StringContent]::new('false'), 'retain_signed')

            $fileStream = [System.IO.File]::OpenRead($Path)
            $fileContent = [System.Net.Http.StreamContent]::new($fileStream)
            $fileContent.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse('application/octet-stream')
            $multipart.Add($fileContent, 'file', $UploadFileName)

            Write-Host "Submitting code signing job request: file=$FileName, attempt=$attempt/$maxAttempts"
            $response = $client.PostAsync("$baseUrl/api/sign-jobs", $multipart).GetAwaiter().GetResult()
            $body = Read-HttpContentString $response
            if ($response.IsSuccessStatusCode) {
                return $body
            }

            $statusCode = [int]$response.StatusCode
            if (($attempt -lt $maxAttempts) -and (Test-RetriableCodeSigningStatus $statusCode)) {
                Write-Host "Code signing submit got retryable response: file=$FileName, status=$statusCode, body=$body"
                Start-Sleep -Seconds ([Math]::Max(5, $CodeSigningPollIntervalSeconds))
                continue
            }

            throw "Code signing submit failed for ${FileName}: status=$statusCode, body=$body"
        }
        finally {
            if ($multipart) {
                $multipart.Dispose()
            }
            elseif ($fileStream) {
                $fileStream.Dispose()
            }
            $client.Dispose()
        }
    }

    throw "Code signing submit failed for ${FileName}: attempts exhausted"
}

function Invoke-CodeSigningJob {
    param(
        [string]$Path,
        [string]$Stage,
        [string]$Token
    )

    Require-File $Path "Code signing target"

    $baseUrl = $CodeSigningBrokerUrl.TrimEnd('/')
    $fileName = Split-Path -Leaf $Path
    $uploadFileName = "$([Guid]::NewGuid().ToString('N'))$([System.IO.Path]::GetExtension($Path))"
    $signedTempPath = "$Path.signed.tmp"
    if (Test-Path -LiteralPath $signedTempPath) {
        Remove-Item -LiteralPath $signedTempPath -Force
    }

    $unsignedHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "Submitting code signing job: stage=$Stage, file=$fileName, sha256=$unsignedHash"

    $submitBody = Invoke-TimedDetail "Code signing submit: $fileName" {
        Invoke-CodeSigningSubmit -Path $Path -UploadFileName $uploadFileName -Token $Token -FileName $fileName
    }
    $job = $submitBody | ConvertFrom-Json
    $jobId = $job.id
    if (-not $jobId) {
        throw "Code signing submit response did not include job id for ${fileName}: $submitBody"
    }
    Write-Host "Code signing job submitted: file=$fileName, job_id=$jobId"

    Invoke-TimedDetail "Code signing wait: $fileName" {
        $client = New-CodeSigningHttpClient $Token
        $deadline = (Get-Date).AddSeconds($CodeSigningTimeoutSeconds)
        try {
            while ($true) {
                $statusResponse = $client.GetAsync("$baseUrl/api/sign-jobs/$jobId").GetAwaiter().GetResult()
                $statusBody = Read-HttpContentString $statusResponse
                if (-not $statusResponse.IsSuccessStatusCode) {
                    throw "Code signing status request failed for ${fileName}: job_id=$jobId, status=$([int]$statusResponse.StatusCode), body=$statusBody"
                }

                $status = $statusBody | ConvertFrom-Json
                if ($status.status -eq 'success') {
                    Write-Host "Code signing job succeeded: file=$fileName, job_id=$jobId"
                    break
                }
                if ($status.status -eq 'failed') {
                    throw "Code signing job failed for ${fileName}: job_id=$jobId, message=$($status.failure_message)"
                }
                if ((Get-Date) -ge $deadline) {
                    throw "Timed out waiting for code signing job: file=$fileName, job_id=$jobId"
                }

                Write-Host "Code signing job status: file=$fileName, job_id=$jobId, status=$($status.status)"
                Start-Sleep -Seconds $CodeSigningPollIntervalSeconds
            }
        }
        finally {
            $client.Dispose()
        }
    }

    Invoke-TimedDetail "Code signing download: $fileName" {
        $client = New-CodeSigningHttpClient $Token
        $downloadStream = $null
        $outputStream = $null
        try {
            $downloadResponse = $client.GetAsync("$baseUrl/api/sign-jobs/$jobId/download").GetAwaiter().GetResult()
            if (-not $downloadResponse.IsSuccessStatusCode) {
                $downloadBody = Read-HttpContentString $downloadResponse
                throw "Code signing download failed for ${fileName}: job_id=$jobId, status=$([int]$downloadResponse.StatusCode), body=$downloadBody"
            }

            $downloadStream = $downloadResponse.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
            $outputStream = [System.IO.File]::Open($signedTempPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write)
            $downloadStream.CopyTo($outputStream)
        }
        finally {
            if ($outputStream) {
                $outputStream.Dispose()
            }
            if ($downloadStream) {
                $downloadStream.Dispose()
            }
            $client.Dispose()
        }
    }

    Require-File $signedTempPath "Signed artifact"
    Remove-Item -LiteralPath $Path -Force
    Move-Item -LiteralPath $signedTempPath -Destination $Path -Force

    $signedHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "Code signing output written: file=$fileName, signed_sha256=$signedHash"
}

function Invoke-CodeSigning {
    param(
        [string]$Stage,
        [string[]]$Paths
    )

    if (-not $CodeSigningEnabled) {
        Write-Host "Code signing skipped: $Stage"
        return
    }

    if (-not $CodeSigningBrokerUrl -or $CodeSigningBrokerUrl.Trim().Length -eq 0) {
        throw 'Windows signing broker URL is required when code signing is enabled'
    }
    if (-not $CodeSigningProject -or $CodeSigningProject.Trim().Length -eq 0) {
        throw 'Windows signing project is required when code signing is enabled'
    }
    if ($CodeSigningTimeoutSeconds -le 0) {
        throw 'Windows signing timeout must be greater than zero'
    }
    if ($CodeSigningPollIntervalSeconds -le 0) {
        throw 'Windows signing poll interval must be greater than zero'
    }

    $token = Get-CodeSigningToken
    Invoke-TimedDetail "Code signing broker health check: $Stage" {
        Test-CodeSigningBrokerHealth $token
    }
    foreach ($Path in $Paths) {
        $fileName = Split-Path -Leaf $Path
        Invoke-TimedDetail "Code signing file total: $fileName ($Stage)" {
            Invoke-CodeSigningJob -Path $Path -Stage $Stage -Token $token
        }
    }
}

function Assert-AuthenticodeValid {
    param(
        [string]$Path,
        [string]$Description
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid') {
        throw "$Description Authenticode status is $($signature.Status): $Path"
    }
    Write-Host "$Description Authenticode status is Valid: $Path"
}

function Invoke-CodeSigningIfFileExists {
    param(
        [string]$Path,
        [string]$Stage,
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Write-Warning "Optional code signing target not found, skip ${Description}: $Path"
        return
    }

    Invoke-CodeSigning $Stage @($Path)
    if ($CodeSigningEnabled) {
        Assert-AuthenticodeValid -Path $Path -Description $Description
    }
}

function Get-FileMd5Lower {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm MD5).Hash.ToLowerInvariant()
}

function Enable-PnpmBuildScript {
    param(
        [string]$ProjectDir,
        [string]$PackageName
    )

    $packageJsonPath = Join-Path $ProjectDir 'package.json'
    Require-File $packageJsonPath "package.json for $ProjectDir"

    $workspaceManifestPath = Join-Path $ProjectDir 'pnpm-workspace.yaml'
    $workspaceManifest = @"
packages:
  - .
allowBuilds:
  ${PackageName}: true
"@

    Set-Content -LiteralPath $workspaceManifestPath -Value $workspaceManifest -Encoding ASCII
    Write-Host "Enabled pnpm build script for ${PackageName}: $workspaceManifestPath"
}

function Get-OssDirectoryPrefix {
    param([string]$Uri)
    if (-not $Uri -or $Uri.Trim().Length -eq 0) {
        throw 'Artifact OSS URI is required'
    }
    return $Uri.TrimEnd('/')
}

function Resolve-MakeNsis {
    $candidate = Join-Path $RepoRoot 'packaging\windows\NSIS\makensis.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return $candidate
    }
    $command = Get-Command 'makensis.exe' -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "makensis.exe not found. Expected repository NSIS at $candidate or makensis.exe in PATH."
}

function Resolve-PythonCommand {
    foreach ($name in @('python', 'python3')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    $pyCommand = Get-Command 'py.exe' -ErrorAction SilentlyContinue
    if ($pyCommand) {
        return $pyCommand.Source
    }

    throw 'Required command not found: python'
}

function Resolve-SdkOpenRuntimeDir {
    param([Parameter(Mandatory = $true)][string]$BuildDirectory)

    $candidates = @(
        (Join-Path (Join-Path $BuildDirectory 'src\sdk_open') $BuildConfig),
        (Join-Path $BuildDirectory $BuildConfig)
    )
    $requiredFiles = @(
        'sdk_open_app.exe',
        'sdk_private_auth_c_api.dll',
        'sdk_private_providers.dll'
    )

    foreach ($candidate in $candidates) {
        $missingFiles = @()
        foreach ($fileName in $requiredFiles) {
            if (-not (Test-Path -LiteralPath (Join-Path $candidate $fileName) -PathType Leaf)) {
                $missingFiles += $fileName
            }
        }
        if ($missingFiles.Count -eq 0) {
            Write-Host "Resolved sdk-open runtime directory: $candidate"
            return $candidate
        }
        if (Test-Path -LiteralPath (Join-Path $candidate 'sdk_open_app.exe') -PathType Leaf) {
            Write-Host "Skip sdk-open runtime candidate because private DLL(s) are missing: $candidate; missing=$($missingFiles -join ', ')"
        }
    }

    throw "Complete sdk-open runtime not found in candidates: $($candidates -join '; '). Required files: $($requiredFiles -join ', ')"
}

function Get-PeMachine {
    param([Parameter(Mandatory = $true)][string]$Path)

    Require-File $Path 'PE binary'
    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Invalid DOS signature: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Invalid PE signature: $Path"
        }
        return $reader.ReadUInt16()
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Assert-PeArchitecture {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ValidateSet('x64', 'x86')][string]$Architecture,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0x014C }
    $actualMachine = Get-PeMachine -Path $Path
    if ($actualMachine -ne $expectedMachine) {
        throw ('{0} PE architecture mismatch: expected {1} (0x{2:X4}), actual 0x{3:X4}: {4}' -f $Description, $Architecture, $expectedMachine, $actualMachine, $Path)
    }
}

function Assert-RuntimeReady {
    param(
        [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
        [Parameter(Mandatory = $true)][ValidateSet('x64', 'x86')][string]$Architecture
    )

    Require-Directory $RuntimeDirectory 'sdk-open runtime directory'
    $sdkOpenApp = Join-Path $RuntimeDirectory 'sdk_open_app.exe'
    $privateProvider = Join-Path $RuntimeDirectory 'sdk_private_providers.dll'
    Require-File $sdkOpenApp 'sdk_open_app.exe'
    Require-Directory (Join-Path $RuntimeDirectory 'web\admin') 'web/admin runtime assets'
    Require-Directory (Join-Path $RuntimeDirectory 'web\demo') 'web/demo runtime assets'
    Require-Directory (Join-Path $RuntimeDirectory 'configs') 'runtime configs directory'
    Require-Directory (Join-Path $RuntimeDirectory 'twain_helper_runtime') 'dependency-isolated TWAIN helper runtime directory'

    Assert-PeArchitecture -Path $sdkOpenApp -Architecture $Architecture -Description 'sdk_open_app.exe'
    Assert-PeArchitecture -Path $privateProvider -Architecture $Architecture -Description 'sdk_private_providers.dll'

    $x86Helper = Join-Path $RuntimeDirectory 'twain_helper_runtime\twain_helper_x86.exe'
    $x64Helper = Join-Path $RuntimeDirectory 'twain_helper_runtime\twain_helper_x64.exe'
    Require-File $x86Helper 'dependency-isolated x86 TWAIN helper'
    Assert-PeArchitecture -Path $x86Helper -Architecture 'x86' -Description 'x86 TWAIN helper'
    if ($Architecture -eq 'x64') {
        Require-File $x64Helper 'dependency-isolated x64 TWAIN helper'
        Assert-PeArchitecture -Path $x64Helper -Architecture 'x64' -Description 'x64 TWAIN helper'
    }
    elseif (Test-Path -LiteralPath $x64Helper) {
        throw "x86 sdk-open runtime must not contain x64 TWAIN helper: $x64Helper"
    }

    $requiredFiles = @(
        'sdk_private_auth_c_api.dll',
        'sdk_private_providers.dll',
        'docx.dll',
        'zlib.dll',
        'zlib1.dll',
        'zip.dll',
        'czcv_scan.dll',
        'czcv_ocr_api.dll'
    )
    if ($Architecture -eq 'x64') {
        $requiredFiles += @('libcrypto-1_1-x64.dll', 'libssl-1_1-x64.dll', 'libcrypto-3-x64.dll', 'libssl-3-x64.dll')
    }
    else {
        $requiredFiles += @('libcrypto-1_1.dll', 'libssl-1_1.dll', 'libcrypto-3.dll', 'libssl-3.dll')
    }
    foreach ($fileName in $requiredFiles) {
        Require-File (Join-Path $RuntimeDirectory $fileName) "runtime dependency $fileName"
    }

    $requiredDirs = @(
        'models',
        'czcv_model',
        'ofd_fonts'
    )
    foreach ($dirName in $requiredDirs) {
        Require-Directory (Join-Path $RuntimeDirectory $dirName) "runtime dependency directory $dirName"
    }
}

function Assert-SdkOpenServiceSourceReady {
    $sdkOpenDir = Join-Path $RepoRoot 'src\sdk_open'
    $sdkOpenMain = Join-Path $sdkOpenDir 'runtime\sdk_open_main.cpp'
    Require-Directory $sdkOpenDir 'sdk_open submodule directory'
    Require-File $sdkOpenMain 'sdk_open_main.cpp'

    $topLevelHead = (& git -C $RepoRoot rev-parse HEAD | Out-String).Trim()
    $sdkOpenHead = (& git -C $sdkOpenDir rev-parse HEAD | Out-String).Trim()
    Write-Host "Top-level HEAD: $topLevelHead"
    Write-Host "sdk_open submodule HEAD: $sdkOpenHead"

    $requiredSourceText = @(
        '--install-service',
        '--uninstall-service',
        'StartServiceCtrlDispatcher'
    )
    foreach ($text in $requiredSourceText) {
        if (-not (Select-String -LiteralPath $sdkOpenMain -SimpleMatch $text -Quiet)) {
            throw "sdk_open_main.cpp does not contain required Windows Service source text '$text': $sdkOpenMain"
        }
    }
    Write-Host 'sdk_open_main.cpp Windows Service source check passed.'
}

function Read-SdkOpenVersionInfo {
    $versionFile = Join-Path $RepoRoot 'src\sdk_open\version\version.ini'
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
        Stage = $values['stage']
    }
}

function New-SdkOpenReleaseNote {
    param([string]$OutputPath)

    $version = Read-SdkOpenVersionInfo
    $generator = Join-Path $RepoRoot 'src\sdk_open\ci\scripts\generate_sdk_open_release_note.py'
    $englishChangelog = Join-Path $RepoRoot 'src\sdk_open\doc\CHANGELOG.md'
    $chineseChangelog = Join-Path $RepoRoot 'src\sdk_open\doc\CHANGELOG_ZH.md'

    Require-File $generator 'SDK Open release note generator'
    Require-File $englishChangelog 'SDK Open English changelog'
    Require-File $chineseChangelog 'SDK Open Chinese changelog'

    $pythonArguments = @()
    if ([System.IO.Path]::GetFileName($PythonCommand).Equals('py.exe', [System.StringComparison]::OrdinalIgnoreCase)) {
        $pythonArguments += '-3'
    }
    $pythonArguments += @(
        $generator,
        '--version',
        $version.Version,
        '--build',
        $version.Build,
        '--stage',
        $version.Stage,
        '--english-changelog',
        $englishChangelog,
        '--chinese-changelog',
        $chineseChangelog,
        '--output',
        $OutputPath
    )
    Invoke-CheckedCommand $PythonCommand $pythonArguments 'Generate sdk-open release note'

    Require-File $OutputPath 'generated sdk-open release note'
    Write-Host "Generated sdk-open Windows release note: $OutputPath"
    Get-Content -LiteralPath $OutputPath | ForEach-Object { Write-Host $_ }
}

function New-Sha256Sums {
    param([string]$Directory)

    $checksumPath = Join-Path $Directory 'SHA256SUMS.txt'
    if (Test-Path -LiteralPath $checksumPath) {
        Remove-Item -LiteralPath $checksumPath -Force
    }

    Get-ChildItem -LiteralPath $Directory -File |
        Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
        Sort-Object Name |
        ForEach-Object {
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $($_.Name)"
        } |
        Set-Content -LiteralPath $checksumPath -Encoding ASCII

    return $checksumPath
}

function Upload-Artifacts {
    param([string]$Directory)

    $targetPrefix = Get-OssDirectoryPrefix $ArtifactOssUri
    Require-File $OssutilConfigPath 'ossutil config'

    Get-ChildItem -LiteralPath $Directory -File |
        Sort-Object Name |
        ForEach-Object {
            $targetUri = "$targetPrefix/$($_.Name)"
            Write-Host "Uploading sdk-open Windows artifact to OSS: $($_.FullName) -> $targetUri"
            & ossutil cp $_.FullName $targetUri -f -c $OssutilConfigPath
            if ($LASTEXITCODE -ne 0) {
                throw "OSS upload failed for $($_.FullName) with exit code $LASTEXITCODE"
            }
        }
}

function Upload-Symbols {
    param([string]$Directory)

    if (-not $SymbolOssUri -or $SymbolOssUri.Trim().Length -eq 0) {
        Write-Host 'SymbolOssUri is empty, skip sdk-open Windows symbol upload.'
        return
    }

    $targetPrefix = Get-OssDirectoryPrefix $SymbolOssUri
    Require-File $OssutilConfigPath 'ossutil config'

    Get-ChildItem -LiteralPath $Directory -File |
        Sort-Object Name |
        ForEach-Object {
            $targetUri = "$targetPrefix/$($_.Name)"
            Write-Host "Uploading sdk-open Windows symbol to OSS: $($_.FullName) -> $targetUri"
            & ossutil cp $_.FullName $targetUri -f -c $OssutilConfigPath
            if ($LASTEXITCODE -ne 0) {
                throw "OSS symbol upload failed for $($_.FullName) with exit code $LASTEXITCODE"
            }
        }
}

$CodeSigningEnabled = ConvertTo-Bool $CodeSigningEnabledText

Write-Host "Remote sdk-open Windows build started at: $($ScriptStartedAt.ToString('o'))"
Write-Host "WorkspaceRoot=$WorkspaceRoot"
Write-Host "RepoRoot=$RepoRoot"
Write-Host "BuildConfig=$BuildConfig"
Write-Host "WindowsArch=$WindowsArch"
Write-Host "ArtifactOssUri=$ArtifactOssUri"
Write-Host "SymbolOssUri=$SymbolOssUri"
Write-Host "VerifyInstallerText=$VerifyInstallerText"
Write-Host "Windows code signing enabled: $CodeSigningEnabled"
if ($CodeSigningEnabled) {
    Write-Host "Windows signing broker URL: $CodeSigningBrokerUrl"
    Write-Host "Windows signing project: $CodeSigningProject"
    Write-Host "Windows signing token file exists: $(Test-Path -LiteralPath $CodeSigningTokenPath)"
}

Require-Command 'git'
Require-Command 'cmake'
Require-Command 'powershell'
Require-Command 'node'
Require-Command 'corepack.cmd'
Require-Command 'ossutil'
$MakeNsis = Resolve-MakeNsis
$PythonCommand = Resolve-PythonCommand
Write-Host "makensis.exe=$MakeNsis"
Write-Host "python=$PythonCommand"

Require-Directory $RepoRoot 'repository root'
Require-File (Join-Path $RepoRoot 'CMakeLists.txt') 'root CMakeLists.txt'
Require-File (Join-Path $RepoRoot 'src\sdk_open\packaging\windows\build-sdk-open-nsis.ps1') 'sdk-open NSIS packager'
Require-File (Join-Path $RepoRoot 'src\sdk_open\packaging\windows\verify-sdk-open-install.ps1') 'sdk-open installer verifier'
Require-File $OssutilConfigPath 'ossutil config'

. (Join-Path $PSScriptRoot 'sdk_open_pnpm.ps1')
$SdkOpenPnpmCommand = Invoke-TimedStep 'Prepare and verify sdk-open pnpm' {
    Initialize-SdkOpenPnpm -WorkspaceRoot $WorkspaceRoot -ProjectDirs @(
        (Join-Path $RepoRoot 'src/sdk_open/frontend/admin-site'),
        (Join-Path $RepoRoot 'src/sdk_open/frontend/demo-site')
    )
}

if (Test-Path -LiteralPath $ArtifactDir) {
    Remove-Item -LiteralPath $ArtifactDir -Recurse -Force
}
if (Test-Path -LiteralPath $SymbolDir) {
    Remove-Item -LiteralPath $SymbolDir -Recurse -Force
}
Ensure-Directory $ArtifactDir
Ensure-Directory $SymbolDir

Enable-PnpmBuildScript -ProjectDir (Join-Path $RepoRoot 'src\sdk_open\frontend\admin-site') -PackageName 'esbuild'
Enable-PnpmBuildScript -ProjectDir (Join-Path $RepoRoot 'src\sdk_open\frontend\demo-site') -PackageName 'esbuild'

function Build-SdkOpenWindowsArch {
    param([Parameter(Mandatory = $true)][ValidateSet('x64', 'x86')][string]$Architecture)

    # 两种架构使用完全独立的构建、NSIS 临时文件和符号目录，避免 all 模式互相覆盖 (x64/x86).
    $cmakePlatform = if ($Architecture -eq 'x64') { 'x64' } else { 'Win32' }
    $buildDirectoryName = "cmake-build-open_sdk_windows-$Architecture"
    $buildDirectory = Join-Path $RepoRoot $buildDirectoryName
    $architectureWorkDir = Join-Path $ArtifactDir "work-$Architecture"
    $packagerOutput = Join-Path $architectureWorkDir 'nsis-output'
    $architectureSymbolDir = Join-Path $SymbolDir $Architecture

    Ensure-Directory $architectureWorkDir
    Ensure-Directory $architectureSymbolDir
    Write-Host "Building sdk-open Windows architecture: $Architecture (CMake platform: $cmakePlatform)"

    Invoke-CheckedCommand 'cmake' @(
        '-S',
        '.',
        '-B',
        $buildDirectoryName,
        '-G',
        'Visual Studio 17 2022',
        '-A',
        $cmakePlatform,
        '-DBUILD_SDK_OPEN=ON',
        '-DBUILD_SDK_WEB=ON',
        '-DSDK_OPEN_ENABLE_HTTP_SERVER=ON',
        '-DBUILD_SDK_PRIVATE=ON',
        '-DSDK_OPEN_USE_PRIVATE_PROVIDER=ON',
        "-DPNPM_CMD_EXECUTABLE:FILEPATH=$SdkOpenPnpmCommand",
        "-DPNPM_EXECUTABLE:FILEPATH=$SdkOpenPnpmCommand"
    ) "Configure sdk-open Windows $Architecture CMake project"

    Invoke-CheckedCommand 'cmake' @(
        '--build',
        $buildDirectoryName,
        '--config',
        $BuildConfig,
        '--target',
        'sdk_open_app',
        '--',
        '/m:2'
    ) "Build sdk_open_app ($Architecture)"

    $runtimeDirectory = Resolve-SdkOpenRuntimeDir -BuildDirectory $buildDirectory
    Assert-RuntimeReady -RuntimeDirectory $runtimeDirectory -Architecture $Architecture
    $sdkOpenPdbPath = Join-Path $runtimeDirectory 'sdk_open_app.pdb'
    if ($BuildConfig -eq 'Release') {
        Require-File $sdkOpenPdbPath "sdk_open_app.pdb for $Architecture Release symbol backup"
    }

    $sdkOpenAppPath = Join-Path $runtimeDirectory 'sdk_open_app.exe'
    Invoke-TimedStep "Code signing sdk-open $Architecture runtime executable before installer packaging" {
        Invoke-CodeSigning "sdk-open $Architecture runtime executable before installer packaging" @($sdkOpenAppPath)
        if ($CodeSigningEnabled) {
            Assert-AuthenticodeValid -Path $sdkOpenAppPath -Description "Signed sdk-open $Architecture runtime executable"
        }
    }

    Invoke-TimedStep "Code signing sdk-open $Architecture runtime dependencies before installer packaging" {
        # Runtime dependencies vary between x86/x64 and dependency bundle versions.
        $runtimeSigningTargets = @(
            'cairo.dll',
            'czcv_ocr_api.dll',
            'czcv_scan.dll',
            'docx.dll',
            'fontconfig.dll',
            'FreeImage.dll',
            'jpeg8.dll',
            'libcrypto-1_1.dll',
            'libcrypto-1_1-x64.dll',
            'libcrypto-3.dll',
            'libcrypto-3-x64.dll',
            'libexif.dll',
            'libiconv.dll',
            'liblzma.dll',
            'libopenjp2-7.dll',
            'libpng16.dll',
            'libssl-1_1.dll',
            'libssl-1_1-x64.dll',
            'libssl-3.dll',
            'libssl-3-x64.dll',
            'libxml2.dll',
            'models\czcv_ocr.dll',
            'opencv_world450.dll',
            'sdk_private_auth_c_api.dll',
            'sdk_private_providers.dll',
            'tiff.dll',
            'turbojpeg.dll',
            'zip.dll',
            'zlib.dll',
            'zlib1.dll',
            'zstd.dll'
        )
        foreach ($relativePath in $runtimeSigningTargets) {
            Invoke-CodeSigningIfFileExists `
                -Path (Join-Path $runtimeDirectory $relativePath) `
                -Stage "sdk-open $Architecture runtime dependency before installer packaging: $relativePath" `
                -Description "Signed sdk-open $Architecture runtime dependency $relativePath"
        }
    }

    Invoke-TimedStep "Code signing sdk-open $Architecture TWAIN helpers before installer packaging" {
        $helperPaths = @(
            (Join-Path $runtimeDirectory 'twain_helper_runtime\twain_helper_x86.exe'),
            (Join-Path $runtimeDirectory 'twain_helper_runtime\twain_helper_x64.exe')
        )
        foreach ($helperPath in $helperPaths) {
            Invoke-CodeSigningIfFileExists `
                -Path $helperPath `
                -Stage "sdk-open $Architecture TWAIN helper before installer packaging: $helperPath" `
                -Description "Signed sdk-open $Architecture TWAIN helper $helperPath"
        }
    }

    $packager = Join-Path $RepoRoot 'src\sdk_open\packaging\windows\build-sdk-open-nsis.ps1'
    $packagerArgs = @(
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-File',
        $packager,
        '-RuntimeDir',
        $runtimeDirectory,
        '-OutputDir',
        $packagerOutput,
        '-BuildType',
        $BuildConfig,
        '-Architecture',
        $Architecture,
        '-MakeNsis',
        $MakeNsis
    )
    if ($CodeSigningEnabled) {
        $signedUninstallerPath = Join-Path $architectureWorkDir 'signed-uninstaller\uninstall.exe'
        $packagerArgs += @('-GenerateUninstallerOnly', '-SignedUninstallerPath', $signedUninstallerPath)
        Invoke-CheckedCommand 'powershell' $packagerArgs "Build temporary sdk-open $Architecture NSIS uninstaller payload"

        Invoke-TimedStep "Code signing sdk-open $Architecture NSIS uninstaller payload" {
            Invoke-CodeSigning "sdk-open $Architecture NSIS uninstaller payload" @($signedUninstallerPath)
            Assert-AuthenticodeValid -Path $signedUninstallerPath -Description "Signed sdk-open $Architecture uninstall.exe payload"
        }

        $packagerArgs = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $packager,
            '-RuntimeDir', $runtimeDirectory,
            '-OutputDir', $packagerOutput,
            '-BuildType', $BuildConfig,
            '-Architecture', $Architecture,
            '-MakeNsis', $MakeNsis,
            '-SignedUninstallerPath', $signedUninstallerPath
        )
    }
    $packagerArgs += @('-PdbOutputDir', $architectureSymbolDir)
    Invoke-CheckedCommand 'powershell' $packagerArgs "Build sdk-open $Architecture NSIS installer"

    if ($BuildConfig -eq 'Release') {
        $copiedSdkOpenPdbPath = Join-Path $architectureSymbolDir 'sdk_open_app.pdb'
        Require-File $copiedSdkOpenPdbPath "copied sdk_open_app.pdb for $Architecture Release symbol backup"
        $binaryMd5 = Get-FileMd5Lower $sdkOpenAppPath
        $symbolTimestamp = Get-Date -Format 'yyyyMMdd_HHmm'
        $renamedSdkOpenPdbPath = Join-Path $architectureSymbolDir "sdk_open_app_${Architecture}_${binaryMd5}_${symbolTimestamp}.pdb"
        if (Test-Path -LiteralPath $renamedSdkOpenPdbPath) {
            Remove-Item -LiteralPath $renamedSdkOpenPdbPath -Force
        }
        Rename-Item -LiteralPath $copiedSdkOpenPdbPath -NewName ([System.IO.Path]::GetFileName($renamedSdkOpenPdbPath))
        Get-ChildItem -LiteralPath $architectureSymbolDir -Recurse -File -Filter '*.pdb' |
            Where-Object { $_.FullName -ne $renamedSdkOpenPdbPath } |
            Remove-Item -Force
        Upload-Symbols -Directory $architectureSymbolDir
    }

    $installerCandidates = @(Get-ChildItem -LiteralPath $packagerOutput -File -Filter "sdk-open_Windows_${Architecture}_*.exe")
    if ($installerCandidates.Count -ne 1) {
        throw "Expected exactly one sdk-open_Windows_${Architecture}_*.exe, found $($installerCandidates.Count) in $packagerOutput"
    }
    $artifactInstallerPath = Join-Path $ArtifactDir $installerCandidates[0].Name
    Copy-Item -LiteralPath $installerCandidates[0].FullName -Destination $artifactInstallerPath -Force
    Write-Host "sdk-open Windows $Architecture installer staged: $artifactInstallerPath"

    Invoke-TimedStep "Code signing sdk-open $Architecture final NSIS installer before artifact checksum" {
        Invoke-CodeSigning "sdk-open $Architecture final NSIS installer before artifact checksum" @($artifactInstallerPath)
        if ($CodeSigningEnabled) {
            Assert-AuthenticodeValid -Path $artifactInstallerPath -Description "Signed sdk-open $Architecture final installer"
        }
    }

    if (ConvertTo-Bool $VerifyInstallerText) {
        $tlsRegression = Join-Path $RepoRoot 'src\sdk_open\packaging\windows\test-runtime-tls.ps1'
        Invoke-CheckedCommand 'powershell' @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $tlsRegression,
            '-RuntimeExecutable', $sdkOpenAppPath
        ) "Verify sdk-open Windows $Architecture TLS migration in temporary directories"
        $verifier = Join-Path $RepoRoot 'src\sdk_open\packaging\windows\verify-sdk-open-install.ps1'
        Invoke-CheckedCommand 'powershell' @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $verifier,
            '-Installer', $artifactInstallerPath,
            '-Architecture', $Architecture,
            '-RequireSignedText', $CodeSigningEnabled.ToString()
        ) "Verify sdk-open Windows $Architecture installer"
    }
    else {
        Write-Host "Skipping sdk-open Windows $Architecture installer verification because VerifyInstallerText is false."
    }

    # 只删除本架构打包中间目录，最终安装包保留在公共 artifact 根目录 (artifact).
    Remove-Item -LiteralPath $architectureWorkDir -Recurse -Force
}

Push-Location $RepoRoot
try {
    Assert-SdkOpenServiceSourceReady

    # 发布说明依赖版本号与中英文 changelog 一致，先校验，避免完成耗时编译和签名后才失败 (preflight).
    New-SdkOpenReleaseNote -OutputPath (Join-Path $ArtifactDir 'release_note.md')

    $architectures = if ($WindowsArch -eq 'all') { @('x86', 'x64') } else { @($WindowsArch) }
    foreach ($architecture in $architectures) {
        Build-SdkOpenWindowsArch -Architecture $architecture
    }

    New-Sha256Sums -Directory $ArtifactDir | Out-Null
    Get-Content -LiteralPath (Join-Path $ArtifactDir 'SHA256SUMS.txt') | ForEach-Object { Write-Host $_ }

    Upload-Artifacts -Directory $ArtifactDir

    $elapsed = (Get-Date) - $ScriptStartedAt
    Write-Host "Remote sdk-open Windows build completed. duration=$(Format-Duration $elapsed) seconds=$([Math]::Round($elapsed.TotalSeconds, 3))"
}
finally {
    Pop-Location
}
