$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Ares = if ($env:ARES) { $env:ARES } else { Join-Path $Root "build\release\ares.exe" }
$Replay = if ($env:REPLAY) { $env:REPLAY } else { Join-Path $Root "build\release\ares-replay.exe" }
$Record = if ($env:RECORD) { $env:RECORD } else { Join-Path $Root "build\demo-gps.bin" }
& $Ares --scenario gps_stale --seed 42 --duration-ms 12000 --record $Record
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Replay $Record
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Replay --verify $Record
exit $LASTEXITCODE
