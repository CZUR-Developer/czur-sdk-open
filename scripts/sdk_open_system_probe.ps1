$ErrorActionPreference = 'Continue'

$ProbeOutput = 'C:\Windows\Temp\sdk-open-system-probe.txt'
$TargetRepo = 'git@codeup.aliyun.com:694cdba2d8b34f3292b49f0b/capeditor_thridparty.git'
$TargetCommit = 'c7a49f30b2c998f65ef60266f8c25330a6da7c03'

Remove-Item -LiteralPath $ProbeOutput -Force -ErrorAction SilentlyContinue

function Write-ProbeLine {
    param([string]$Message)

    $Message | Add-Content -LiteralPath $ProbeOutput -Encoding UTF8
}

Write-ProbeLine "timestamp=$(Get-Date -Format o)"
Write-ProbeLine "whoami=$(whoami)"
Write-ProbeLine "USERPROFILE=$env:USERPROFILE"
Write-ProbeLine "SSH_DIR_EXISTS=$(Test-Path -LiteralPath (Join-Path $env:USERPROFILE '.ssh'))"
Write-ProbeLine "PRIVATE_KEY_EXISTS=$(Test-Path -LiteralPath (Join-Path (Join-Path $env:USERPROFILE '.ssh') 'id_rsa'))"
Write-ProbeLine "PUBLIC_KEY_EXISTS=$(Test-Path -LiteralPath (Join-Path (Join-Path $env:USERPROFILE '.ssh') 'id_rsa.pub'))"
Write-ProbeLine "GIT_PATH=$((Get-Command git -ErrorAction SilentlyContinue).Source)"
Write-ProbeLine "SSH_PATH=$((Get-Command ssh -ErrorAction SilentlyContinue).Source)"

Write-ProbeLine '--- ssh -T codeup.aliyun.com ---'
$sshOutput = & ssh -o BatchMode=yes -o StrictHostKeyChecking=no -T git@codeup.aliyun.com 2>&1
$sshExitCode = $LASTEXITCODE
foreach ($line in $sshOutput) {
    Write-ProbeLine ([string]$line)
}
Write-ProbeLine "ssh_exit_code=$sshExitCode"

Write-ProbeLine '--- git ls-remote target commit ---'
$remoteOutput = & git ls-remote $TargetRepo $TargetCommit 2>&1
$remoteExitCode = $LASTEXITCODE
foreach ($line in $remoteOutput) {
    Write-ProbeLine ([string]$line)
}
Write-ProbeLine "ls_remote_exit_code=$remoteExitCode"

Write-Host "Probe completed: $ProbeOutput"
Get-Content -LiteralPath $ProbeOutput
