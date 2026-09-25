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
    Write-Output "ARES v1.0 — $Title"
    Write-Output "=================================================="
}

function Fail([string]$Message) {
    Write-Error "demo failed: $Message"
    exit 1
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
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "scripts\demo_nav_restart.ps1")
$nav = $LASTEXITCODE
if ($nav -ne 0) { Fail "navigation restart demonstration exit $nav" }

Section "Bounded Recovery Failure"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "scripts\demo_recovery_failure.ps1")
$failure = $LASTEXITCODE
if ($failure -ne 0) { Fail "recovery-failure demonstration exit $failure" }

Write-Output ""
Write-Output "=================================================="
Write-Output "ARES v1.0 — Expected versus observed"
Write-Output "=================================================="
Write-Output "nominal exit:            expected 0, observed $nominal"
Write-Output "gps exit:                expected 0, observed $gps"
Write-Output "gps replay verify:       expected 0, observed $gpsReplay"
Write-Output "navigation restart demo: expected 0, observed $nav"
Write-Output "recovery-failure demo:   expected 0, observed $failure"
Write-Output "recordings: $GpsRecord and $NavRecord"
exit 0
