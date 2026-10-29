param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('EnsureTwainRuntime', 'RemoveBuildArtifacts', 'InstallHost', 'RemoveHost', 'WaitQuickStart', 'InstallVCRedist', 'ProvisionTls', 'InstallTlsTrust', 'RemoveTrustedRoot')]
    [string]$Action,
    [string]$Url = 'http://127.0.0.1:17081/quick-start',
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 45,
    [string]$HostsPath = '',
    [string]$InstallerPath = '',
    [string]$RuntimeConfigPath = '',
    [string]$TwainRuntimeDirectory = '',
    [string]$RuntimeDirectory = '',
    [string]$ExecutablePath = '',
    [string]$TlsDirectory = '',
    [string]$LegacyCertificatePath = '',
    [string]$LegacyPrivateKeyPath = '',
    [string]$LegacyCertificateHash = '',
    [string]$LegacyPrivateKeyHash = '',
    [string]$PublicRootCaPath = '',
    [string]$CertutilPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$hostsPath = if ($HostsPath -and $HostsPath.Trim().Length -gt 0) {
    [IO.Path]::GetFullPath($HostsPath)
}
else {
    Join-Path $env:SystemRoot 'System32\drivers\etc\hosts'
}
$hostName = 'sdk-runtime.localhost'
$managedLine = "127.0.0.1 $hostName # Local SDK runtime"
$activeHostPattern = '(?m)^[\t ]*[^#\r\n]*[\t ]{0}([\t ]|$)' -f [Regex]::Escape($hostName)
$managedLinePattern = '# Local SDK runtime[\t ]*$'
$installLogDir = Join-Path $env:ProgramData 'CZUR\sdk-open'
$installLogPath = Join-Path $installLogDir 'install.log'

function Write-InstallDiagnostic {
    param(
        [string]$Message,
        [switch]$ErrorMessage
    )

    $line = "$(Get-Date -Format o) [sdk-open install] $Message"
    try {
        if (-not (Test-Path -LiteralPath $installLogDir)) {
            New-Item -ItemType Directory -Path $installLogDir -Force | Out-Null
        }
        Add-Content -LiteralPath $installLogPath -Value $line -Encoding UTF8
    }
    catch {
        # Diagnostic logging must not hide the original installer result.
    }

    if ($ErrorMessage) {
        [Console]::Error.WriteLine($line)
    }
    else {
        Write-Host $line
    }
}

function Ensure-HostsFile {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return
    }
    if (Test-Path -LiteralPath $Path) {
        throw "Windows hosts path is not a regular file: $Path"
    }

    $hostsDirectory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $hostsDirectory -PathType Container)) {
        throw "Windows hosts directory not found: $hostsDirectory"
    }

    [IO.File]::WriteAllText($Path, '', [Text.UTF8Encoding]::new($false))
    Write-InstallDiagnostic "Created missing Windows hosts file: $Path"
}

function Ensure-TwainRuntime {
    param(
        [string]$ConfigPath,
        [string]$RuntimeDirectory
    )

    if (-not $ConfigPath -or $ConfigPath.Trim().Length -eq 0) {
        $ConfigPath = Join-Path $installLogDir 'runtime.env'
    }
    else {
        $ConfigPath = [IO.Path]::GetFullPath($ConfigPath)
    }

    if (-not $RuntimeDirectory -or $RuntimeDirectory.Trim().Length -eq 0) {
        $RuntimeDirectory = Join-Path $installLogDir 'twain-runtime'
    }
    else {
        $RuntimeDirectory = [IO.Path]::GetFullPath($RuntimeDirectory)
    }

    if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
        throw "SDK Open runtime configuration not found: $ConfigPath"
    }

    New-Item -ItemType Directory -Path $RuntimeDirectory -Force | Out-Null

    $icaclsPath = Join-Path $env:SystemRoot 'System32\icacls.exe'
    & $icaclsPath $RuntimeDirectory /inheritance:r /grant:r `
        '*S-1-5-18:(OI)(CI)F' `
        '*S-1-5-32-544:(OI)(CI)F' `
        '*S-1-5-32-545:(OI)(CI)M' /c
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to configure shared TWAIN runtime directory permissions: $RuntimeDirectory (icacls exit code $LASTEXITCODE)"
    }

    $config = [IO.File]::ReadAllText($ConfigPath)
    if ($config -notmatch '(?m)^[\t ]*CZUR_TWAIN_WORK_DIR[\t ]*=') {
        $separator = if ($config.Length -eq 0 -or $config.EndsWith("`r") -or $config.EndsWith("`n")) { '' } else { [Environment]::NewLine }
        [IO.File]::AppendAllText(
            $ConfigPath,
            "$separator" + "CZUR_TWAIN_WORK_DIR=$RuntimeDirectory" + [Environment]::NewLine,
            [Text.UTF8Encoding]::new($false)
        )
        Write-InstallDiagnostic "Configured shared TWAIN runtime directory: $RuntimeDirectory"
    }
    else {
        Write-InstallDiagnostic "Preserved configured shared TWAIN runtime directory in: $ConfigPath"
    }
}

function Remove-BuildArtifacts {
    param([string]$Directory)

    if (-not $Directory -or $Directory.Trim().Length -eq 0) {
        throw 'RuntimeDirectory is required for RemoveBuildArtifacts'
    }
    $Directory = [IO.Path]::GetFullPath($Directory)
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        throw "SDK Open runtime directory not found: $Directory"
    }

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
        Remove-Item -LiteralPath $buildArtifact.FullName -Force
        Write-InstallDiagnostic "Removed stale SDK Open build artifact: $($buildArtifact.FullName)"
    }

    $remainingArtifacts = @(
        Get-ChildItem -LiteralPath $Directory -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $forbiddenExtensions -contains $_.Extension.ToLowerInvariant() }
    )
    if ($remainingArtifacts.Count -gt 0) {
        throw "SDK Open build artifacts remain in runtime directory: $($remainingArtifacts.FullName -join '; ')"
    }
}

function Get-CertificateThumbprint {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }

    # The packaged root CA is PEM encoded. Windows certificate-store
    # Thumbprint values are SHA-1 fingerprints of the DER certificate, so a
    # file SHA-256 (Get-FileHash) cannot be compared with Cert:\...\Root.
    $pem = [IO.File]::ReadAllText([IO.Path]::GetFullPath($Path))
    $match = [Regex]::Match(
        $pem,
        '-----BEGIN CERTIFICATE-----(?<body>.*?)-----END CERTIFICATE-----',
        [Text.RegularExpressions.RegexOptions]::Singleline
    )
    if (-not $match.Success) {
        throw "Certificate is not PEM encoded: $Path"
    }

    $base64 = $match.Groups['body'].Value -replace '\s', ''
    $der = [Convert]::FromBase64String($base64)
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($der)
    try {
        return $certificate.Thumbprint.Replace(' ', '').ToUpperInvariant()
    }
    finally {
        $certificate.Dispose()
    }
}

function Remove-LegacyTrustedRoot {
    # SHA-1 of the DER certificate shipped before per-machine provisioning.
    # Match the exact product certificate, never all roots sharing its subject.
    $legacyRootPath = 'Cert:\LocalMachine\Root\4608C5BD34AC2327E1C7A4A6D6D874CC8235B482'
    if (Test-Path -LiteralPath $legacyRootPath) {
        Remove-Item -LiteralPath $legacyRootPath -Force
        Write-InstallDiagnostic 'Removed obsolete packaged SDK Open root CA trust'
    }
}

try {
    if ($Action -eq 'EnsureTwainRuntime') {
        Ensure-TwainRuntime -ConfigPath $RuntimeConfigPath -RuntimeDirectory $TwainRuntimeDirectory
        exit 0
    }

    if ($Action -eq 'RemoveBuildArtifacts') {
        Remove-BuildArtifacts -Directory $RuntimeDirectory
        exit 0
    }

    if ($Action -eq 'InstallVCRedist') {
        if (-not $InstallerPath -or $InstallerPath.Trim().Length -eq 0) {
            throw 'InstallerPath is required for InstallVCRedist'
        }

        $resolvedInstallerPath = [IO.Path]::GetFullPath($InstallerPath)
        if (-not (Test-Path -LiteralPath $resolvedInstallerPath -PathType Leaf)) {
            throw "VC++ Redistributable installer not found: $resolvedInstallerPath"
        }

        $process = Start-Process `
            -FilePath $resolvedInstallerPath `
            -ArgumentList @('/install', '/quiet', '/norestart') `
            -WindowStyle Hidden `
            -Wait `
            -PassThru
        $acceptedExitCodes = @(0, 3010, 1638, -2147023258)
        if ($acceptedExitCodes -contains $process.ExitCode) {
            Write-InstallDiagnostic "VC++ Redistributable completed with accepted exit code $($process.ExitCode)"
            exit 0
        }

        throw "VC++ Redistributable installer failed with exit code $($process.ExitCode)"
    }

    if ($Action -eq 'ProvisionTls') {
        if (-not $ExecutablePath -or $ExecutablePath.Trim().Length -eq 0) {
            throw 'ExecutablePath is required for ProvisionTls'
        }
        if (-not $TlsDirectory -or $TlsDirectory.Trim().Length -eq 0) {
            throw 'TlsDirectory is required for ProvisionTls'
        }
        $resolvedExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
        $resolvedTlsDirectory = [IO.Path]::GetFullPath($TlsDirectory)
        if (-not (Test-Path -LiteralPath $resolvedExecutablePath -PathType Leaf)) {
            throw "SDK Open executable not found: $resolvedExecutablePath"
        }
        New-Item -ItemType Directory -Path $resolvedTlsDirectory -Force | Out-Null
        $statePath = Join-Path $resolvedTlsDirectory 'tls-state.json'
        $certificatePath = Join-Path $resolvedTlsDirectory 'sdk-runtime.localhost.fullchain.pem'
        $privateKeyPath = Join-Path $resolvedTlsDirectory 'sdk-runtime.localhost.key.pem'
        $rootCertificatePath = Join-Path $resolvedTlsDirectory 'local-runtime-root-ca.crt'
        $rootPrivateKeyPath = Join-Path $resolvedTlsDirectory 'local-runtime-root-ca.key.pem'
        $migrationBackupDirectory = ''
        $retainMigrationBackup = $false
        $migrationPaths = @($certificatePath, $privateKeyPath, $rootCertificatePath, $rootPrivateKeyPath, $statePath)
        foreach ($path in @($migrationPaths)) { $migrationPaths += "$path.next" }
        foreach ($path in @($certificatePath, $privateKeyPath)) {
            $migrationPaths += "$path.rotation", "$path.rotation.next", "$path.previous"
        }
        $hasCertificate = Test-Path -LiteralPath $certificatePath -PathType Leaf
        $hasPrivateKey = Test-Path -LiteralPath $privateKeyPath -PathType Leaf
        if ($hasCertificate -ne $hasPrivateKey) {
            throw 'TLS certificate and private key must be replaced as a matching pair'
        }
        if (Test-Path -LiteralPath $statePath -PathType Leaf) {
            $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
            if ($state.mode -notin @('managed', 'custom')) {
                throw "Unknown SDK Open TLS state: $statePath"
            }
            if ($state.mode -eq 'managed' -and
                (-not (Test-Path -LiteralPath $rootCertificatePath -PathType Leaf) -or
                 -not (Test-Path -LiteralPath $rootPrivateKeyPath -PathType Leaf))) {
                throw 'Managed TLS root certificate and private key are incomplete; restore the original CA pair'
            }
            if ($state.mode -eq 'custom' -and -not $hasCertificate) {
                throw 'Customer TLS certificate and private key are missing'
            }
        }
        elseif (-not (Test-Path -LiteralPath $rootPrivateKeyPath)) {
            # Pre-rotation packages shipped a public root, never its private key.
            # Only discard exact known defaults. Unknown roots remain untouched
            # and provisioning reports the incomplete CA pair to the installer.
            $isLegacyRoot = (Test-Path -LiteralPath $rootCertificatePath -PathType Leaf) -and
                (Get-FileHash -LiteralPath $rootCertificatePath -Algorithm SHA256).Hash -eq
                    '4405b523122776ada5d3559bad492689b6edf161f9186e6893c0a5700575793c'
            if (-not (Test-Path -LiteralPath $rootCertificatePath) -or $isLegacyRoot) {
                $legacyCertificateHashes = @(
                    '6519c52926b4cc3399b242dd44077f38b39457e080c56b6fe2f09a8af92ad32e',
                    '3b70d132fef144f6da10e732c8265029794757f1ebb258c9cb5541e4485b18b0'
                )
                $legacyKeyHash = 'ceae324c3be1d43a19ea983c8bbd2f757001486265c7a6596ece42134f01cf11'
                # Retain compatibility with earlier callers that supplied seed paths/hashes.
                if ($LegacyCertificatePath) {
                    $legacyCertificateHashes += (Get-FileHash -LiteralPath $LegacyCertificatePath -Algorithm SHA256).Hash
                }
                if ($LegacyCertificateHash) { $legacyCertificateHashes += $LegacyCertificateHash }
                if ($LegacyPrivateKeyPath) {
                    $legacyKeyHash = (Get-FileHash -LiteralPath $LegacyPrivateKeyPath -Algorithm SHA256).Hash
                }
                elseif ($LegacyPrivateKeyHash) { $legacyKeyHash = $LegacyPrivateKeyHash }
                $isLegacyLeafPair = $hasCertificate -and
                    $legacyCertificateHashes -contains (Get-FileHash -LiteralPath $certificatePath -Algorithm SHA256).Hash -and
                    (Get-FileHash -LiteralPath $privateKeyPath -Algorithm SHA256).Hash -eq $legacyKeyHash
                if ($isLegacyLeafPair -or $isLegacyRoot) {
                    $migrationBackupDirectory = Join-Path $resolvedTlsDirectory ('.legacy-migration.' + [Guid]::NewGuid().ToString('N'))
                    New-Item -ItemType Directory -Path $migrationBackupDirectory | Out-Null
                    foreach ($path in $migrationPaths) {
                        if (Test-Path -LiteralPath $path -PathType Leaf) {
                            Copy-Item -LiteralPath $path -Destination $migrationBackupDirectory
                        }
                    }
                }
            }
        }
        try {
            if ($migrationBackupDirectory) {
                if ($isLegacyLeafPair) {
                    Remove-Item -LiteralPath $certificatePath, $privateKeyPath -Force
                    Write-InstallDiagnostic 'Migrating packaged legacy TLS credentials to a machine-local CA'
                }
                if ($isLegacyRoot) { Remove-Item -LiteralPath $rootCertificatePath -Force }
            }
            & $resolvedExecutablePath --provision-tls --provision-tls-dir $resolvedTlsDirectory
            if ($LASTEXITCODE -ne 0) {
                throw "SDK Open TLS provisioning failed with exit code $LASTEXITCODE"
            }
        }
        catch {
            if ($migrationBackupDirectory) {
                # Migration starts only without state/root key. Remove partial
                # generated files and restore the complete original credentials.
                $retainMigrationBackup = $true
                foreach ($path in $migrationPaths) {
                    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force -ErrorAction Stop }
                }
                Get-ChildItem -LiteralPath $migrationBackupDirectory -File |
                    Copy-Item -Destination $resolvedTlsDirectory -Force
                $retainMigrationBackup = $false
                Write-InstallDiagnostic 'TLS migration failed; restored original credentials'
            }
            throw
        }
        finally {
            if ($migrationBackupDirectory -and -not $retainMigrationBackup) {
                Remove-Item -LiteralPath $migrationBackupDirectory -Recurse -Force
            }
            elseif ($retainMigrationBackup) {
                Write-InstallDiagnostic "Unable to restore TLS credentials; backup retained at $migrationBackupDirectory" -ErrorMessage
            }
        }
        Write-InstallDiagnostic "Provisioned local runtime TLS credentials in: $resolvedTlsDirectory"
        exit 0
    }

    if ($Action -eq 'InstallTlsTrust') {
        if (-not $TlsDirectory -or -not $PublicRootCaPath) {
            throw 'TlsDirectory and PublicRootCaPath are required for InstallTlsTrust'
        }
        $statePath = Join-Path $TlsDirectory 'tls-state.json'
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        if ($state.mode -eq 'custom') {
            Remove-LegacyTrustedRoot
            Write-InstallDiagnostic 'Customer TLS pair is active; skipping SDK root trust import and export'
            exit 0
        }
        if ($state.mode -ne 'managed') { throw "Unknown SDK Open TLS state: $statePath" }
        $rootCertificatePath = Join-Path $TlsDirectory 'local-runtime-root-ca.crt'
        $certutilPath = if ($CertutilPath -and $CertutilPath.Trim().Length -gt 0) {
            [IO.Path]::GetFullPath($CertutilPath)
        }
        else {
            Join-Path $env:SystemRoot 'System32\certutil.exe'
        }
        & $certutilPath -addstore -f Root $rootCertificatePath
        if ($LASTEXITCODE -ne 0) { throw "Unable to trust SDK Open root CA (certutil exit $LASTEXITCODE)" }
        $thumbprint = Get-CertificateThumbprint -Path $rootCertificatePath
        if (-not (Test-Path -LiteralPath "Cert:\LocalMachine\Root\$thumbprint")) {
            throw 'SDK Open root CA is absent from LocalMachine Root after import'
        }
        # Copy directly to the final filename: Rename would retain a stale .cer
        # on repair installations. Only the public root is exported.
        New-Item -ItemType Directory -Path (Split-Path -Parent $PublicRootCaPath) -Force | Out-Null
        Copy-Item -LiteralPath $rootCertificatePath -Destination $PublicRootCaPath -Force
        Remove-LegacyTrustedRoot
        Write-InstallDiagnostic "Trusted and exported SDK Open root CA: $thumbprint"
        exit 0
    }

    if ($Action -eq 'RemoveTrustedRoot') {
        Remove-LegacyTrustedRoot
        if ($TlsDirectory) {
            $statePath = Join-Path $TlsDirectory 'tls-state.json'
            if (Test-Path -LiteralPath $statePath -PathType Leaf) {
                $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
                if ($state.mode -eq 'custom') {
                    Write-InstallDiagnostic 'Customer TLS mode: leaving customer trust settings unchanged'
                    exit 0
                }
            }
        }
        $expectedThumbprint = ''
        if ($TlsDirectory -and $TlsDirectory.Trim().Length -gt 0) {
            $rootCertificatePath = Join-Path ([IO.Path]::GetFullPath($TlsDirectory)) 'local-runtime-root-ca.crt'
            $expectedThumbprint = Get-CertificateThumbprint -Path $rootCertificatePath
        }
        $removed = 0
        foreach ($certificate in @(Get-ChildItem -Path Cert:\LocalMachine\Root |
                                   Where-Object {
                                       $_.Subject -eq 'CN=Local Runtime Root CA' -and
                                       $expectedThumbprint -and $_.Thumbprint -eq $expectedThumbprint
                                   })) {
            Remove-Item -LiteralPath $certificate.PSPath -Force
            $removed++
        }
        Write-InstallDiagnostic "Removed $removed local runtime root CA certificate(s)"
        exit 0
    }

    if ($Action -eq 'WaitQuickStart') {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        do {
            try {
                $response = Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 3
                if ($response.StatusCode -eq 200) {
                    Write-InstallDiagnostic "Quick Start is reachable: $Url"
                    exit 0
                }
            }
            catch {
                Start-Sleep -Milliseconds 500
            }
        } while ((Get-Date) -lt $deadline)

        throw "Quick Start did not become reachable within $TimeoutSeconds seconds: $Url"
    }

    if ($Action -eq 'InstallHost') {
        Ensure-HostsFile -Path $hostsPath
        $text = [IO.File]::ReadAllText($hostsPath)
        if ($text -match $activeHostPattern) {
            Write-InstallDiagnostic "Active mapping already exists for $hostName"
            exit 0
        }

        $separator = if ($text.Length -eq 0 -or $text.EndsWith("`r") -or $text.EndsWith("`n")) { '' } else { [Environment]::NewLine }
        [IO.File]::AppendAllText(
            $hostsPath,
            $separator + $managedLine + [Environment]::NewLine,
            [Text.UTF8Encoding]::new($false)
        )
        Write-InstallDiagnostic "Added hosts mapping: $managedLine"
        exit 0
    }

    if (-not (Test-Path -LiteralPath $hostsPath -PathType Leaf)) {
        Write-InstallDiagnostic 'Managed hosts mapping is already absent because the hosts file does not exist'
        exit 0
    }

    $lines = @([IO.File]::ReadAllLines($hostsPath))
    $retainedLines = @($lines | Where-Object { $_ -notmatch $managedLinePattern })
    if ($retainedLines.Count -eq $lines.Count) {
        Write-InstallDiagnostic 'Managed hosts mapping is already absent'
        exit 0
    }

    [IO.File]::WriteAllLines($hostsPath, $retainedLines, [Text.UTF8Encoding]::new($false))
    Write-InstallDiagnostic 'Removed managed hosts mapping'
}
catch {
    Write-InstallDiagnostic "$Action failed: $($_.Exception.Message)" -ErrorMessage
    exit 1
}
