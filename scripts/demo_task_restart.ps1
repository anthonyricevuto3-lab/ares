$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Ares = if ($env:ARES) { $env:ARES } else { Join-Path $Root "build\release\ares.exe" }
& $Ares --scenario nav_restart --seed 1 --duration-ms 8000
exit $LASTEXITCODE
