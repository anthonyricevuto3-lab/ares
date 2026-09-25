# ARES Architecture — v0.1

v0.1 establishes the flight-software skeleton. It does not simulate sensors, inject faults, or record a mission.

## Why these boundaries

Flight behavior has to stay testable when hardware, faults, and telemetry do not exist yet. The core library therefore owns mechanisms: time, logging, typed event storage, periodic execution, and thread lifetime. The flight library owns policy: which mode transitions are legal, and what the three example tasks mean. `ares` is the only composition root. It is allowed to depend on both.

```
ares  ->  flight  ->  core
tests ->  flight  ->  core
```

`core` does not include flight headers. There is no global logger, clock, or mode object. Dependencies are constructor arguments.

Empty `simulation`, `faults`, `telemetry`, and `recorder` directories are reserved. v0.1 does not put behavior there.

## Time

Durations are `std::chrono::nanoseconds` and have no epoch. `SteadyClock::time_point` is tagged `SteadyEpoch` and follows `std::chrono::steady_clock`. `ManualClock::time_point` is tagged `ManualEpoch` and starts at zero. The two time-point types do not convert or compare, so a manual reading cannot be waited on as a steady timestamp. Flight logic does not read wall-clock time. `advance()` rejects a negative step and a step that would overflow. `SteadyClock::now` and `wait_until` convert to and from the host steady-clock duration only after a range check. A value that does not fit returns `ClockStatus::Unrepresentable` and is not cast. When the host tick is coarser than one nanosecond, a successful wait is rounded up by at most one host tick, so timing precision is that tick.

`wait_until` takes a `std::stop_token`, so a blocked task can shut down without a detached thread and without a test sleeping on the wall clock.

A deadline is release-relative: completion later than `scheduled + deadline` is a miss, and finishing exactly on the deadline is on time. Completion before the scheduled release is `Unusable`, not on time. v0.1 does not measure a separate CPU budget. A late wake is a miss. After a cycle, the next release is the period boundary completion landed on, or the next boundary after completion when completion falls strictly between boundaries. A task that finishes exactly on its next release is scheduled at that instant and does not count it as skipped. A release strictly before completion is skipped so a late task does not burst. `poll()` stores the deadline record and the next release before hooks run. A throwing body consumes that release and is rethrown; it does not run again. A throwing hook returns `HookError` and does not rerun the body. `poll()` and `arm()` each succeed once per cycle or task: a nested `poll()` returns `Reentrant`, and a second `arm()` leaves the release unchanged.

## Modes

The machine starts in `Boot`. v0.1's executive uses only:

`Boot -> Initialization -> Standby -> Nominal`

`StartMission` is accepted only from `Standby`. The rest of the mode set is still explicit, so illegal transitions are defined now and later fault responses do not have to invent the model:

| From | Legal destinations |
| --- | --- |
| Boot | Initialization |
| Initialization | Standby |
| Standby | Nominal |
| Nominal | Standby, Degraded, SafeMode, Emergency |
| Degraded | Nominal, SafeMode, Emergency |
| SafeMode | Standby, Emergency |
| Emergency | SafeMode |

Rejected transitions leave the mode unchanged and emit no `ModeChangedEvent`. They do emit `ModeTransitionRejected` with a reason: `Illegal`, `InvalidMode`, or `Reentrant`. `can_transition` is false while a callback is running, matching `transition`. A transition attempted from inside the rejection callback is rejected and is not published again. A second boot does not invent an edge into `Initialization`; it records `BootRejected` from the current mode to itself. The executive stores that event before it tries to log. Logger failures are caught after the mode and the typed event are committed, so `boot_to_standby` still returns its `BootResult`. `StartMission` asks the machine to enter `Nominal`; the executive does not keep a second copy of that edge.

`transition()` commits the new mode before it notifies. A callback that calls `transition()` again is rejected, so a successful call leaves `mode()` equal to the requested target even if the handler tries to move on. If the change handler throws, the committed mode remains and the reentrancy guard is cleared. Nothing in v0.1 enters `Degraded`, `SafeMode`, or `Emergency` on its own. A deadline miss is an event, not a mode change.

## Tasks and threads

`PeriodicTask::poll()` is synchronous and is what tests call. It passes `std::stop_token` into the work function. Deadline state and the next release are stored before either hook runs. The miss hook runs before the cycle hook. `TaskSupervisor` keeps a fixed table of `std::jthread` workers. Threads block on a start gate until every launch has succeeded. `start()` then marks itself started and opens the gate. If startup fails, the gate is cancelled, launched threads are joined, and no task body has run. A worker exception is caught at the thread boundary, stored as `WorkerFault` without allocating, and requests stop on the whole supervisor. Hook failures are `WorkerFault::Hook`, not a body exception. `health()` reports `Hung` when stop has been requested and the worker is still inside work, `Faulted` after a worker exits with a fault, `Stopped` after a clean exit, and `Invalid` for an index that is not in the table. `fault()` returns `nullopt` for an invalid index; `None` means that worker has no fault. `shutdown()` requests stop, waits up to 50 ms of host time for every launched worker to reach `Exited`, records who is still in any other phase, then joins. The returned `ShutdownReport` marks those workers `missed_grace`. If join then sees `Exited`, the same worker is also `exited`, which is a late exit rather than a clean shutdown. It does not detach or kill a thread. A worker that never returns still blocks that join, and no report is returned until it does. `stopped_workers()` counts threads that have left their loop. `DeadlineRecord` remains on the task when `poll` returns `ScheduleError`, including a miss computed before the schedule failed. `ares::run` maps a worker fault to a non-zero `ExitCode` and does not rethrow it. A deadline-miss log that overwrote an older miss returns `FaultHistoryOverflow` when no worker fault outranks it. Ordinary cycle-log overflow is logged and is not that failure.

`MissionRuntime` in `src/application.cpp` owns the steady clock, logger, event logs, flight tasks, and supervisor, in that order. The supervisor is destroyed first, so workers are joined before the clock or tasks are destroyed.

The three flight tasks are `NavigationCadence`, `HealthPulse`, and `CommBeacon`. They increment a cycle counter and log. They do not read sensors. The composition root chooses their periods and deadlines. The executable uses 100 ms, 200 ms, and 400 ms, with each deadline equal to its period. Shorter deadlines are often missed by the desktop scheduler, and that miss is reported rather than hidden.

## Events and logs

Mode changes, commands, and rejected transitions are a `std::variant` in a fixed-capacity `EventLog`. `publish` copies into that storage and does not allocate. When the log is full, the oldest event is overwritten, the new event is kept, and the overwrite is counted. Cycle and deadline-miss records are not in that variant. They use `TaskId` (a fixed 16-byte name) and a mutex-protected `BoundedLog` with the same overwrite-oldest rule. Shutdown logs retained misses from both the miss log and each task's `DeadlineRecord`, plus the overwrite counts. A miss-log overwrite is `FaultHistoryOverflow`. A cycle-log overwrite is a warning only. Normal runs are expected to have zero miss-log overwrites. `boot_to_standby` returns both its status and the spacecraft mode actually reached. A rejected boot is a `BootRejected` event, not a fabricated mode edge.

`Logger` writes one line per record to an injected stream and stamps it with the injected clock. `enabled` lets a caller skip formatting when the level will be dropped. Messages that contain newlines are flattened. The complete line is queued under the mutex; `flush` runs after that mutex is released, and only one thread touches the stream at a time.

## Lock order

`Logger::log` reads the clock and formats the line before taking the logger mutex. The mutex only queues the finished line. Flush runs after that lock is released. Do not log while holding a clock mutex. Task hooks must not call `poll()` or `wait_until` on the same clock.

## v0.2 Digital spacecraft

Hardware contracts live in `include/ares/hardware` and depend only on core time types. Flight code (`NavigationCadence`, `PowerManager`, `ThermalMonitor`) holds those interfaces. `ares_simulation` implements them and does not include flight policy. `ares_app` is the only composition root that sees both.

```
ares_app -> ares_flight -> ares_hardware -> ares_core
ares_app -> ares_simulation -> ares_hardware -> ares_core
```

`SpacecraftModel` owns the truth state and stamps samples from the injected clock. Position and attitude integrate at a constant rate from an epoch. Displacement is `rate * elapsed_nanoseconds / 1000000000`, truncated toward zero. The product is formed in a 128-bit integer, so the intermediate multiplication does not wrap. The `int64` result saturates only when that quotient does not fit. If the clock is earlier than the epoch, `state_now` returns no value and the simulated sensors report `Unavailable`. v0.2 has no measurement noise. A sensor status is only what the sensor reports. There is no fault injection and no mode change from battery or temperature.

Replacing `SimulatedImu` with a future `HardwareImu` does not change navigation, because navigation never names the simulated type.

## v0.2.1 Sensor freshness and deterministic noise

The sensor still reports `SensorStatus`: `Valid`, `Invalid`, `Stale`, or `Unavailable`. It does not decide that a sample is stale because of its age. Flight software makes that decision with `evaluate_freshness`.

The evaluator takes the reported status, the sample timestamp, the current ARES time, and a maximum age. The result is `SampleUsability`:

| Result | Meaning |
| --- | --- |
| Usable | Reported `Valid`, and the age is representable and not greater than the limit |
| Stale | Reported `Stale` and the age fits, or reported `Valid` and the age fits and is greater than the limit |
| Invalid | Reported `Invalid`, the timestamp is not in the future, and the age fits |
| Unavailable | Reported `Unavailable`, the timestamp is not in the future, and the age fits |
| Future | Sample timestamp is later than the current ARES time |
| TimeError | The clock sample is unusable, the age does not fit, or the limit is negative |

Age equal to the limit is still `Usable`. Time is checked before the reported status, so a future timestamp is never `Usable`. The arithmetic is `checked_time_between`. Nothing in this path reads the wall clock.

`ares::run` passes named limits from `ares/flight/sample_limits.hpp`. Navigation IMU is 200 ms (two 100 ms periods). Navigation GPS is 1 s (ten navigation periods). Power and thermal are 400 ms (two 200 ms health periods). `NavigationCadence`, `PowerManager`, and `ThermalMonitor` all call the same evaluator. A solution is accepted only when `usability` is `Usable`. The combined sensor-reported status stays on `NavigationSolution::status`. Callers do not infer usability from `SensorStatus`.

`NavigationCadence::solution()` is the latest combination, including a result that is not usable. `last_usable()` changes only when that combination is `Usable`. `PowerManager` and `ThermalMonitor` keep `latest_observation()` apart from `last_usable()`. Every read updates the observation and the usability result. Only `SampleUsability::Usable` replaces the last usable sample. None of these consumers change spacecraft mode.

Measurement noise lives only in the simulated sensors. Truth remains constant-rate integration from the epoch, using the widening product above. Each device owns a `DeterministicRng` (SplitMix64, one `uint64_t`, no heap). The mission seed is an explicit `SensorNoise::mission_seed`. The stream seed is

`SplitMix64(mission_seed + (stream_index + 1) * kStreamSalt).next()`

with `kStreamSalt = 0x9E3779B97F4A7C15`. Stream index is 0 IMU, 1 GPS, 2 battery, 3 temperature. Unsigned wrap is modulo 2^64. Drawing more values from one sensor does not advance another sensor's stream.

Each simulated sensor instance has exactly one flight-task reader for its lifetime. `read()` stays const on the hardware contract and advances that sensor's private stream. Two concurrent `read()` calls on the same sensor object are a data race. The simulator does not take a lock to hide that. `MissionRuntime` follows the rule: the navigation task is the only reader of the IMU and of the GPS, and the health task is the only reader of the battery monitor and of the temperature sensor.

A positive amplitude draws one integer offset per scalar, uniform on `[-amplitude, +amplitude]`, in axis order x, y, z. IMU draws acceleration, then angular rate. GPS draws position, then velocity. Battery draws voltage, then current, then state of charge. Temperature draws one value. A non-positive amplitude does not draw and adds nothing, so the zero-noise configuration matches v0.2. The offset is added with a checked sum; a sum that does not fit saturates to the `int64` limit. An amplitude too large to form the inclusive span does not draw, leaves the truth value, and reports `Invalid`. The simulator does not call `std::random_device`.

## v0.3 Fault detection and response

v0.3 adds flight-side fault detection, a bounded registry, and a small recovery policy. It does not inject faults, and it does not restart tasks or command hardware. The rules are in `docs/FAULT_MODEL.md`. This section is how those rules sit on the v0.2.1 software.

Detection answers what is wrong. Policy answers what to do. `FaultRegistry` stores records and does not choose a mode. Sensor classes, `SpacecraftModel`, and the scheduler do not contain policy.

`NavigationSolution` now keeps the IMU usability and the GPS usability that `combine_navigation` already computed. Callers do not run the freshness arithmetic a second time. The combined `usability` is still the worse of those two results. A clock that is not `Ok` still makes the combined result `TimeError`, and both per-sensor results are `TimeError` as well.

The health task is the only mutator of `FaultRegistry`. Navigation publishes IMU and GPS usability into `FaultMailbox`. Each task's cycle hook publishes that task's deadline result into the same mailbox. The health task publishes battery and temperature after it samples them, consumes the mailbox, updates the registry, and may call `FlightExecutive::request_mode`. The mailbox is the only new lock. It does not guard the registry.

An IMU, GPS, or temperature slot keeps the newest usability and whether any `Usable` sample arrived since the last consume. It is not a queue. If `Usable` arrived, that consume clears the source's sensor-health faults first. If the newest sample is not `Usable`, the same consume then applies it as a new detection, so its consecutive count starts at one. A newest `Usable` sample only clears. If no `Usable` arrived, only the newest sample is applied. `Future` and `TimeError` still hold. A window of `Usable` then `Future`, or `Usable` then `TimeError`, still clears. The hold sample creates no fault. Battery is published once in the health cycle that consumes it, so it has no multi-sample window. A deadline miss stays set until that consume, so an on-time publish cannot erase a miss the health task has not taken yet.

The cycle hook runs after the task body. The health task's own deadline observation is published after FDIR has already run, so it is consumed on the next health cycle. If the health body has already observed stop, it does not consume the mailbox and it does not call policy. A deadline the hook still publishes is left pending and is discarded when the mailbox is destroyed. The task supervisor is the last member of the mission runtime, so workers are joined before the mailbox and the controller are destroyed. After `start()`, the main thread does not call `request_mode`.

`request_mode` is the existing mode machine. The mode is committed before the log line. A logging failure does not undo the transition, and it does not decide whether the transition is attempted. Policy requests only edges the v0.1 table already allows: `Nominal -> Degraded`, `Degraded -> Nominal`, `Nominal -> SafeMode`, `Degraded -> SafeMode`, and, after three consecutive healthy evaluations, `SafeMode -> Standby`. It does not request `Emergency`. It does not request `SafeMode -> Nominal`. `FdirController::apply` keeps the requested mode and the machine's `TransitionStatus` as separate fields. A requested `Degraded` or `Standby` is not proof the spacecraft is in that mode. The registry is not rewritten from that status. The SafeMode healthy-cycle counter lives in the controller, which is the health task's object. A stop observed before `apply` does not advance it.

## v0.4 Deterministic chaos engine

The chaos engine is simulation infrastructure. It does not write `FaultRegistry`, call `FdirController`, request a mode, or build a fault record. A scenario changes what a simulated device returns, or how long a task appears to have run. Flight software reads the device, evaluates freshness or the deadline, publishes the mailbox, and lets the existing v0.3.1 policy respond.

`ChaosEngine` owns a fixed schedule of at most 16 events and the scenario epoch. After `load`, the schedule is immutable. Device reads are pure queries of simulation time. Freeze samples are cached on the simulated sensor that has the single reader, not in a lock shared by every task. `note()` is the one mutator after load, and only the health task calls it, to log injection edges. It does not call FDIR or the mode machine.

Events use ARES time from the scenario epoch: active while `start <= elapsed < start + duration`. The same scenario, seed, and clock sequence produce the same injection view. There is no `std::random_device` and no sleep in the scenario clock. A zero-duration event never becomes active. Two events at one timestamp keep declaration order. An end at that same timestamp is reported before a start.

Sensor freeze captures one sample, including its timestamp, for one freeze-event identity. A later freeze captures a new sample even if the sensor was not read between the events. The clock keeps moving. `evaluate_freshness` is what turns that age into `Stale`. A read with no freeze active uses the current time. Unavailable and invalid injections change the sensor-reported status. They do not return `SampleUsability`. Overlapping events on the same target are rejected at load. A failed load leaves the previous schedule unchanged and does not start workers. A battery override replaces a valid simulated voltage for the event window. `PowerManager` and the low-battery hysteresis decide `LowBattery`. Restoring the voltage does not clear that fault by itself.

A task delay is a `SimulatedExecution` note on that worker. `PeriodicTask` adds it to the completion timestamp after the body returns, and the existing deadline monitor scores the miss. The note is null, or the extra is zero, on every uninjected cycle, so the v0.1 schedule is unchanged for those cycles. Navigation and communications can carry a delay. The health task does not. Communication dropout is deferred: the beacon has no simulated link to interrupt.

`ares --scenario NAME` selects a named schedule before workers start. The default is `nominal`. An unknown name exits with a usage error and does not start tasks. `--seed` sets the noise stream. The shipped scenarios use zero noise so an override is the exact millivolt value. The schedules and the boundary are described in `docs/CHAOS_ENGINE.md`.

## v0.5 Autonomous recovery

The health task owns recovery. After it ingests the mailbox it asks `RecoveryManager` what to do, then applies mode policy. The manager does not write the registry and does not request a mode. Two actions exist: restart navigation, and switch from primary GPS to backup GPS. The rules are in `docs/RECOVERY.md`.

Navigation restart is a handoff on the existing worker. Generation N's `poll` returns before generation N+1 is armed. The generation counter does not wrap. Two restarts may be executed in one episode. Success is three consecutive on-time completions from that generation, not the handoff itself. While that verification is open, or after it has failed, `SafeMode` does not return to `Standby` and `Degraded` does not return to `Nominal`.

Primary and backup GPS are separate simulated devices. Navigation reads each once per cycle and puts only the selected sample in the solution. A persistent primary sensor warning switches to backup and isolates primary without clearing the primary fault. Three usable backup samples from after that switch complete that recovery. Samples from before the switch do not count. There is no automatic failback.

A verified isolated primary warning stays in the registry and stops blocking mode recovery. A failed backup verification does not. That `RecoveryFailed` is terminal for the mission: one automatic GPS attempt, selection stays on backup, and later healthy samples may clear fault records without clearing the recovery failure or starting another episode. A new `MissionRuntime` is what leaves that state. Fault health and recovery confidence stay separate. The stop token is checked again immediately before a navigation restart stores its new generation and re-arms.

## v0.6 Flight recorder and replay

`FlightRecorder` is an optional observer owned by `MissionRuntime`. It copies already-produced mode, fault, recovery, chaos, deadline, task-generation, and GPS-selection edges into a fixed prefix buffer. It does not write `FaultRegistry`, `RecoveryManager`, the mode machine, the GPS selector, the task supervisor, or `ChaosEngine`. A full buffer or a failed disk write does not change those authorities. `docs/FLIGHT_RECORDER.md` is the format and the overflow contract. `docs/REPLAY.md` is the offline parser.

`EventLog` remains the bounded in-memory operational history. The recorder is attached as an observer after that log releases its mutex, plus thin adapters for chaos edges, navigation generation, and GPS failover. If the log overwrites an old event, the recorder can still hold the copy it already took. The recorder is not authoritative.

Disk I/O runs on the main thread in `commit()`, after workers have stopped. The flight path only copies a fixed slot under one recorder mutex. `--record FILE` opens that file before boot and truncates it. Omitting it leaves recording disabled. Record sequence is the order in the file. Logical timestamps may decrease across threads, and replay keeps sequence order. `ares-replay` reads the file and does not start a mission.

## Build

C++20, CMake, and Ninja. GoogleTest 1.15.2 is fetched by URL and hash. Warnings are errors on project targets. `ARES_ENABLE_SANITIZERS` adds ASan and UBSan for Clang and GCC, and ASan for MSVC, after a configure-time link check. The `debug-sanitizers` preset and CI turn that on for a Debug build. Sanitizers are off unless requested. The current MSYS2 UCRT64 GCC cannot link them because the runtime libraries are absent; CI uses Clang on Ubuntu, where they are present.
