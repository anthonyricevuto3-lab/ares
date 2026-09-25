# Developing ARES

## Tools

- CMake 3.20 or newer
- Ninja
- GCC or Clang with C++20 and `__int128`
- GoogleTest 1.15.2, downloaded by CMake from a pinned URL and hash

Windows builds use an MSYS2 UCRT64 shell (`echo $MSYSTEM` prints `UCRT64`). Put `C:\msys64\ucrt64\bin` on `PATH` if you configure from another shell. MSVC is not supported.

Linux CI uses Clang. A local GCC build is also valid.

## Configure, build, test

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Release, without sanitizers:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

AddressSanitizer and UndefinedBehaviorSanitizer, Debug only:

```sh
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan --output-on-failure
```

MSYS2 UCRT64 GCC does not ship `libasan` or `libubsan`. Use Clang on Linux for that preset. Sanitizer flags are not part of the `debug` or `release` presets.

Useful slices:

```sh
ctest --test-dir build/debug -L unit --output-on-failure
ctest --test-dir build/debug -L integration --output-on-failure
ctest --test-dir build/debug -L recorder --output-on-failure
ctest --test-dir build/debug -R 'FlightRecorder|Replay|Recording' --output-on-failure
```

Repeat a filter by running `ctest` again. The suite does not sleep for correctness. A hung test stops at the 60-second CTest timeout.

## Format and tidy

These targets do not run as part of `cmake --build` unless you name them. The check target does not modify files.

```sh
cmake --build build/debug --target ares-format-check
cmake --build build/debug --target ares-format
cmake --build build/debug --target ares-tidy
```

The same check without CMake:

```sh
clang-format --dry-run --Werror $(find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' \))
clang-tidy -p build/debug --warnings-as-errors='*' $(find src -name '*.cpp')
```

Run tidy against a non-sanitizer build so `compile_commands.json` does not carry sanitizer flags. `.clang-tidy` treats project warnings as errors. Third-party headers are outside the header filter.

## Install and package

```sh
cmake --install build/release --prefix "$PWD/build/install"
cpack --config build/release/CPackConfig.cmake -G TGZ
```

Windows release binaries need the UCRT64 runtime on `PATH`: `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, and `libwinpthread-1.dll`. The package does not bundle those DLLs.

## What not to add casually

Communications simulation, new recovery actions, GPS failback, checkpoint rollback, and a user interface are out of scope until a milestone says otherwise. The frozen behavior in v0.1 through v0.6 stays unless a change is an intentional, tested contract update.
