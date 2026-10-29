param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $RepoRoot 'src\sdk_open\providers\private\auth\private_sdk_auth_service.cpp'
)
$start = $source.IndexOf('bool EnsureAuthorizedSubsystem(std::string* err)')
$end = $source.IndexOf('bool GetLocalMachineCode(', $start)
if ($start -lt 0 -or $end -lt 0) {
    throw 'Unable to locate EnsureAuthorizedSubsystem.'
}
$functionBody = $source.Substring($start, $end - $start)

if ($functionBody -match '_putenv_s\s*\(\s*"CZUR_AUTH_LICENSE_DIR"' -or
    $functionBody -match 'setenv\s*\(\s*"CZUR_AUTH_LICENSE_DIR"') {
    throw 'EnsureAuthorizedSubsystem must not derive CZUR_AUTH_LICENSE_DIR from the SDK runtime directory.'
}
if ($functionBody -notmatch 'GetSdkRuntimeLogDir\(\)') {
    throw 'EnsureAuthorizedSubsystem must retain SDK runtime log initialization.'
}
if ($functionBody -notmatch 'auth_manager\.Initialize\(') {
    throw 'EnsureAuthorizedSubsystem must initialize AuthManager so it applies explicit overrides or platform defaults.'
}
Write-Host 'SDK private shared authorization-file static checks passed.'
