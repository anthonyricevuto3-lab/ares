# Requires an MSYS2 UCRT64 environment on PATH (cmake, ninja, g++).
# Fails if gcovr is missing. Does not treat a missing tool as a pass.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
python -m gcovr --version
if ($LASTEXITCODE -ne 0) {
    Write-Error "gcovr is required. Install with: python -m pip install gcovr==8.3"
}
$Build = Join-Path $Root "build\coverage"
cmake -S $Root -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARES_ENABLE_COVERAGE=ON -DARES_ENABLE_SANITIZERS=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $Build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir $Build --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $Build --target ares-coverage
exit $LASTEXITCODE
