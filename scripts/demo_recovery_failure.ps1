$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Ares = if ($env:ARES) { $env:ARES } else { Join-Path $Root "build\release\ares.exe" }
& $Ares --scenario restart_fail --seed 1 --duration-ms 20000
exit $LASTEXITCODE
