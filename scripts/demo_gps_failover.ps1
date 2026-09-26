$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Ares = if ($env:ARES) { $env:ARES } else { Join-Path $Root "build\release\ares.exe" }
& $Ares --scenario gps_stale --seed 42 --duration-ms 12000
exit $LASTEXITCODE
