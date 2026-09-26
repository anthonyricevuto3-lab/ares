# Demonstrating ARES

Build the release preset first. The scripts look for `build/release/ares` or `build/release/ares.exe`.

```sh
cmake --preset release
cmake --build --preset release
```

The `ares` process uses the host steady clock. The schedules below are deterministic in ARES time, but a loaded machine can add extra deadline misses. The campaign tests under `ctest -L integration` are the manual-clock oracle and do not depend on wall time.

Each demo prints a summary with no timestamps, process ids, or addresses. Look at `final_mode`, `active_gps`, `navigation_generation`, `recovery_successes`, `recovery_failures`, and `exit`.

## Demo 1 — Nominal

```sh
./build/release/ares --scenario nominal --seed 1 --duration-ms 1000
```

Or `scripts/demo_nominal.sh` / `scripts/demo_nominal.ps1`.

Look for `scenario: nominal`, `final_mode: Nominal`, `active_gps: primary`, `navigation_generation: 0`, `recording: disabled`, and `exit: Success`. Boot lines show `Boot -> Initialization -> Standby` and then `Standby -> Nominal`.

## Demo 2 — GPS failover

```sh
./build/release/ares --scenario gps_stale --seed 42 --duration-ms 12000
```

The primary GPS freezes at 5 seconds for 4 seconds. Health should enter Degraded, select the backup, and verify it.

Look for `active_gps: backup`, `recovery_successes: 1` or more, and `exit: Success`. The mode log contains `Nominal -> Degraded` and a later return toward Nominal once the primary warning is isolated and verified.

## Demo 3 — Navigation restart

```sh
./build/release/ares --scenario nav_restart --seed 1 --duration-ms 8000
```

A 150 ms navigation delay lasts 1 second. The deadline escalates, the task restarts on the same worker, and three on-time completions verify the new generation.

Look for `navigation_generation: 1` or greater, `recovery_successes` at least 1, `final_mode: Standby`, and `exit: Success`.

## Demo 4 — Recovery failure

```sh
./build/release/ares --scenario restart_fail --seed 1 --duration-ms 20000
```

The same delay is held for 30 seconds, so both restart attempts fail before the injection ends.

Look for `recovery_failures` at least 1, `navigation_generation: 2`, and `final_mode: SafeMode`. Recovery failure does not by itself change the process exit. On the host clock this run also fills the 32-entry deadline-miss log, so the process can exit `FaultHistoryOverflow` (9). That code is the miss history, and the summary still shows the failed recovery.

## Record and replay

```sh
./build/release/ares --scenario gps_stale --seed 42 --duration-ms 12000 --record build/demo-gps.bin
./build/release/ares-replay build/demo-gps.bin
./build/release/ares-replay --verify build/demo-gps.bin
```

`ares-replay` should exit 0 and print `valid` for `--verify`. The timeline is in file order. Expect a mission start, a chaos edge, a fault activation, `RecoveryStarted`, a backup selection, `RecoverySucceeded`, and a mission end. Two host-clock recordings of this demo are not required to be byte-identical. A manual-clock campaign of `gps_stale` is.

`recording: written` in the mission summary means the file was committed. `exit: RecorderFailed` means the flight result was success and the recording operation was not. A flight failure is never replaced by that code.

## Scripts

| Script | Scenario | Duration |
| --- | --- | --- |
| `scripts/demo_nominal.sh` | nominal | 1 s |
| `scripts/demo_gps_failover.sh` | gps_stale | 12 s |
| `scripts/demo_task_restart.sh` | nav_restart | 8 s |
| `scripts/demo_recovery_failure.sh` | restart_fail | 20 s |
| `scripts/demo_record_replay.sh` | gps_stale, then replay | 12 s |

PowerShell copies use the `.ps1` suffix and the same arguments.
