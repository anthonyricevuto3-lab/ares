# ARES C++20 coding standard

This standard records the rules ARES already follows and the safety-oriented rules that apply to later changes. It is the project's response to the intent of NPR 7150.2D SWE-061 (select, define, and adhere to coding methods, standards, and criteria). It is not a NASA Center coding standard.

## Language and toolchain

- C++20 (`CMAKE_CXX_STANDARD 20`, extensions off).
- GCC or Clang with `__int128`. MSVC and clang-cl are rejected at configure time because spacecraft integration uses `__int128`.
- Supported hosts used for this baseline: MSYS2 UCRT64 GCC, and Clang on Linux.
- ARES targets compile with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor` and `-Werror`.
- Release builds add `-fno-fast-math`. Flight checks are explicit. They are not `assert()`, so `NDEBUG` does not remove them.
- Layout is LLVM style, column 100, enforced by `.clang-format`. clang-tidy (`.clang-tidy`, `WarningsAsErrors *`, header filter `ares`) is the static check. GoogleTest is a system include and is not compiled with `-Werror`.

## Ownership and concurrency

- Own resources with RAII. No raw owning pointers. No detached threads.
- `TaskSupervisor` owns worker threads and joins them.
- One writer for fault and recovery state. Other tasks publish through `FaultMailbox` or an atomic generation handoff.
- Document any single-reader assumption next to the data. Simulated sensors are read by the health or navigation task that owns that sample.
- Exceptions do not escape a worker. The supervisor catches them and records `WorkerException`.

## Arithmetic, bounds, and storage

- Persistent and protocol fields use explicit-width integers (`std::uint16_t`, `std::uint32_t`, and the same family).
- Time and spacecraft integration use checked arithmetic. Unrepresentable time is an error. Integration saturates.
- Do not rely on signed overflow. It is undefined behavior.
- Flight-state containers are bounded. Do not grow them on the periodic path.
- Avoid allocating on the periodic flight path. The logger and offline replay are the documented exceptions.
- Validate indexes. Do not subscript a `std::span`, a C array, or a `string_view` in a way clang-tidy rejects for this tree. Prefer iterators or a checked index.
- Binary files are written field by field. Do not dump a C++ struct as the file format. Parse with explicit lengths and a CRC.

## Flight boundaries

- Flight code does not depend on a concrete simulated device.
- Chaos does not write FDIR, recovery, or mode state.
- The recorder and other observers do not command mode or clear faults.
- Deterministic simulation draws come from the seeded SplitMix64 source in `random_source.hpp`. Do not call `std::random_device` on that path.
- Fault and recovery keys in flight state are enumerations, not strings.
- Handle enumerations exhaustively.

## Tests and logging

- Logging is diagnostic. It is not the authoritative fault or mode state.
- Tests that need a timeline use `ManualClock`. Do not use `sleep` as the correctness mechanism when the simulated clock can advance the same condition.
- A seeded campaign must pass the seed in from the test. Do not draw an unspecified seed.

## Intentional exceptions

- `std::string` allocation in the logger, one line at a time.
- Allocation inside offline replay and inside `snapshot()` at shutdown.
- GoogleTest and third-party headers are not held to the ARES warning set.
- Host `SteadyClock` demos are allowed to be timing-variable. They are not the determinism oracle.
