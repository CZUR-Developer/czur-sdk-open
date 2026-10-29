param(
    [Parameter(Mandatory = $true)][string]$RuntimeExecutable
)

# Uses temporary TLS directories only. No service, hosts, ACL or trust-store writes.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$helper = Join-Path $PSScriptRoot 'sdk-open-install-helper.ps1'
$fixtures = Join-Path (Split-Path -Parent $PSScriptRoot) 'tls/runtime'
$oldLeaf = Join-Path (Split-Path -Parent $PSScriptRoot) 'macos/tls/legacy-sdk-runtime.localhost.pem'
$RuntimeExecutable = (Resolve-Path -LiteralPath $RuntimeExecutable).Path
$shell = (Get-Process -Id $PID).Path
$workspace = Join-Path ([IO.Path]::GetTempPath()) ('sdk tls tests ' + [Guid]::NewGuid().ToString('N'))
$savedProgramData = $env:ProgramData
$savedSystemRoot = $env:SystemRoot

function Assert($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Compact-Output([string]$Text) {
    return [Regex]::Replace($Text, '\s+', '')
}
function New-Case([string]$Name) {
    $directory = Join-Path $workspace $Name
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    return $directory
}
function Invoke-Helper([string]$Directory, [int]$ExpectedExit = 0, [string]$Action = 'ProvisionTls', [string]$Executable = $RuntimeExecutable) {
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # Windows PowerShell 5.1 wraps redirected native stderr in error
        # records. Expected failures must reach the explicit exit-code check.
        $ErrorActionPreference = 'Continue'
        if ($Action -eq 'InstallTlsTrust') {
            $output = & $shell -NoProfile -NonInteractive -File $trustHarness `
                -Helper $helper -Directory $Directory 2>&1 | Out-String
        }
        else {
            $output = & $shell -NoProfile -NonInteractive -File $helper -Action $Action `
                -ExecutablePath $Executable -TlsDirectory $Directory `
                -PublicRootCaPath (Join-Path $Directory 'public/root.cer') 2>&1 | Out-String
        }
        $actualExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $savedErrorActionPreference }
    Assert ($actualExit -eq $ExpectedExit) "Unexpected helper result ($actualExit instead of $ExpectedExit): $output"
}
function Read-Mode([string]$Directory) {
    return (Get-Content -LiteralPath (Join-Path $Directory 'tls-state.json') -Raw | ConvertFrom-Json).mode
}
function Seed-Legacy([string]$Directory, [string]$Leaf) {
    Copy-Item -LiteralPath $Leaf -Destination (Join-Path $Directory 'sdk-runtime.localhost.fullchain.pem')
    foreach ($name in @('sdk-runtime.localhost.key.pem', 'local-runtime-root-ca.crt')) {
        Copy-Item -LiteralPath (Join-Path $fixtures $name) -Destination (Join-Path $Directory $name)
    }
}
function Snapshot([string]$Directory) {
    return @(Get-ChildItem -LiteralPath $Directory -File | Sort-Object Name | ForEach-Object {
        $_.Name + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }) -join '|'
}

try {
    New-Item -ItemType Directory -Path $workspace | Out-Null
    $env:ProgramData = Join-Path $workspace 'program data'
    # Non-Windows hosts have no SystemRoot, while the helper builds a hosts path.
    if (-not $env:SystemRoot) { $env:SystemRoot = $workspace }
    # Keep SystemRoot intact: Windows PowerShell 5.1 uses it to locate the .NET
    # GAC. The trust-failure wrapper overrides only the certutil executable.
    $trustHarness = Join-Path $workspace 'trust-boundary.ps1'
    @'
param([string]$Helper, [string]$Directory)
$certutilPath = Join-Path $PSScriptRoot 'missing-certutil.exe'
function Test-Path {
    param([string]$LiteralPath, [string]$PathType)
    if ($LiteralPath -like 'Cert:*') { return $false }
    return Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
}
& $Helper -Action InstallTlsTrust -TlsDirectory $Directory -PublicRootCaPath (Join-Path $Directory 'public/root.cer') -CertutilPath $certutilPath
exit $LASTEXITCODE
'@ | Set-Content -LiteralPath $trustHarness -Encoding UTF8

    $fresh = New-Case 'fresh'
    Invoke-Helper $fresh
    Assert ((Read-Mode $fresh) -eq 'managed') 'Fresh install must be managed'
    $before = Snapshot $fresh
    Invoke-Helper $fresh
    Assert ((Snapshot $fresh) -eq $before) 'Repeat provisioning changed managed credentials'
    Invoke-Helper $fresh 1 'InstallTlsTrust'
    Assert (-not (Test-Path (Join-Path $fresh 'public/root.cer'))) 'Export created after trust failure'
    Write-Host 'PASS: fresh, repeat and trust failure'

    $index = 0
    foreach ($leaf in @($oldLeaf, (Join-Path $fixtures 'sdk-runtime.localhost.fullchain.pem'))) {
        $legacy = New-Case "legacy-$index"
        Seed-Legacy $legacy $leaf
        Invoke-Helper $legacy
        Assert ((Read-Mode $legacy) -eq 'managed') 'Legacy pair was incorrectly classified as custom'
        Assert ((Get-FileHash (Join-Path $legacy 'local-runtime-root-ca.crt')).Hash -ne
            (Get-FileHash (Join-Path $fixtures 'local-runtime-root-ca.crt')).Hash) 'Old bundled root survived migration'
        Assert ((Get-FileHash (Join-Path $legacy 'sdk-runtime.localhost.key.pem')).Hash -ne
            (Get-FileHash (Join-Path $fixtures 'sdk-runtime.localhost.key.pem')).Hash) 'Old shared key survived migration'
        $before = Snapshot $legacy
        Invoke-Helper $legacy
        Assert ((Snapshot $legacy) -eq $before) 'Repeat migration changed the machine CA'
        $index++
    }
    Write-Host 'PASS: both legacy leaf versions migrate and survive repeat install'

    $failingRuntime = Join-Path $workspace 'failing-runtime.ps1'
    @'
$directory = $args[2]
Set-Content (Join-Path $directory 'local-runtime-root-ca.key.pem') 'partial generated root key'
Set-Content (Join-Path $directory 'local-runtime-root-ca.key.pem.next') 'partial temporary root key'
Set-Content (Join-Path $directory 'sdk-runtime.localhost.fullchain.pem') 'partial generated leaf'
Set-Content (Join-Path $directory 'sdk-runtime.localhost.key.pem.rotation.next') 'partial temporary leaf key'
exit 9
'@ | Set-Content -LiteralPath $failingRuntime -Encoding UTF8
    foreach ($customerReplacement in @($false, $true)) {
        $failedMigration = New-Case "failed-migration-$customerReplacement"
        Seed-Legacy $failedMigration $oldLeaf
        if ($customerReplacement) { Set-Content (Join-Path $failedMigration 'sdk-runtime.localhost.key.pem') 'customer key' }
        $before = Snapshot $failedMigration
        Invoke-Helper $failedMigration 1 'ProvisionTls' $failingRuntime
        Assert ((Snapshot $failedMigration) -eq $before) 'Failed migration did not restore the original credentials'
        Assert (@(Get-ChildItem $failedMigration -Directory).Count -eq 0) 'Migration backup was left behind'
    }
    Write-Host 'PASS: failed migration restores legacy and customer credentials'

    $lockedFileHarness = Join-Path $workspace 'locked-file.ps1'
    @'
param([string]$Helper, [string]$Directory, [string]$Runtime, [string]$FailureMode)
$global:SdkTlsBlockedRoot = Join-Path $Directory 'local-runtime-root-ca.crt'
$global:SdkTlsDeleteAttempts = 0
function Remove-Item {
    [CmdletBinding()]
    param([string[]]$LiteralPath, [switch]$Force, [switch]$Recurse)
    if ($LiteralPath -contains $global:SdkTlsBlockedRoot) {
        $global:SdkTlsDeleteAttempts++
        if ($FailureMode -eq 'always' -or $global:SdkTlsDeleteAttempts -eq 1) { throw 'simulated locked root certificate' }
    }
    Microsoft.PowerShell.Management\Remove-Item @PSBoundParameters
}
& $Helper -Action ProvisionTls -TlsDirectory $Directory -ExecutablePath $Runtime
exit $LASTEXITCODE
'@ | Set-Content -LiteralPath $lockedFileHarness -Encoding UTF8
    foreach ($failureMode in @('once', 'always')) {
        $locked = New-Case "locked-root-$failureMode"
        Seed-Legacy $locked $oldLeaf
        $before = Snapshot $locked
        $savedErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $output = & $shell -NoProfile -NonInteractive -File $lockedFileHarness `
                -Helper $helper -Directory $locked -Runtime $RuntimeExecutable -FailureMode $failureMode 2>&1 | Out-String
            $lockedExit = $LASTEXITCODE
        }
        finally { $ErrorActionPreference = $savedErrorActionPreference }
        $compactOutput = Compact-Output $output
        Assert ($lockedExit -eq 1) "Locked-file migration succeeded unexpectedly: $output"
        if ($failureMode -eq 'once') {
            Assert ((Snapshot $locked) -eq $before) 'A deletion failure outside provisioning lost original files'
            Assert ($compactOutput.Contains('restoredoriginalcredentials')) "Rollback was not completed: $output"
        }
        else {
            $backups = @(Get-ChildItem $locked -Directory -Force)
            Assert ($backups.Count -eq 1) 'Failed rollback discarded its backup'
            Assert ((Snapshot $backups[0].FullName) -eq $before) 'Failed rollback backup is incomplete'
            Assert ($compactOutput.Contains('backupretained') -and
                -not $compactOutput.Contains('restoredoriginalcredentials')) 'Failed rollback reported success'
        }
    }
    Write-Host 'PASS: locked files trigger rollback; failed rollback retains the original backup'

    $rootOnly = New-Case 'legacy-root-only'
    Copy-Item (Join-Path $fixtures 'local-runtime-root-ca.crt') $rootOnly
    Invoke-Helper $rootOnly
    Assert ((Read-Mode $rootOnly) -eq 'managed') 'Known root-only installation did not recover'
    $noRoot = New-Case 'legacy-without-root'
    Seed-Legacy $noRoot $oldLeaf
    Remove-Item (Join-Path $noRoot 'local-runtime-root-ca.crt')
    Invoke-Helper $noRoot
    Assert ((Read-Mode $noRoot) -eq 'managed') 'Legacy pair without a root was not migrated'
    Write-Host 'PASS: root-only and leaf-only legacy installations'

    foreach ($withLegacyRoot in @($false, $true)) {
        $custom = New-Case "custom-$withLegacyRoot"
        foreach ($name in @('sdk-runtime.localhost.fullchain.pem', 'sdk-runtime.localhost.key.pem')) {
            Copy-Item (Join-Path $fresh $name) $custom
        }
        if ($withLegacyRoot) { Copy-Item (Join-Path $fixtures 'local-runtime-root-ca.crt') $custom }
        $leafHash = (Get-FileHash (Join-Path $custom 'sdk-runtime.localhost.fullchain.pem')).Hash
        $keyHash = (Get-FileHash (Join-Path $custom 'sdk-runtime.localhost.key.pem')).Hash
        Invoke-Helper $custom
        Assert ((Read-Mode $custom) -eq 'custom') 'Customer pair must remain custom'
        Assert ((Get-FileHash (Join-Path $custom 'sdk-runtime.localhost.fullchain.pem')).Hash -eq $leafHash) 'Customer certificate changed'
        Assert ((Get-FileHash (Join-Path $custom 'sdk-runtime.localhost.key.pem')).Hash -eq $keyHash) 'Customer key changed'
        $before = Snapshot $custom
        Invoke-Helper $custom
        Assert ((Snapshot $custom) -eq $before) 'Repeat provisioning changed custom credentials'
        Invoke-Helper $custom 0 'InstallTlsTrust'
        Assert (-not (Test-Path (Join-Path $custom 'public'))) 'Customer mode exported an unrelated SDK CA'
    }
    $customerKey = New-Case 'legacy-certificate-customer-key'
    Seed-Legacy $customerKey $oldLeaf
    Set-Content (Join-Path $customerKey 'sdk-runtime.localhost.key.pem') 'customer key must not be deleted'
    Invoke-Helper $customerKey
    Assert ((Read-Mode $customerKey) -eq 'custom') 'Certificate-only match overwrote customer key'
    Assert ((Get-Content (Join-Path $customerKey 'sdk-runtime.localhost.key.pem') -Raw).Contains('customer key')) 'Customer key was lost'
    Write-Host 'PASS: custom pairs, custom trust skip and customer replacement key'

    foreach ($missing in @('sdk-runtime.localhost.key.pem', 'sdk-runtime.localhost.fullchain.pem')) {
        $partial = New-Case "partial-$missing"
        Seed-Legacy $partial $oldLeaf
        Remove-Item (Join-Path $partial $missing)
        $before = Snapshot $partial
        Invoke-Helper $partial 1
        Assert ((Snapshot $partial) -eq $before) 'Partial pair was modified before rejection'
    }
    $unknownRoot = New-Case 'unknown-incomplete-root'
    Seed-Legacy $unknownRoot $oldLeaf
    Copy-Item (Join-Path $fresh 'local-runtime-root-ca.crt') (Join-Path $unknownRoot 'local-runtime-root-ca.crt') -Force
    $before = Snapshot $unknownRoot
    Invoke-Helper $unknownRoot 1
    # Runtime may create a lock file while rejecting an incomplete unknown CA.
    Remove-Item (Join-Path $unknownRoot 'tls-state.json.lock') -ErrorAction SilentlyContinue
    Assert ((Snapshot $unknownRoot) -eq $before) 'Unknown CA or its leaf pair was deleted'
    foreach ($missingRoots in @(@('local-runtime-root-ca.crt'), @('local-runtime-root-ca.key.pem'), @('local-runtime-root-ca.crt', 'local-runtime-root-ca.key.pem'))) {
        $broken = New-Case ('broken-managed-' + [Guid]::NewGuid().ToString('N'))
        Get-ChildItem $fresh -File | Copy-Item -Destination $broken
        foreach ($name in $missingRoots) { Remove-Item (Join-Path $broken $name) }
        $before = Snapshot $broken
        Invoke-Helper $broken 1
        Assert ((Snapshot $broken) -eq $before) 'Incomplete managed CA was silently replaced'
    }
    Write-Host 'PASS: incomplete leaf pairs, unknown root and incomplete managed CA are preserved and rejected'

    # Mock only the certificate-store boundary in a separate process. Exercise
    # the real RemoveTrustedRoot action without touching the machine store.
    $storeHarness = Join-Path $workspace 'store-harness.ps1'
    @'
param([string]$Helper, [string]$Directory, [string]$RemovalLog)
$ErrorActionPreference = 'Stop'
function Get-ChildItem {
    param([string]$Path)
    if ($Path -ne 'Cert:\LocalMachine\Root') { throw "Unexpected enumeration: $Path" }
    $pem = Get-Content (Join-Path $Directory 'local-runtime-root-ca.crt') -Raw
    $body = [Regex]::Match($pem, '-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----',
        [Text.RegularExpressions.RegexOptions]::Singleline).Groups[1].Value -replace '\s', ''
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new([Convert]::FromBase64String($body))
    [PSCustomObject]@{Subject='CN=Local Runtime Root CA'; Thumbprint=$certificate.Thumbprint; PSPath='Cert:\installed'}
    [PSCustomObject]@{Subject='CN=Local Runtime Root CA'; Thumbprint='unrelated-root'; PSPath='Cert:\unrelated'}
    $certificate.Dispose()
}
function Remove-Item {
    param([string]$LiteralPath, [switch]$Force)
    if ($LiteralPath -notin @('Cert:\installed', 'Cert:\LocalMachine\Root\4608C5BD34AC2327E1C7A4A6D6D874CC8235B482')) {
        throw "Unrelated root removal attempted: $LiteralPath"
    }
    Add-Content -LiteralPath $RemovalLog -Value $LiteralPath
}
function Test-Path {
    param([string]$LiteralPath, [string]$PathType)
    if ($LiteralPath -like 'Cert:*') {
        return $LiteralPath -eq 'Cert:\LocalMachine\Root\4608C5BD34AC2327E1C7A4A6D6D874CC8235B482'
    }
    return Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
}
& $Helper -Action RemoveTrustedRoot -TlsDirectory $Directory
exit $LASTEXITCODE
'@ | Set-Content -LiteralPath $storeHarness -Encoding UTF8
    $removed = Join-Path $workspace 'removed.txt'
    $output = & $shell -NoProfile -NonInteractive -File $storeHarness -Helper $helper -Directory $fresh -RemovalLog $removed 2>&1 | Out-String
    Assert ($LASTEXITCODE -eq 0) "Uninstall store regression failed: $output"
    Assert (Test-Path $removed) "Installed root removal was skipped: $output"
    $removals = @(Get-Content $removed)
    Assert ($removals.Count -eq 2 -and $removals[1] -eq 'Cert:\installed') 'Exact installed and obsolete roots were not removed'
    Remove-Item $removed
    $output = & $shell -NoProfile -NonInteractive -File $storeHarness -Helper $helper -Directory $custom -RemovalLog $removed 2>&1 | Out-String
    Assert ($LASTEXITCODE -eq 0) "Custom uninstall failed: $output"
    $removals = @(Get-Content $removed)
    Assert ($removals.Count -eq 1 -and $removals[0].EndsWith('4608C5BD34AC2327E1C7A4A6D6D874CC8235B482')) 'Customer uninstall removed a root other than the known obsolete product CA'
    Write-Host 'PASS: uninstall removes exact installed/obsolete product roots and preserves customer trust'
    Write-Host 'Windows TLS provisioning regression checks passed.'
}
finally {
    $env:ProgramData = $savedProgramData
    $env:SystemRoot = $savedSystemRoot
    Remove-Item -LiteralPath $workspace -Recurse -Force -ErrorAction SilentlyContinue
}
