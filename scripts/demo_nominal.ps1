$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Ares = if ($env:ARES) { $env:ARES } else { Join-Path $Root "build\release\ares.exe" }
& $Ares --scenario nominal --seed 1 --duration-ms 1000
exit $LASTEXITCODE
