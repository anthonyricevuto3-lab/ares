# Bounded recovery-failure demonstration. The wrapper exit means the
# demonstration proved RecoveryFailed, not the raw ares process code.
# Set ARES to an installed binary if it is not under build/release.
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($env:ARES) { $Ares = $env:ARES } else { $Ares = Join-Path $Root "build\release\ares.exe" }

function Fail([string]$Message) {
    Write-Error "demo failed: $Message"
    exit 1
}

$failureText = @(& $Ares --scenario restart_fail --seed 1 --duration-ms 6000 2>&1 | ForEach-Object { "$_".TrimEnd("`r") })
$failure = $LASTEXITCODE
$failureText | Write-Output
$failureJoined = $failureText -join "`n"
if ($failureJoined -notmatch '(?m)^recovery_failures: 1$') {
    Fail "restart_fail did not report recovery_failures: 1"
}
if ($failure -eq 0) {
    Write-Output "Observed process exit Success (0)."
    Write-Output "RecoveryFailed does not change the process exit. The summary above is the recovery result."
} elseif ($failure -eq 9) {
    Write-Output "Observed FaultHistoryOverflow (9)."
    Write-Output "Recovery had already failed after two attempts. Continued misses then filled the 32-entry history."
} else {
    Fail "restart_fail exit $failure"
}
exit 0
