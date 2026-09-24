# ARES

ARES (Autonomous Resilient Embedded Spacecraft System) is a C++20 flight-software simulation. Version 0.1 boots a mode state machine, runs three periodic tasks, and shuts them down by joining their threads.

This is not certified real-time flight software. Deadline checks on the operating-system clock describe desktop scheduling behavior.

## Build

The supported local toolchain is MSYS2 UCRT64. Put its binaries on `PATH`, then configure with Ninja:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
.\build\debug\ares.exe --duration-ms 250
```

`--duration-ms` defaults to 250. `--help` prints usage.

Debug sanitizers (ASan and UBSan, or MSVC ASan) are a separate preset. GitHub Actions runs that preset with Clang on Ubuntu. MSYS2 UCRT64 GCC does not ship `libasan` or `libubsan`, so the preset stops at configure time on that toolchain.

```powershell
cmake --preset debug-sanitizers
cmake --build --preset debug-sanitizers
ctest --preset debug-sanitizers
```

Configuration downloads pinned GoogleTest 1.15.2.

## Layout

`src/core` is infrastructure: clock, log, events, periodic tasks, and thread lifetime. `src/flight` is the mode machine and the three example tasks. The executable composes them. Flight code does not depend on a simulator.

## Checks

```powershell
clang-format --dry-run --Werror (Get-ChildItem -Recurse include,src,tests -Include *.hpp,*.cpp)
clang-tidy -p build/debug --warnings-as-errors=* (Get-ChildItem src -Recurse -Filter *.cpp)
```

GitHub Actions runs the Debug sanitizer build, tests, `clang-format`, and `clang-tidy` on Ubuntu.
