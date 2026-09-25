# ARES 1.0.0

ARES 1.0.0 is the demonstration release of the flight software built in v0.1 through v0.7. It adds no spacecraft subsystem and does not change the recording format. Format 1.0 is unchanged. New recordings store application version 1.0.0 in the existing header field.

## Overview

`ares` boots a mode machine, runs three periodic tasks against a simulated spacecraft, detects a closed set of faults, and can recover in two ways: restart navigation on its existing thread, or fail over from primary GPS to backup GPS. `ares-replay` checks the optional binary recording and prints the timeline in file order.

## Core architecture

The simulator implements hardware interfaces. Flight software does not include the simulator. The health task is the only writer of the fault registry and of recovery progress. The mode machine is the only mode authority. Workers are joined `std::jthread`s. Nothing is detached.

## Fault management

Typed faults live in a 19-slot registry. Deadline misses escalate from Advisory to Warning to Critical. Three Warning detections can enter Degraded. A Critical fault can enter SafeMode. Three healthy evaluations can return SafeMode to Standby. The deadline-miss log holds 32 entries. An overwrite is exit 9 when no worker fault outranks it.

## Autonomous recovery

Navigation restart is at most two attempts in one episode. Success is three consecutive on-time completions from the new generation. GPS failover is one attempt. Success is three usable backup samples after the switch. There is no automatic return to primary. A failed GPS recovery stays failed for that mission.

## Chaos testing

Named schedules inject sensor and deadline conditions. They do not write faults or request modes. `nav_restart` and `restart_fail` are demonstration names for schedules that already existed in the campaign tests.

## Flight recording and replay

The recorder is an observer with a 256-record prefix. Sequence is the total order. Logical timestamps may decrease across threads. Replay rejects a structurally invalid file and does not re-execute the mission.

## Release engineering

CMake presets are `debug`, `release`, and `asan-ubsan`. GitHub Actions builds Linux Debug, Linux Release, Linux ASan/UBSan, and Windows UCRT64 Debug. `cmake --install` and CPack produce `ares`, `ares-replay`, and the design documents. `scripts/demo_v1.sh` runs the four canonical demonstrations.

## Known limitations

This is not flight-certified software. Host-clock runs are not byte-identical and can add deadline misses. A long `restart_fail` run can exit 9 after recovery has already failed, because the miss log is bounded. Communications dropout, automatic GPS failback, checkpoint rollback, and hardware-in-the-loop are not implemented. ThreadSanitizer is not part of the supported validation and is not reported as passed. MSVC is unsupported.
