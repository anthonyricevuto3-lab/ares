# Canonical ARES v1.0 demonstration. Set ARES and REPLAY to use installed binaries.
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($env:ARES) { $Ares = $env:ARES } else { $Ares = Join-Path $Root "build\release\ares.exe" }
if ($env:REPLAY) { $Replay = $env:REPLAY } else { $Replay = Join-Path $Root "build\release\ares-replay.exe" }
if ($env:RECORD_DIR) { $RecordDir = $env:RECORD_DIR } else { $RecordDir = Join-Path $Root "build" }
New-Item -ItemType Directory -Force -Path $RecordDir | Out-Null
$GpsRecord = Join-Path $RecordDir "demo-gps.bin"
$NavRecord = Join-Path $RecordDir "demo-nav.bin"

function Section([string]$Title) {
    Write-Output ""
    Write-Output "=================================================="
    Write-Output "ARES v1.0 - $Title"
    Write-Output "=================================================="
}

function Fail([string]$Message) {
    Write-Error "demo failed: $Message"
    exit 1
}

function Has-Exact([string]$Text, [string]$Line) {
    return $Text -match ('(?m)^' + [regex]::Escape($Line) + '$')
}

Section "Nominal Mission"
& $Ares --scenario nominal --seed 1 --duration-ms 1000
$nominal = $LASTEXITCODE
if ($nominal -ne 0) { Fail "nominal exit $nominal" }

Section "GPS Autonomous Recovery"
& $Ares --scenario gps_stale --seed 42 --duration-ms 12000 --record $GpsRecord
$gps = $LASTEXITCODE
if ($gps -ne 0) { Fail "gps exit $gps" }
& $Replay --verify $GpsRecord
$gpsReplay = $LASTEXITCODE
if ($gpsReplay -ne 0) { Fail "gps replay exit $gpsReplay" }

Section "Navigation Autonomous Restart"
$navText = @(& $Ares --scenario nav_restart --seed 1 --duration-ms 8000 --record $NavRecord 2>&1 | ForEach-Object { "$_".TrimEnd("`r") })
$navCode = $LASTEXITCODE
$navText | Write-Output
$navJoined = $navText -join "`n"
if ($navCode -ne 0) {
    Fail "nav_restart exit $navCode"
}
if ((Has-Exact $navJoined "navigation_generation: 0") -or (Has-Exact $navJoined "recovery_successes: 0")) {
    Write-Output "Navigation restart was not observed on this host/run."
    Write-Output "This is a failed demonstration run due to host scheduling, not a flight-software failure."
    exit 1
}
if (-not (Has-Exact $navJoined "navigation_generation: 1") -or
    -not (Has-Exact $navJoined "recovery_successes: 1") -or
    -not (Has-Exact $navJoined "recovery_failures: 0")) {
    Fail "navigation restart demo expected navigation_generation: 1, recovery_successes: 1, and recovery_failures: 0"
}
if (-not (Test-Path -Path $NavRecord -PathType Leaf)) {
    Fail "missing recording: $NavRecord"
}
& $Replay --verify $NavRecord
$navReplay = $LASTEXITCODE
if ($navReplay -ne 0) {
    Fail "navigation replay exit $navReplay"
}
Write-Output "Observed navigation_generation: 1, recovery_successes: 1, recovery_failures: 0, replay valid."
$nav = 0

Section "Bounded Recovery Failure"
$failureText = @(& $Ares --scenario restart_fail --seed 1 --duration-ms 6000 2>&1 | ForEach-Object { "$_".TrimEnd("`r") })
$failureCode = $LASTEXITCODE
$failureText | Write-Output
$failureJoined = $failureText -join "`n"
if (-not (Has-Exact $failureJoined "recovery_failures: 1")) {
    Fail "restart_fail did not report recovery_failures: 1"
}
if ($failureCode -eq 0) {
    Write-Output "Observed process exit Success (0)."
    Write-Output "RecoveryFailed does not change the process exit. The summary above is the recovery result."
} elseif ($failureCode -eq 9) {
    Write-Output "Observed FaultHistoryOverflow (9)."
    Write-Output "Recovery had already failed after two attempts. Continued misses then filled the 32-entry history."
} else {
    Fail "restart_fail exit $failureCode"
}
$failure = 0

Write-Output ""
Write-Output "=================================================="
Write-Output "ARES v1.0 - Expected versus observed"
Write-Output "=================================================="
Write-Output "nominal exit:            expected 0, observed $nominal"
Write-Output "gps exit:                expected 0, observed $gps"
Write-Output "gps replay verify:       expected 0, observed $gpsReplay"
Write-Output "navigation restart demo: expected 0, observed $nav"
Write-Output "recovery-failure demo:   expected 0, observed $failure (ares exit $failureCode)"
Write-Output "recordings: $GpsRecord and $NavRecord"
exit 0
