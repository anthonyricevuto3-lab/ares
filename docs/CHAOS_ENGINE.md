# ARES chaos engine — v0.4

The chaos engine creates fault conditions. It does not create fault records.

## Boundary

The engine lives in the simulation layer. `MissionRuntime` owns it. Simulated IMU, GPS, and battery hold a const pointer to it. Navigation and communications write a per-task execution note that only their own `PeriodicTask` reads.

It does not write `FaultRegistry`, call `FdirController`, request a spacecraft mode, construct a `FaultRecord`, or change a persistence counter. It does not bypass `IImu`, `IGps`, or `IBatteryMonitor`.

```
ChaosEngine
    -> simulated device or simulated execution time
    -> flight task reads the device or finishes a cycle
    -> freshness, power, or the deadline monitor
    -> FaultMailbox
    -> FDIR
    -> FaultRegistry
    -> FaultPolicy
    -> ModeMachine
```

FDIR decides whether a condition is a fault. v0.3.1 policy is unchanged.

## Scenario model

An event is a typed record: injection kind, target, start, duration, and one integer parameter. Kinds in v0.4 are `SensorUnavailable`, `SensorInvalid`, `SensorFreeze`, `BatteryVoltageOverride`, and `TaskExecutionDelay`. Targets are IMU, GPS, battery, the navigation task, and the communications task.

Time is ARES monotonic time from the scenario epoch, which is the clock sample taken when the workers are about to start. There is no wall-clock schedule and no sleep in the scenario. An event is active while `start <= elapsed < start + duration`. A zero duration never activates. The schedule holds at most 16 events. Two events that share a timestamp keep declaration order. If an event ends at the same time another starts, the end is reported first.

`note()` logs those edges (`t_ms=... target kind started|restored`). Only the health task calls it. The log cannot change a mode or a fault. Device queries do not take a lock. Freeze state sits on the sensor object, and that object still has one flight-task reader.

`load` is a strong operation. Success installs the whole new schedule and its epoch. Failure leaves the previous schedule and epoch unchanged. A schedule larger than 16 events fails this way. So does any pair of events on the same target whose half-open intervals overlap (`start < other_end && other_start < end`). That covers two GPS freezes, freeze with invalid, freeze with unavailable, invalid with unavailable, two battery overrides, and two execution delays for the same task. Events on different targets may overlap, including a GPS freeze with a battery override and a GPS freeze with a navigation delay. Touching endpoints do not overlap: `[1s, 2s)` and `[2s, 3s)` are both accepted. A zero duration, or a start plus duration that does not fit, does not form an interval and does not activate. Declaration order is not a precedence rule.

`copy_edges` returns how many edges were written and whether the caller buffer was too small. A full buffer that holds every pending edge is not truncated. A short buffer sets `truncated`. Truncation affects only that copy. It does not change which events are active. The health-task log uses a buffer of 32 edges, which holds every start and end of a 16-event schedule.

## Sensor injection

Unavailable and invalid injections set the sensor-reported status and keep the current sample time. They do not return `SampleUsability`. Flight freshness and detection map that status as they already do.

A frozen sample belongs to one freeze event. `load` assigns each event a declaration-order sequence, and the sensor stores that sequence with the cached sample. A later read during the same sequence returns the cached sample and does not draw noise. A different sequence captures a new sample and replaces both the cache and the identity, even when no read happened while the sensor was unfrozen. Two GPS freezes are distinct events. A read while no freeze is active clears the cache and the identity, and that sample uses the current time. Cache correctness does not depend on observing the inactive interval. The clock continues during a freeze. When `now - frozen_timestamp` is greater than the GPS or IMU maximum age, `evaluate_freshness` returns `Stale`. Age equal to the limit stays usable.

## Battery override

While the override is active and the simulated sample is `Valid`, the reported voltage is the event parameter. The shipped low-battery and mixed scenarios set the model baseline to 12400 mV and the override to 10800 mV. 10800 mV is below the 11000 mV activation threshold, so a usable sample activates `LowBattery` and policy may enter `SafeMode`. When the override ends, the monitor reports 12400 mV again. That is above the 11500 mV clear threshold, so hysteresis clears `LowBattery`. The engine does not request `Standby`. Three later healthy FDIR evaluations do, on the existing `SafeMode -> Standby` edge, and that cycle does not enter `Nominal`.

## Deadline injection

`TaskExecutionDelay` stores a nanosecond count. The task body copies it into that task's `SimulatedExecution` note. After the body returns, `PeriodicTask` adds a positive note to the completion timestamp and clears the note. The deadline monitor then sees a late completion and records a miss. The cycle hook publishes that miss. FDIR escalates it with the v0.3.1 rule: 1–2 advisory, 3–4 warning, 5 or more critical. When the delay event ends, the next completion is the real clock sample. An on-time cycle clears the streak.

Navigation and communications are the delay targets. The health task is not. An uninjected cycle leaves the note at zero, so the completion timestamp stays the clock sample. If the completion timestamp plus a positive delay cannot be represented, the cycle is a schedule error: the task is disarmed, the miss flag stays clear, and the supervisor reports `Schedule`. That is not a deadline miss and it is not an on-time cycle.

Communication dropout is not implemented. The communications task has no simulated link, and a dropout will not be faked with a fault record.

## Repeatability and restoration

The same initial truth, mission seed, scenario, and clock sequence produce the same injection view and the same flight events. Scenarios use zero measurement noise, so the seed does not move a sample unless a test sets a positive amplitude. Ending an event returns that device to the model. `reset()` drops the schedule. One scenario does not leave injection state for the next load.

## CLI

```
ares --scenario nominal
ares --scenario gps_stale
ares --scenario low_battery
ares --scenario mixed_faults --seed 42
```

Names: `nominal`, `gps_stale`, `gps_unavailable`, `imu_invalid`, `low_battery`, `deadline_storm`, `mixed_faults`. The default is `nominal`. An unknown name exits nonzero with `unknown scenario:` and does not start workers. A schedule that fails `load` returns `StartupFailed` before `start()`, so workers do not run and the previous engine schedule is left unchanged. The shipped scenarios do not overlap on one target. `--duration-ms` is still the run length. The desktop clock must actually reach the event times; the deterministic tests advance `ManualClock` instead.

At shutdown the process logs one scenario line: name, final mode, fault activation count, fault clear count, mode-transition count, and `completed`. `completed=1` only when the process exit is success. A worker exception, a schedule fault, and a miss-history overflow are unsuccessful exits, so they log `completed=0`. The final spacecraft mode does not define that flag. A missed stop grace is logged and does not by itself change the exit code, so it does not by itself clear `completed`. The summary and the process exit code use the same result. That summary does not publish a registry reference.

## What this milestone does not do

It does not restart a task, add a redundant sensor, roll back a checkpoint, or vote. It does not add a communications stack. It does not make FDIR smarter.
