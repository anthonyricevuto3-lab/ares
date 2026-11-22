# Fails if lizard is missing. A function above the voluntary threshold is
# reported; it is not by itself a failed measurement.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
python -c "import lizard"
if ($LASTEXITCODE -ne 0) {
    Write-Error "lizard is required. Install with: python -m pip install lizard==1.17.10"
}
$Build = Join-Path $Root "build\debug"
if (-not (Test-Path $Build)) {
    Write-Error "Configure build/debug first so the ares-complexity target exists."
}
cmake --build $Build --target ares-complexity
exit $LASTEXITCODE
