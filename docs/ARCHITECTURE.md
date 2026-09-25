# ARES Architecture

ARES 1.0 is a workstation spacecraft-software simulator. This document describes the system as it exists. Release history is in `docs/RELEASE_NOTES.md` and in the Git tags.

## System overview

`ares` is the composition root. It boots a mission, starts three periodic workers, and shuts them down. `ares-replay` reads a recording and does not start a mission.

```
ares        -> ares_app -> ares_flight -> ares_hardware -> ares_core
ares        -> ares_app -> ares_simulation -> ares_hardware -> ares_core
ares        -> ares_app -> ares_recorder
ares-replay -> ares_recorder
tests       -> the same libraries
```

`ares_core` does not include flight headers. There is no global logger, clock, or mode object. Dependencies are constructor arguments.

The long-form behavior notes are `docs/FAULT_MODEL.md`, `docs/RECOVERY.md`, `docs/CHAOS_ENGINE.md`, `docs/FLIGHT_RECORDER.md`, `docs/REPLAY.md`, and `docs/MEMORY.md`.

## Architectural boundaries

Simulation changes what a device returns or how long a task appears to have run. Flight code depends on hardware interfaces (`Gps`, `Imu`, `Power`, `Thermal`) and on the task supervisor. It does not include the concrete simulated devices.

`ChaosEngine` may freeze a sensor sample, change a reported sensor status, override a battery reading, or delay task execution. It does not call `FaultRegistry`, `RecoveryManager`, or `ModeMachine`.

The health task, through `FdirController`, is the only writer of fault and recovery state. `RecoveryManager` does not call `ModeMachine`. `FdirController::apply` calls `FlightExecutive::request_mode`.

`EventLog` and `FlightRecorder` observe. A recorder failure may change the process exit only when the flight result is Success (`RecorderFailed`). It does not change mode or fault policy. Replay checks the file. It does not re-execute flight software.

## Time and scheduling

Durations are `std::chrono::nanoseconds` and have no epoch. `SteadyClock::time_point` is tagged `SteadyEpoch` and follows `std::chrono::steady_clock`. `ManualClock::time_point` is tagged `ManualEpoch` and starts at zero. The two time-point types do not convert or compare. Flight logic does not read wall-clock time.

`advance()` rejects a negative step and a step that would overflow. `SteadyClock::now` and `wait_until` convert to and from the host steady-clock duration only after a range check. A value that does not fit returns `ClockStatus::Unrepresentable`. When the host tick is coarser than one nanosecond, a successful wait is rounded up by at most one host tick.

A deadline is release-relative. Completion later than `scheduled + deadline` is a miss. Finishing exactly on the deadline is on time. Completion before the scheduled release is `Unusable`. A late wake is a miss. After a cycle, the next release is the period boundary completion landed on, or the next boundary after completion when completion falls strictly between boundaries. A release strictly before completion is skipped so a late task does not burst.

`poll()` stores the deadline record and the next release before hooks run. A throwing body consumes that release and is rethrown. A throwing hook returns `HookError` and does not rerun the body. A nested `poll()` returns `Reentrant`. A second `arm()` leaves the release unchanged.

## Task and thread model

`PeriodicTask::poll()` is synchronous and is what tests call. It passes `std::stop_token` into the work function. The miss hook runs before the cycle hook.

`TaskSupervisor` keeps a fixed table of `std::jthread` workers. Threads are not detached. They block on a start gate until every launch has succeeded. If startup fails, the gate is cancelled, launched threads are joined, and no task body has run. A worker exception is caught at the thread boundary, stored as `WorkerFault` without allocating, and requests stop on the whole supervisor. Hook failures are `WorkerFault::Hook`.

`shutdown()` requests stop, waits up to 50 ms of host time for every launched worker to reach `Exited`, records who is still in any other phase, then joins. It does not detach or kill a thread. A worker that never returns still blocks that join.

`MissionRuntime` in `src/application.cpp` owns the steady clock, logger, event logs, flight tasks, and supervisor, in that order. The supervisor is destroyed first, so workers are joined before the clock or tasks are destroyed.

The three flight tasks are `NavigationCadence`, `HealthPulse`, and `CommBeacon`. The executable uses 100 ms, 200 ms, and 400 ms, with each deadline equal to its period. Navigation reads the IMU and both GPS devices. Health reads battery and temperature and is the only FDIR writer. The communications task logs a cycle. It has no simulated link.

## Hardware abstraction

Hardware contracts live in `include/ares/hardware` and depend only on core time types. Flight code holds those interfaces. `ares_simulation` implements them and does not include flight policy. Replacing a simulated device with another implementation of the same interface does not change navigation, because navigation never names the simulated type.

## Spacecraft simulation

`SpacecraftModel` owns the truth state and stamps samples from the injected clock. Position and attitude integrate at a constant rate from an epoch. Displacement is `rate * elapsed_nanoseconds / 1000000000`, truncated toward zero. The product is formed in a 128-bit integer. The `int64` result saturates only when that quotient does not fit. If the clock is earlier than the epoch, `state_now` returns no value and the simulated sensors report `Unavailable`.

## Navigation and sensor freshness

A sensor reports `SensorStatus`: `Valid`, `Invalid`, `Stale`, or `Unavailable`. It does not decide that a sample is stale because of its age. Flight software does that with `evaluate_freshness`.

| Result | Meaning |
| --- | --- |
| Usable | Reported `Valid`, and the age is representable and not greater than the limit |
| Stale | Reported `Stale` and the age fits, or reported `Valid` and the age fits and is greater than the limit |
| Invalid | Reported `Invalid`, the timestamp is not in the future, and the age fits |
| Unavailable | Reported `Unavailable`, the timestamp is not in the future, and the age fits |
| Future | Sample timestamp is later than the current ARES time |
| TimeError | The clock sample is unusable, the age does not fit, or the limit is negative |

Age equal to the limit is still `Usable`. Time is checked before the reported status. `ares::run` passes the named limits from `sample_limits.hpp`: navigation IMU 200 ms, navigation GPS 1 s, power and thermal 400 ms. A solution is accepted only when `usability` is `Usable`.

`NavigationCadence::solution()` is the latest combination, including a result that is not usable. `last_usable()` changes only when that combination is `Usable`. Power and thermal keep the same split. None of those consumers change spacecraft mode by themselves.

## Fault detection and FDIR

Detection answers what is wrong. Policy answers what to do. `FaultRegistry` stores records and does not choose a mode. Sensor classes, `SpacecraftModel`, and the scheduler do not contain policy. The rules are in `docs/FAULT_MODEL.md`.

Navigation publishes IMU and GPS usability into `FaultMailbox`. Each task's cycle hook publishes that task's deadline result into the same mailbox. The health task publishes battery and temperature, consumes the mailbox, updates the registry, asks `RecoveryManager` what to do, and may call `FlightExecutive::request_mode`. The mailbox is the only lock on that handoff. It does not guard the registry.

An IMU, GPS, or temperature slot keeps the newest usability and whether any `Usable` sample arrived since the last consume. It is not a queue. Battery is published once in the health cycle that consumes it. A deadline miss stays set until that consume, so an on-time publish cannot erase a miss the health task has not taken yet.

The cycle hook runs after the task body. The health task's own deadline observation is consumed on the next health cycle. If the health body has already observed stop, it does not consume the mailbox and it does not call policy. After `start()`, the main thread does not call `request_mode`.

## Mode management

The machine starts in `Boot`. Legal edges:

| From | Legal destinations |
| --- | --- |
| Boot | Initialization |
| Initialization | Standby |
| Standby | Nominal |
| Nominal | Standby, Degraded, SafeMode, Emergency |
| Degraded | Nominal, SafeMode, Emergency |
| SafeMode | Standby, Emergency |
| Emergency | SafeMode |

`StartMission` is accepted only from `Standby`. Rejected transitions leave the mode unchanged and emit `ModeTransitionRejected` with reason `Illegal`, `InvalidMode`, or `Reentrant`. They do not emit `ModeChangedEvent`. `transition()` commits the new mode before it notifies. A callback that calls `transition()` again is rejected. If the change handler throws, the committed mode remains and the reentrancy guard is cleared.

Policy requests only edges the table already allows: `Nominal -> Degraded`, `Degraded -> Nominal`, `Nominal -> SafeMode`, `Degraded -> SafeMode`, and, after three consecutive healthy evaluations, `SafeMode -> Standby`. It does not request `Emergency` or `SafeMode -> Nominal`. A requested mode is not proof the spacecraft is in that mode. The registry is not rewritten from the transition status. A logging failure does not undo a committed transition.

## Autonomous recovery

The health task owns recovery. The manager does not write the registry and does not request a mode. Two actions exist: restart navigation, and switch from primary GPS to backup GPS. The rules are in `docs/RECOVERY.md`.

Navigation restart is a handoff on the existing worker. Generation N's `poll` returns before generation N+1 is armed. The generation counter does not wrap. Two restarts may be executed in one episode. Success is three consecutive on-time completions from that generation, not the handoff itself. While that verification is open, or after it has failed, `SafeMode` does not return to `Standby` and `Degraded` does not return to `Nominal`.

`RecoveryFailed` is terminal for that episode. It does not by itself change the process exit. A later miss-history overflow can still exit `FaultHistoryOverflow`.

## GPS redundancy

Primary and backup GPS are separate simulated devices and separate noise streams. Navigation reads each once per cycle and puts only the selected sample in the solution. A persistent primary sensor warning switches to backup and isolates primary without clearing the primary fault. Three usable backup samples from after that switch complete that recovery. Samples from before the switch do not count. There is no automatic failback. A failed backup verification is one terminal GPS failure.

## Chaos injection

`ChaosEngine` owns a fixed schedule of at most 16 events. After `load`, the schedule is immutable. Device reads are queries of simulation time. `note()` logs injection edges and is called by the health task. It does not call FDIR or the mode machine.

Events are active while `start <= elapsed < start + duration`, measured from the scenario epoch. The same scenario, seed, and clock sequence produce the same injection view. There is no `std::random_device` and no sleep in the scenario clock. Overlapping events on the same target are rejected at load. A failed load leaves the previous schedule unchanged and does not start workers.

`ares --scenario NAME` selects a named schedule before workers start. The default is `nominal`. An unknown name exits with a usage error and does not start tasks. The schedules are in `docs/CHAOS_ENGINE.md`.

## Flight recording

`FlightRecorder` copies already-produced mode, fault, recovery, chaos, deadline, task-generation, and GPS-selection edges into a fixed prefix buffer. It does not write `FaultRegistry`, `RecoveryManager`, the mode machine, the GPS selector, the task supervisor, or `ChaosEngine`. A full buffer or a failed disk write does not change those authorities.

`EventLog` remains the bounded in-memory operational history. The recorder is attached after that log releases its mutex. Disk I/O runs on the main thread in `commit()`, after workers have stopped. `--record FILE` opens that file before boot and truncates it. Omitting it leaves recording disabled. The on-disk format is 1.0. Records are length-prefixed with an ISO-HDLC CRC. Sequence is the total order. Timestamps may move backward across producers. `docs/FLIGHT_RECORDER.md` is the format and the overflow contract.

## Replay

`ares-replay` reads the file, checks the header, checksum, sequence, and required records, and prints a report. It does not start `TaskSupervisor`, `MissionRuntime`, or `ares::run`. `docs/REPLAY.md` is the parser contract.

## Concurrency and ownership

`Logger::log` reads the clock and formats the line before taking the logger mutex. Flush runs after that lock is released. Do not log while holding a clock mutex. Task hooks must not call `poll()` or `wait_until` on the same clock.

Each simulated sensor instance has exactly one flight-task reader for its lifetime. `read()` stays const on the hardware contract and advances that sensor's private stream. Two concurrent `read()` calls on the same sensor object are a data race. The simulator does not take a lock to hide that. Navigation is the only reader of the IMU and of both GPS devices. Health is the only reader of the battery monitor and of the temperature sensor.

The navigation generation is published as a complete value. Recovery waits until that generation advances before it counts verification samples. A second navigation generation is not armed until the previous poll has returned.

## Resource bounds

Normal flight-state tables are fixed: fault registry 19, chaos schedule 16, chaos edges 32, event log 64, cycle log 64, miss log 32, recorder 256 with the last slot reserved for MissionEnd, recovery episodes 2. The logger may allocate a string per line. Replay may allocate while parsing. `snapshot()` allocates at shutdown. Those are outside the periodic flight-state tables. `docs/MEMORY.md` lists the same bounds.

A miss-log overwrite is `FaultHistoryOverflow` when no worker fault outranks it. A cycle-log overwrite is a warning only. `combine_exit` prefers a worker fault over miss-history overflow.

## Shutdown and lifetime

`wait_until` takes a `std::stop_token`, so a blocked task can shut down without a detached thread. `shutdown()` joins every launched worker. The stop token is checked again immediately before a navigation restart stores its new generation and re-arms. `ares::run` maps a worker fault to a non-zero `ExitCode` and does not rethrow it.

## Determinism

Measurement noise lives only in the simulated sensors. Each device owns a `DeterministicRng` (SplitMix64, one `uint64_t`, no heap). The mission seed is explicit. Stream index is 0 IMU, 1 primary GPS, 2 battery, 3 temperature, and the backup GPS uses its own stream so the two receivers do not draw each other's noise. The simulator does not call `std::random_device`.

ManualClock campaigns with the same scenario and seed repeat. Host SteadyClock runs are not byte-identical, and a short chaos window can be missed by desktop scheduling. Integer and time overflow paths saturate or reject. They do not wrap the spacecraft state. Fault and recovery identities are enumerations, not strings.

## Build and runtime boundaries

C++20, CMake 3.20, and Ninja. The supported compilers are GCC and Clang. MSVC is not supported because spacecraft integration uses `__int128`. GoogleTest 1.15.2 is fetched by URL and hash. Warnings are errors on project targets and are not applied to GoogleTest.

`ARES_ENABLE_SANITIZERS` adds ASan and UBSan for Clang and GCC after a configure-time link check. The `asan-ubsan` preset turns that on for a Debug build. Sanitizers are off in `debug` and `release`. `ARES_ENABLE_COVERAGE` instruments a separate build and cannot be combined with sanitizers. It defaults off. Format and tidy targets are `ares-format-check` and `ares-tidy`. Complexity is `ares-complexity`. They are not part of the default build.
