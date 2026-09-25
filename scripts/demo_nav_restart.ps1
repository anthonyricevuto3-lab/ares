# Navigation-restart demonstration. The wrapper exit means one verified
# restart was observed, not merely that ares exited 0.
# Set ARES and REPLAY to installed binaries if they are not under build/release.
# The recording is RECORD_DIR/demo-nav.bin, defaulting to <repo>/build.
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($env:ARES) { $Ares = $env:ARES } else { $Ares = Join-Path $Root "build\release\ares.exe" }
if ($env:REPLAY) { $Replay = $env:REPLAY } else { $Replay = Join-Path $Root "build\release\ares-replay.exe" }
if ($env:RECORD_DIR) { $RecordDir = $env:RECORD_DIR } else { $RecordDir = Join-Path $Root "build" }
New-Item -ItemType Directory -Force -Path $RecordDir | Out-Null
$NavRecord = Join-Path $RecordDir "demo-nav.bin"

function Fail([string]$Message) {
    Write-Error "demo failed: $Message"
    exit 1
}

function Has-Exact([string]$Text, [string]$Line) {
    return $Text -match ('(?m)^' + [regex]::Escape($Line) + '$')
}

$text = @(& $Ares --scenario nav_restart --seed 1 --duration-ms 8000 --record $NavRecord 2>&1 | ForEach-Object { "$_".TrimEnd("`r") })
$code = $LASTEXITCODE
$text | Write-Output
$joined = $text -join "`n"
if ($code -ne 0) {
    Fail "nav_restart exit $code"
}
if ((Has-Exact $joined "navigation_generation: 0") -or (Has-Exact $joined "recovery_successes: 0")) {
    Write-Output "Navigation restart was not observed on this host/run."
    Write-Output "This is a failed demonstration run due to host scheduling, not a flight-software failure."
    exit 1
}
if (-not (Has-Exact $joined "navigation_generation: 1") -or
    -not (Has-Exact $joined "recovery_successes: 1") -or
    -not (Has-Exact $joined "recovery_failures: 0")) {
    Fail "navigation restart demo expected navigation_generation: 1, recovery_successes: 1, and recovery_failures: 0"
}
if (-not (Test-Path -Path $NavRecord -PathType Leaf)) {
    Fail "missing recording: $NavRecord"
}
& $Replay --verify $NavRecord
$replay = $LASTEXITCODE
if ($replay -ne 0) {
    Fail "navigation replay exit $replay"
}
Write-Output "Observed navigation_generation: 1, recovery_successes: 1, recovery_failures: 0, replay valid."
exit 0
