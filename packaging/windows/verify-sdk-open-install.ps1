param(
    [string]$Installer = '',
    [string]$InstallDir = '',
    [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
    [string]$ServiceName = 'sdk-open',
    [int]$DemoPort = 17081,
    [int]$AssetHttpsPort = 18082,
    [ValidateRange(1, 3600)][int]$InstallerTimeoutSeconds = 600,
    [string]$RequireSignedText = 'false',
    [switch]$KeepInstalled
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $ScriptRoot))

if (-not $InstallDir -or $InstallDir.Trim().Length -eq 0) {
    $programFiles = if ($Architecture -eq 'x64') {
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)
    }
    else {
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    }
    $InstallDir = Join-Path $programFiles 'CZUR\sdk-open'
}

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'sdk-open install verification must run with administrator privileges.'
    }
}

function Resolve-Installer {
    if ($Installer -and (Test-Path -LiteralPath $Installer -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $Installer).Path
    }

    $outputDir = Join-Path $ScriptRoot 'output'
    $candidate = Get-ChildItem -LiteralPath $outputDir -Filter 'sdk-open_Windows_*.exe' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($candidate) {
        return $candidate.FullName
    }

    throw "Installer not found. Pass -Installer <path-to-sdk-open_Windows_*.exe>."
}

function Invoke-Native {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Description,
        [int[]]$AllowedExitCodes = @(0)
    )

    Write-Host "[sdk-open verify] $Description"
    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    if ($AllowedExitCodes -notcontains $exitCode) {
        throw "$Description failed with exit code $exitCode"
    }
}

function Show-InstallerDiagnostics {
    $logPath = Join-Path $env:ProgramData 'CZUR\sdk-open\install.log'
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        Write-Host "[sdk-open verify] Installer diagnostic log not found: $logPath"
        return
    }

    Write-Host "[sdk-open verify] Installer diagnostic log: $logPath"
    Get-Content -LiteralPath $logPath -Tail 100 | ForEach-Object {
        Write-Host $_
    }
}

function Invoke-Installer {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Description,
        [int]$TimeoutSeconds = $InstallerTimeoutSeconds
    )

    Write-Host "[sdk-open verify] $Description (timeout=${TimeoutSeconds}s)"
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Write-Host "[sdk-open verify] $Description timed out; terminating installer process $($process.Id)"
        try {
            & taskkill.exe /PID $process.Id /T /F | ForEach-Object {
                Write-Host "[sdk-open verify] $_"
            }
            if ($LASTEXITCODE -ne 0 -and -not $process.HasExited) {
                $process.Kill()
            }
            $process.WaitForExit(10000) | Out-Null
        }
        catch {
            Write-Warning "Unable to terminate installer process $($process.Id): $($_.Exception.Message)"
        }
        throw "$Description timed out after $TimeoutSeconds seconds"
    }
    if ($process.ExitCode -ne 0) {
        throw "$Description failed with exit code $($process.ExitCode)"
    }
}

function ConvertTo-Bool {
    param([string]$Value)
    if (-not $Value) {
        return $false
    }
    return @('1', 'true', 'yes', 'on') -contains $Value.Trim().ToLowerInvariant()
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
    Write-Host "[sdk-open verify] $Description Authenticode status is Valid: $Path"
}

function Assert-PathExists {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }
}

function Assert-FileReadable {
    param([string]$Path, [string]$Description)

    $stream = $null
    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite
        )
        if ($stream.Length -le 0) {
            throw "$Description is empty: $Path"
        }
    }
    catch {
        throw "$Description is not readable: $Path. $($_.Exception.Message)"
    }
    finally {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
    }
}

function Assert-NoSdkOpenBuildArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$Directory
    )

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
    if ($buildArtifacts.Count -gt 0) {
        throw "Installed SDK Open runtime contains build artifacts: $($buildArtifacts.FullName -join '; ')"
    }
    Write-Host "[sdk-open verify] No SDK Open build artifacts found under $Directory."
}

function Assert-ActiveHostMapping {
    param()

    $hostsPath = Join-Path $env:SystemRoot 'System32\drivers\etc\hosts'
    $activeHostPattern = '(?m)^[\t ]*[^#\r\n]*[\t ]sdk-runtime\.localhost([\t ]|$)'
    $hostsText = if (Test-Path -LiteralPath $hostsPath -PathType Leaf) {
        [IO.File]::ReadAllText($hostsPath)
    }
    else {
        ''
    }

    if (-not [regex]::IsMatch($hostsText, $activeHostPattern)) {
        throw "Active sdk-runtime.localhost hosts mapping is not present: $hostsPath"
    }
}

function Assert-ManagedHostMappingRemoved {
    $hostsPath = Join-Path $env:SystemRoot 'System32\drivers\etc\hosts'
    $managedLinePattern = '(?m)^[\t ]*127\.0\.0\.1[\t ]+sdk-runtime\.localhost[\t ]+# Local SDK runtime[\t ]*\r?$'
    $hostsText = if (Test-Path -LiteralPath $hostsPath -PathType Leaf) {
        [IO.File]::ReadAllText($hostsPath)
    }
    else {
        ''
    }

    if ([regex]::IsMatch($hostsText, $managedLinePattern)) {
        throw "Managed sdk-runtime.localhost hosts mapping still exists: $hostsPath"
    }
}

function Invoke-HostHelper {
    param(
        [string]$HelperPath,
        [ValidateSet('InstallHost', 'RemoveHost')][string]$Action,
        [string]$HostsPath
    )

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $HelperPath -Action $Action -HostsPath $HostsPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Host helper action '$Action' failed with exit code $exitCode for $HostsPath"
    }
}

function Test-InstalledHostHelper {
    param([string]$HelperPath)

    $testDirectory = Join-Path ([IO.Path]::GetTempPath()) ("sdk-open-hosts-verification-" + [Guid]::NewGuid().ToString('N'))
    $testHostsPath = Join-Path $testDirectory 'hosts'
    $managedLinePattern = '(?m)^[\t ]*127\.0\.0\.1[\t ]+sdk-runtime\.localhost[\t ]+# Local SDK runtime[\t ]*\r?$'

    try {
        New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
        Invoke-HostHelper -HelperPath $HelperPath -Action InstallHost -HostsPath $testHostsPath
        if (-not (Test-Path -LiteralPath $testHostsPath -PathType Leaf)) {
            throw "Host helper did not create missing hosts file: $testHostsPath"
        }

        $hostsText = [IO.File]::ReadAllText($testHostsPath)
        if ([regex]::Matches($hostsText, $managedLinePattern).Count -ne 1) {
            throw "Host helper did not add exactly one managed mapping: $testHostsPath"
        }

        Invoke-HostHelper -HelperPath $HelperPath -Action InstallHost -HostsPath $testHostsPath
        $hostsText = [IO.File]::ReadAllText($testHostsPath)
        if ([regex]::Matches($hostsText, $managedLinePattern).Count -ne 1) {
            throw "Host helper added a duplicate managed mapping: $testHostsPath"
        }

        Invoke-HostHelper -HelperPath $HelperPath -Action RemoveHost -HostsPath $testHostsPath
        $hostsText = [IO.File]::ReadAllText($testHostsPath)
        if ([regex]::IsMatch($hostsText, $managedLinePattern)) {
            throw "Host helper did not remove the managed mapping: $testHostsPath"
        }

        Remove-Item -LiteralPath $testHostsPath -Force
        Invoke-HostHelper -HelperPath $HelperPath -Action RemoveHost -HostsPath $testHostsPath
        Write-Host '[sdk-open verify] Packaged host helper missing-file recovery and cleanup verified.'
    }
    finally {
        Remove-Item -LiteralPath $testDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Get-PeMachine {
    param([Parameter(Mandatory = $true)][string]$Path)

    Assert-PathExists -Path $Path -Description 'PE binary'
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
        [Parameter(Mandatory = $true)][ValidateSet('x64', 'x86')][string]$ExpectedArchitecture,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $expectedMachine = if ($ExpectedArchitecture -eq 'x64') { 0x8664 } else { 0x014C }
    $actualMachine = Get-PeMachine -Path $Path
    if ($actualMachine -ne $expectedMachine) {
        throw ('{0} PE architecture mismatch: expected {1} (0x{2:X4}), actual 0x{3:X4}: {4}' -f $Description, $ExpectedArchitecture, $expectedMachine, $actualMachine, $Path)
    }
}

function Assert-TwainDsmManifest {
    param(
        [Parameter(Mandatory = $true)][string]$DsmPath,
        [Parameter(Mandatory = $true)][string]$LicensePath,
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Assert-FileReadable -Path $DsmPath -Description $Description
    Assert-FileReadable -Path $LicensePath -Description "$Description license"
    Assert-FileReadable -Path $ManifestPath -Description "$Description version and SHA-256 manifest"
    $manifest = [IO.File]::ReadAllText($ManifestPath)
    if (-not [regex]::IsMatch($manifest, '(?m)^TWAIN DSM Version:\s*\S+')) {
        throw "$Description manifest is missing TWAIN DSM Version: $ManifestPath"
    }
    $shaMatch = [regex]::Match($manifest, '(?m)^SHA256:\s*([0-9A-Fa-f]{64})\s*$')
    if (-not $shaMatch.Success) {
        throw "$Description manifest is missing a valid SHA256: $ManifestPath"
    }
    $actualSha = (Get-FileHash -LiteralPath $DsmPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($shaMatch.Groups[1].Value.ToLowerInvariant() -ne $actualSha) {
        throw "$Description manifest SHA256 does not match DSM: $ManifestPath"
    }
}

function Assert-TwainDsmRuntime {
    param([Parameter(Mandatory = $true)][string]$Directory)

    $helperDirectory = Join-Path $Directory 'twain_helper_runtime'
    $x86DsmDirectory = Join-Path $helperDirectory 'twain\x86'
    $x86Dsm = Join-Path $x86DsmDirectory 'TWAINDSM.dll'
    Assert-PeArchitecture -Path $x86Dsm -ExpectedArchitecture 'x86' -Description 'installed x86 TWAIN DSM'
    Assert-TwainDsmManifest -DsmPath $x86Dsm `
        -LicensePath (Join-Path $x86DsmDirectory 'TWAINDSM-x86-LICENSE.txt') `
        -ManifestPath (Join-Path $x86DsmDirectory 'TWAINDSM-x86-VERSION-SHA256.txt') `
        -Description 'installed x86 TWAIN DSM'

    $rootTwainDirectory = Join-Path $Directory 'twain'
    if (Test-Path -LiteralPath $rootTwainDirectory) {
        throw "sdk-open installation must not contain legacy root TWAIN runtime directory: $rootTwainDirectory"
    }
    if ($Architecture -eq 'x64') {
        $x64Dsm = Join-Path $Directory 'TWAINDSM.dll'
        $isolatedX64Dsm = Join-Path $helperDirectory 'TWAINDSM.dll'
        Assert-PeArchitecture -Path $x64Dsm -ExpectedArchitecture 'x64' -Description 'installed x64 TWAIN DSM'
        Assert-PeArchitecture -Path $isolatedX64Dsm -ExpectedArchitecture 'x64' -Description 'isolated x64 TWAIN DSM'
        Assert-TwainDsmManifest -DsmPath $x64Dsm `
            -LicensePath (Join-Path $Directory 'TWAINDSM-x64-LICENSE.txt') `
            -ManifestPath (Join-Path $Directory 'TWAINDSM-x64-VERSION-SHA256.txt') `
            -Description 'installed x64 TWAIN DSM'
        Assert-TwainDsmManifest -DsmPath $isolatedX64Dsm `
            -LicensePath (Join-Path $Directory 'TWAINDSM-x64-LICENSE.txt') `
            -ManifestPath (Join-Path $Directory 'TWAINDSM-x64-VERSION-SHA256.txt') `
            -Description 'isolated x64 TWAIN DSM'
    }
    elseif ((Test-Path -LiteralPath (Join-Path $Directory 'TWAINDSM.dll')) -or
            (Test-Path -LiteralPath (Join-Path $helperDirectory 'TWAINDSM.dll'))) {
        throw 'Win32 sdk-open installation must not contain an x64 TWAIN DSM.'
    }
}

function Assert-ArchitectureRuntime {
    param([Parameter(Mandatory = $true)][string]$Directory)

    # 安装后再次校验 PE 位数和 TWAIN helper 组合，防止打包阶段混入异架构文件 (PE/TWAIN).
    Assert-PeArchitecture -Path (Join-Path $Directory 'sdk_open_app.exe') -ExpectedArchitecture $Architecture -Description 'installed sdk_open_app.exe'
    Assert-PeArchitecture -Path (Join-Path $Directory 'sdk_private_providers.dll') -ExpectedArchitecture $Architecture -Description 'installed sdk_private_providers.dll'

    $x86Helper = Join-Path $Directory 'twain_helper_runtime\twain_helper_x86.exe'
    $x64Helper = Join-Path $Directory 'twain_helper_runtime\twain_helper_x64.exe'
    Assert-PeArchitecture -Path $x86Helper -ExpectedArchitecture 'x86' -Description 'installed x86 TWAIN helper'
    if ($Architecture -eq 'x64') {
        Assert-PeArchitecture -Path $x64Helper -ExpectedArchitecture 'x64' -Description 'installed x64 TWAIN helper'
        $opensslFiles = @('libcrypto-1_1-x64.dll', 'libssl-1_1-x64.dll', 'libcrypto-3-x64.dll', 'libssl-3-x64.dll')
    }
    else {
        if (Test-Path -LiteralPath $x64Helper) {
            throw "x86 sdk-open installation must not contain x64 TWAIN helper: $x64Helper"
        }
        $opensslFiles = @('libcrypto-1_1.dll', 'libssl-1_1.dll', 'libcrypto-3.dll', 'libssl-3.dll')
    }
    Assert-TwainDsmRuntime -Directory $Directory
    foreach ($fileName in $opensslFiles) {
        Assert-PathExists -Path (Join-Path $Directory $fileName) -Description "installed runtime dependency $fileName"
    }
}

function Wait-ServiceState {
    param(
        [string]$Name,
        [string]$ExpectedStatus,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $service = Get-Service -Name $Name -ErrorAction SilentlyContinue
        if ($service -and $service.Status.ToString() -eq $ExpectedStatus) {
            return $service
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $current = Get-Service -Name $Name -ErrorAction SilentlyContinue
    $status = if ($current) { $current.Status.ToString() } else { '<missing>' }
    throw "Service '$Name' did not reach status '$ExpectedStatus'. Current status: $status"
}

function Wait-ServiceDeleted {
    param(
        [string]$Name,
        [int]$TimeoutSeconds = 30
    )

    Write-Host "[sdk-open verify] Wait for service $Name to be deleted"
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        & sc.exe query $Name *> $null
        $exitCode = $LASTEXITCODE
        if ($exitCode -eq 1060) {
            Write-Host "[sdk-open verify] Service $Name was deleted."
            return
        }
        if ($exitCode -ne 0) {
            throw "Query service '$Name' failed with exit code $exitCode while waiting for deletion"
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    throw "Service '$Name' was not deleted within $TimeoutSeconds seconds"
}

function Wait-PathRemoved {
    param(
        [string]$Path,
        [string]$Description,
        [int]$TimeoutSeconds = 30
    )

    Write-Host "[sdk-open verify] Wait for $Description to be removed: $Path"
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (-not (Test-Path -LiteralPath $Path)) {
            Write-Host "[sdk-open verify] $Description was removed."
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    if (-not (Test-Path -LiteralPath $Path)) {
        Write-Host "[sdk-open verify] $Description was removed."
        return
    }

    Write-Host "[sdk-open verify] Remaining entries under ${Description}:"
    Get-ChildItem -LiteralPath $Path -Force -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 50 -ExpandProperty FullName |
        ForEach-Object { Write-Host "[sdk-open verify]   $_" }
    throw "$Description still exists after waiting $TimeoutSeconds seconds: $Path"
}

function Wait-HttpOk {
    param(
        [string]$Url,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        try {
            $response = Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 3
            if ($response.StatusCode -eq 200) {
                return $response
            }
        } catch {
            Start-Sleep -Milliseconds 500
        }
    } while ((Get-Date) -lt $deadline)

    throw "HTTP endpoint did not become available: $Url"
}

function Assert-ServiceConfig {
    param(
        [string]$Name,
        [string]$ExpectedInstallDir
    )

    $service = Get-CimInstance Win32_Service -Filter "Name='$Name'"
    if (-not $service) {
        throw "Service not found: $Name"
    }

    if ($service.StartMode -ne 'Auto') {
        throw "Service StartMode mismatch. Expected Auto, got $($service.StartMode)."
    }
    if ($service.State -ne 'Running') {
        throw "Service State mismatch. Expected Running, got $($service.State)."
    }
    if ($service.PathName -notmatch '--service' -or $service.PathName -notmatch '--service-name' -or $service.PathName -notmatch '--config') {
        throw "Service PathName does not include service-mode arguments: $($service.PathName)"
    }

    $serviceKey = "HKLM:\SYSTEM\CurrentControlSet\Services\$Name"
    $serviceProps = Get-ItemProperty -Path $serviceKey
    if ($serviceProps.AppDirectory -ne $ExpectedInstallDir) {
        throw "Service AppDirectory mismatch. Expected '$ExpectedInstallDir', got '$($serviceProps.AppDirectory)'."
    }
}

function Assert-DefaultTlsConfiguration {
    $tlsConfigDir = Join-Path $env:ProgramData 'CZUR\sdk-open'
    $tlsDirectory = Join-Path $tlsConfigDir 'tls'
    $tlsConfigPath = Join-Path $tlsConfigDir 'runtime.env'
    $twainRuntimeDirectory = Join-Path $tlsConfigDir 'twain-runtime'
    $publicRootCaPath = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::CommonDocuments)) 'CZUR\local-runtime-root-ca.cer'

    $certificatePath = Join-Path $tlsDirectory 'sdk-runtime.localhost.fullchain.pem'
    $privateKeyPath = Join-Path $tlsDirectory 'sdk-runtime.localhost.key.pem'
    $rootCaPath = Join-Path $tlsDirectory 'local-runtime-root-ca.crt'
    $rootPrivateKeyPath = Join-Path $tlsDirectory 'local-runtime-root-ca.key.pem'
    $tlsStatePath = Join-Path $tlsDirectory 'tls-state.json'

    Assert-PathExists -Path $tlsConfigPath -Description 'installed SDK Open TLS runtime configuration'
    Assert-PathExists -Path $certificatePath -Description 'installed local runtime TLS certificate'
    Assert-PathExists -Path $privateKeyPath -Description 'installed local runtime TLS private key'
    Assert-PathExists -Path $rootCaPath -Description 'installed local runtime root CA'
    Assert-PathExists -Path $rootPrivateKeyPath -Description 'installed local runtime root CA private key'
    Assert-PathExists -Path $tlsStatePath -Description 'installed local runtime TLS state'
    Assert-PathExists -Path $twainRuntimeDirectory -Description 'installed shared TWAIN runtime directory'
    Assert-FileReadable -Path $certificatePath -Description 'installed local runtime TLS certificate'
    Assert-FileReadable -Path $privateKeyPath -Description 'installed local runtime TLS private key'
    Assert-FileReadable -Path $rootCaPath -Description 'installed local runtime root CA'
    Assert-FileReadable -Path $rootPrivateKeyPath -Description 'installed local runtime root CA private key'
    Assert-FileReadable -Path $tlsStatePath -Description 'installed local runtime TLS state'
    Assert-PathExists -Path $publicRootCaPath -Description 'publicly readable local runtime root CA export'
    Assert-FileReadable -Path $publicRootCaPath -Description 'publicly readable local runtime root CA export'

    $config = Get-Content -LiteralPath $tlsConfigPath -Raw
    foreach ($expectedValue in @(
        'SDK_TLS_ENABLED=1',
        'SDK_TLS_BIND_HOST=127.0.0.1',
        'SDK_ASSET_BASE_URL=https://sdk-runtime.localhost:18082',
        "CZUR_TWAIN_WORK_DIR=$twainRuntimeDirectory"
    )) {
        if (-not $config.Contains($expectedValue)) {
            throw "SDK Open TLS configuration does not contain '$expectedValue': $tlsConfigPath"
        }
    }

    $hasUsersModifyAccess = $false
    foreach ($rule in (Get-Acl -LiteralPath $twainRuntimeDirectory).Access) {
        try {
            $sid = $rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value
            if ($sid -eq 'S-1-5-32-545' -and
                ($rule.FileSystemRights -band [Security.AccessControl.FileSystemRights]::Modify) -ne 0) {
                $hasUsersModifyAccess = $true
                break
            }
        }
        catch {
        }
    }
    if (-not $hasUsersModifyAccess) {
        throw "Interactive users do not have Modify access to the shared TWAIN runtime directory: $twainRuntimeDirectory"
    }

    $pem = [IO.File]::ReadAllText($rootCaPath)
    $body = [Regex]::Match($pem, '-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----',
        [Text.RegularExpressions.RegexOptions]::Singleline).Groups[1].Value -replace '\s', ''
    $rootCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new([Convert]::FromBase64String($body))
    $expectedThumbprint = $rootCertificate.Thumbprint
    $rootCertificate.Dispose()
    $trustedRoot = Get-ChildItem -Path Cert:\LocalMachine\Root |
        Where-Object { $_.Thumbprint -eq $expectedThumbprint } |
        Select-Object -First 1
    if (-not $trustedRoot) {
        throw "Installed local runtime root CA is not trusted in LocalMachine Root: $expectedThumbprint"
    }
    if (Test-Path -LiteralPath 'Cert:\LocalMachine\Root\4608C5BD34AC2327E1C7A4A6D6D874CC8235B482') {
        throw 'Obsolete shared SDK Open root CA remains trusted after installation'
    }
    if ((Get-FileHash -LiteralPath $rootCaPath -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $publicRootCaPath -Algorithm SHA256).Hash) {
        throw 'Public root CA export does not match the installed root'
    }
    return $expectedThumbprint
}

function Show-RecentServiceEvents {
    param([string]$Pattern)

    Write-Host "[sdk-open verify] Recent Service Control Manager events:"
    Get-WinEvent -LogName System -MaxEvents 80 |
        Where-Object { $_.ProviderName -eq 'Service Control Manager' -and $_.Message -match $Pattern } |
        Select-Object TimeCreated, Id, LevelDisplayName, Message |
        Format-List
}

Assert-Admin
$Installer = Resolve-Installer
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir)
$quickStartUrl = "http://127.0.0.1:$DemoPort/quick-start"
$assetHealthUrl = "https://sdk-runtime.localhost:$AssetHttpsPort/healthz"
$tlsConfigDir = Join-Path $env:ProgramData 'CZUR\sdk-open'
$publicRootCaPath = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::CommonDocuments)) 'CZUR\local-runtime-root-ca.cer'
$installerDiagnosticLogPath = Join-Path $tlsConfigDir 'install.log'
$RequireSigned = ConvertTo-Bool $RequireSignedText

Write-Host "[sdk-open verify] Installer: $Installer"
Write-Host "[sdk-open verify] Installer LastWriteTime: $((Get-Item -LiteralPath $Installer).LastWriteTime)"
Write-Host "[sdk-open verify] InstallDir: $InstallDir"
Write-Host "[sdk-open verify] Architecture: $Architecture"
Write-Host "[sdk-open verify] InstallerTimeoutSeconds=$InstallerTimeoutSeconds"
Write-Host "[sdk-open verify] RequireSigned=$RequireSigned"

try {
    Remove-Item -LiteralPath $installerDiagnosticLogPath -Force -ErrorAction SilentlyContinue
    if ($RequireSigned) {
        Assert-AuthenticodeValid -Path $Installer -Description 'sdk-open installer'
    }

    Invoke-Installer -FilePath $Installer -Arguments @('/S', "/D=$InstallDir") -Description 'Install sdk-open NSIS package'

    $installedAppPath = Join-Path $InstallDir 'sdk_open_app.exe'
    $installedUninstallerPath = Join-Path $InstallDir 'uninstall.exe'
    $installedHelperPath = Join-Path $InstallDir 'sdk-open-install-helper.ps1'
    Assert-PathExists -Path $installedAppPath -Description 'installed sdk_open_app.exe'
    Assert-PathExists -Path (Join-Path $InstallDir 'sdk_private_auth_c_api.dll') -Description 'installed sdk_private_auth_c_api.dll'
    Assert-PathExists -Path (Join-Path $InstallDir 'sdk_private_providers.dll') -Description 'installed sdk_private_providers.dll'
    Assert-PathExists -Path $installedUninstallerPath -Description 'installed uninstall.exe'
    Assert-PathExists -Path $installedHelperPath -Description 'installed sdk-open installer helper'
    Assert-NoSdkOpenBuildArtifacts -Directory $InstallDir
    Assert-ArchitectureRuntime -Directory $InstallDir
    $installedRootThumbprint = Assert-DefaultTlsConfiguration
    Assert-ActiveHostMapping
    Test-InstalledHostHelper -HelperPath $installedHelperPath
    if ($RequireSigned) {
        Assert-AuthenticodeValid -Path $installedAppPath -Description 'installed sdk_open_app.exe'
        Assert-AuthenticodeValid -Path $installedUninstallerPath -Description 'installed uninstall.exe'
    }
    Wait-ServiceState -Name $ServiceName -ExpectedStatus 'Running' | Out-Null
    $installedService = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'"
    Write-Host "[sdk-open verify] Service PathName: $($installedService.PathName)"
    Assert-ServiceConfig -Name $ServiceName -ExpectedInstallDir $InstallDir
    Wait-HttpOk -Url $quickStartUrl | Out-Null
    Write-Host "[sdk-open verify] Quick Start is reachable: $quickStartUrl"
    Wait-HttpOk -Url $assetHealthUrl | Out-Null
    Write-Host "[sdk-open verify] TLS asset health endpoint is reachable: $assetHealthUrl"

    Invoke-Native -FilePath 'sc.exe' -Arguments @('stop', $ServiceName) -Description "Stop service $ServiceName before repair install"
    Wait-ServiceState -Name $ServiceName -ExpectedStatus 'Stopped' | Out-Null
    Invoke-Installer -FilePath $Installer -Arguments @('/S', "/D=$InstallDir") -Description 'Repair-install sdk-open while service is stopped'
    Wait-ServiceState -Name $ServiceName -ExpectedStatus 'Running' | Out-Null
    Assert-ServiceConfig -Name $ServiceName -ExpectedInstallDir $InstallDir
    Wait-HttpOk -Url $quickStartUrl | Out-Null
    Wait-HttpOk -Url $assetHealthUrl | Out-Null
    $repairedRootThumbprint = Assert-DefaultTlsConfiguration
    if ($repairedRootThumbprint -ne $installedRootThumbprint) {
        throw 'Repair installation replaced the trusted machine-local root CA'
    }
    Write-Host '[sdk-open verify] Repair install with stopped service verified.'

    if (-not $KeepInstalled) {
        Invoke-Native -FilePath 'sc.exe' -Arguments @('stop', $ServiceName) -Description "Stop service $ServiceName before uninstall"
        Wait-ServiceState -Name $ServiceName -ExpectedStatus 'Stopped' | Out-Null
        Invoke-Installer -FilePath $installedUninstallerPath -Arguments @('/S') -Description 'Uninstall sdk-open NSIS package while service is stopped'
        Wait-ServiceDeleted -Name $ServiceName
        Wait-PathRemoved -Path $InstallDir -Description 'Install directory'
        Wait-PathRemoved -Path $tlsConfigDir -Description 'SDK Open TLS configuration directory'
        Wait-PathRemoved -Path $publicRootCaPath -Description 'public SDK Open root CA export'
        Assert-ManagedHostMappingRemoved
        if (Get-ChildItem -Path Cert:\LocalMachine\Root | Where-Object { $_.Thumbprint -eq $installedRootThumbprint }) {
            throw 'Local runtime root CA still exists after uninstall'
        }
        if (Test-Path -LiteralPath 'Cert:\LocalMachine\Root\4608C5BD34AC2327E1C7A4A6D6D874CC8235B482') {
            throw 'Obsolete shared SDK Open root CA remains trusted after uninstall'
        }
        Write-Host '[sdk-open verify] Uninstall and cleanup verified.'
    }

    Write-Host '[sdk-open verify] Verification completed.'
} catch {
    Show-InstallerDiagnostics
    Show-RecentServiceEvents -Pattern 'sdk-open|SDK Open|sdk_open_app'
    throw
}
