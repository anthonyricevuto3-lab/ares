# ARES product specification

ARES is a desktop simulation of a small flight-software stack. The product is the `ares` and `ares-replay` programs, the named scenarios, and the contracts in `docs/`.

## Problem

Show a bounded flight computer that can detect a closed set of faults, recover in two specific ways, and leave a recording another tool can validate. The audience is an engineer reading the repository, not an operator of a vehicle.

## Target use

Clone the repository, configure with CMake, run a scenario, record it, and replay it. Inject faults through named chaos schedules. Inspect the mode, the selected GPS, and the recovery result in the mission summary.

## Boundaries

In scope: three periodic tasks, a mode machine, sensor freshness, a fault registry, deadline escalation, navigation restart, primary-to-backup GPS failover, deterministic chaos, and an observational flight recorder.

Out of scope: a communications link, GPS failback, checkpoint rollback, voting, a user interface, a database, and any claim of flight certification.

The simulator implements hardware interfaces. It does not write the fault registry or request a mode. The recorder does not either.

## Functional behavior

- Boot `Boot -> Initialization -> Standby`, then accept `StartMission` into `Nominal`.
- Run navigation, health, and communications on joined threads.
- Detect the typed faults in `docs/FAULT_MODEL.md`.
- Restart navigation at most twice per episode, or switch to the backup GPS once, as specified in `docs/RECOVERY.md`.
- Record an optional binary history and reject a malformed file in `ares-replay`.

## Non-functional behavior

- C++20, GCC or Clang, CMake 3.20, Ninja. MSVC is unsupported.
- Flight-side fault, event, chaos, and recorder storage is fixed capacity. See `docs/MEMORY.md`.
- The same manual-clock inputs produce the same flight events and the same recording bytes.
- Project warnings are errors. Sanitizers are available and are not linked into a normal release build.
- Process exit codes are the `ExitCode` enumeration. Success is 0. Recorder failure is 10 and never hides a flight failure.

## Known limitations

Host-clock runs can miss deadlines that a manual-clock test does not. The event log can overwrite older events while the recorder still holds its copy. Scenario names in the file header are at most 15 characters. Replay does not re-execute the mission.
