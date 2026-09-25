# ARES fault model — v0.3.1

This is the flight-side fault lifecycle. It uses samples and deadline records the spacecraft software already has. It does not inject faults and it does not command hardware.

## Identity

A logical fault is the pair `(FaultType, FaultSource)`. Both are enumerators. The registry does not compare names.

Fault types: `SensorUnavailable`, `SensorInvalid`, `SensorStale`, `DeadlineMiss`, `LowBattery`.

Sources: `Imu`, `PrimaryGps`, `BackupGps`, `Battery`, `Temperature`, `NavigationTask`, `HealthTask`, `CommunicationsTask`.

Severity is fixed by type:

| Type | Severity |
| --- | --- |
| SensorUnavailable, SensorInvalid, SensorStale | Warning |
| DeadlineMiss | Advisory |
| LowBattery | Critical |

`SampleUsability::Future` and `SampleUsability::TimeError` are not fault types. They are not stored as `SensorStale`.

## Record

A record holds the type, source, severity, first detection time, most recent detection time, lifetime occurrence count, consecutive count, and active flag. There is no `std::string` in the record.

Clearing sets the record inactive and zeroes the consecutive count. First detection, most recent detection, and the occurrence count stay. A later detection of the same pair reactivates that slot, keeps the original first-detection time, and continues the occurrence count. The consecutive count starts again at one.

## Registry

`FaultRegistry` is a fixed array. The flight size is 19: five sensor sources times three sensor-health types, three task deadlines, and `LowBattery`. It does not grow, and it is not thread-safe. The health task is the only writer.

A repeated detection updates the existing slot. It does not take a new one.

When a new identity needs a slot and every slot is full:

- The lowest-index inactive slot is reused. That inactive history is discarded.
- If every slot is active, the new fault is refused. The existing active records stay. `saturated()` latches and stays set.

Policy treats a saturated registry as a critical condition, so a refused fault is not silent.

## Detection

Sensor detection reads `SampleUsability`. It does not recompute age.

| Usability | Effect |
| --- | --- |
| Unavailable | Activate `SensorUnavailable` |
| Invalid | Activate `SensorInvalid` |
| Stale | Activate `SensorStale` |
| Usable | Clear every active sensor-health fault for that source |
| Future, TimeError | Hold. Do not activate, and do not clear |

A source has one current sensor-health fault. A change from stale to invalid clears the stale record and activates invalid. That is a new record, not a continuation of the stale count.

An unusable battery sample produces the sensor-health fault and does not update `LowBattery`. The voltage is not treated as a measurement. A usable sample below 11000 mV activates `LowBattery`. A usable sample above 11500 mV clears it. From 11000 mV through 11500 mV the previous `LowBattery` state is kept. Those two thresholds live in `ares/flight/fdir_limits.hpp`.

A deadline miss activates `DeadlineMiss` for that task. The record's severity follows the current miss streak on that same `(DeadlineMiss, task)` identity. One miss and a second consecutive miss stay Advisory and do not change spacecraft mode. The third and fourth are Warning. The fifth and later are Critical. An on-time completion clears that task's record, zeroes its consecutive count, and keeps the lifetime occurrence count. A later miss reactivates the same record at Advisory with consecutive count 1. Navigation, health, and communications each keep their own streak. An unusable deadline result holds. The health task's own deadline sample is still consumed one health cycle later.

## Mailbox window

Navigation can publish IMU and GPS usability faster than the health task consumes it. Each of those slots, and the temperature slot, stays fixed-size. The slot keeps the newest usability and a flag that records whether any `Usable` sample arrived since the previous consume. It does not keep a queue of every intermediate sample.

When health consumes a pending sensor slot:

1. If `Usable` was seen in that window, apply `Usable` first. That clears the source's sensor-health faults and zeroes their consecutive counts.
2. If the newest sample is not `Usable`, apply that newest sample next, as a new detection. Its consecutive count starts at 1.
3. If the newest sample is `Usable`, the clear is the whole effect.
4. If no `Usable` was seen, apply only the newest sample.

`Future` and `TimeError` are still Hold. In a window with no `Usable`, a newest `Future` or `TimeError` does not clear the existing sensor-health record and does not reset its consecutive count. A window that saw `Usable` and whose newest sample is `Future`, or `Usable` and then `TimeError`, still performs the clear first. The Hold sample does not create a fault.

Battery is published once by the health task in the same cycle that consumes the mailbox, so that slot has no multi-sample window. A deadline miss stays set until consume. An on-time publish does not erase a miss that has not been consumed yet.

The health task's own deadline is published by the cycle hook after that cycle's FDIR evaluation. The observation is consumed on the next health cycle. It has one health-cycle of latency.

If stop is already requested when health reaches FDIR, the mailbox is not consumed and policy does not run. Pending observations are discarded when the mailbox is destroyed. The task supervisor is destroyed first and joins the workers before that destruction. A cycle hook may still publish a deadline after the body has observed stop. That publish does not request a mode.

## Persistence and policy

Persistence belongs to one logical fault, the pair `(FaultType, FaultSource)`. It is the consecutive detection count on that record, stamped with the injected clock. It is not a wall-clock sleep, not a time window, and not a count of "this sensor has been unhealthy."

`Future` and `TimeError` hold. With no `Usable` sample, they do not clear a record and they do not reset its consecutive count. `Stale`, then `Future`, then `Stale` is one streak on the stale record: the second stale continues that count. `Stale`, then `TimeError`, then `Stale` does the same.

`Stale`, then `Invalid`, then `Stale` is not one streak. The invalid sample clears the stale record and starts `SensorInvalid` at consecutive count 1. The later stale sample clears invalid and starts `SensorStale` again at 1.

Warning faults, including a stale sensor, are recorded on the first detection. A deadline fault is Advisory until its own streak reaches 3, so it is not a persistent warning before that. Policy leaves `Nominal` alone until a Warning record's consecutive count reaches 3 (`kWarningPersistence`). That third detection may request `Degraded`. A deadline Warning already has consecutive count 3, so it is persistent as soon as it becomes a Warning. Advisory faults do not request a mode change. One active `Critical` fault, including a deadline streak of 5 or more, or a saturated registry, may request `SafeMode` on that detection. A critical condition is dominant: a persistent warning beside it does not change the `SafeMode` decision. When the critical fault later clears and a persistent warning remains, policy does not request `Nominal` and SafeMode recovery does not start. The action stays `ContinueDegraded`. Hysteresis is what keeps a battery reading from chattering; the critical response itself is not delayed.

Recovery actions are `None`, `ContinueDegraded`, `EnterSafeMode`, and `RecoverToStandby`. The action is the recommendation. It is not proof the spacecraft entered that mode. `PolicyDecision` keeps `requested_mode` separate from `transition`. `requested_mode` is empty until the current mode has a legal edge. `transition` is empty until `FdirController::apply` records the mode machine's `TransitionStatus`. A rejected transition leaves the mode and the registry unchanged. The registry is not updated from that status. The mode machine remains authoritative.

`evaluate_fault_policy` does not leave `SafeMode`. The controller does, and only toward `Standby`, which is an existing edge. It counts consecutive healthy `apply` calls while the mode is `SafeMode`. A call is healthy when `safe_mode_exit_blocked` is false: the registry is not saturated, no active fault is Critical, and no active Warning has reached the persistence count. Advisory faults and shorter warnings do not block. Any blocked call sets the counter to zero. The third healthy call sets `RecoverToStandby` and requests `Standby`. That call does not also request `Nominal`. After the mode is `Standby`, FDIR does not start the mission again. A rejected `Standby` request leaves the spacecraft in `SafeMode`; the counter remaining ready is not a successful recovery.

When no critical condition is present and no warning has reached the persistence count, policy may request `Degraded -> Nominal`. An advisory fault does not block that return. `SafeMode` is left only by the counted Standby recovery above.

Clearing a record is fault health. It is not recovery confidence. GPS has one automatic failover per mission. If backup verification ends in `RecoveryFailed`, later usable samples may clear the GPS records and still leave that recovery state `Failed`. The terminal failure keeps blocking `Degraded -> Nominal` and `SafeMode -> Standby`. Selection stays on backup, and a new automatic GPS episode does not begin. A new mission is required to leave it. A finished navigation episode still returns to idle when its deadline record is inactive. A failed GPS episode does not.

## Events

`FaultActivated` is published when a record becomes active, including reactivation. `FaultCleared` is published when it becomes inactive. `FaultUpdated` is published once when an active warning's consecutive count reaches the persistence limit, and when a `DeadlineMiss` record changes severity. Later repeats in the same severity are not events. The registry remains the authoritative state. Mode changes still publish the existing `ModeChangedEvent`, including `SafeMode -> Standby`.

## What this milestone does not do

v0.5 restarts navigation and fails over GPS, as `docs/RECOVERY.md` describes. It does not roll back a checkpoint or vote. It does not invent a fault type for a future timestamp or a time error. One mailbox window still does not retain every intermediate non-usable sample. It retains whether `Usable` occurred, what the newest sample was, and the final consecutive usable or on-time run used by recovery. SafeMode recovery does not skip `Standby`, and it does not resume the mission. It also waits while a subsystem recovery is still verifying or has failed. A failed GPS recovery keeps that block for the rest of the mission. Clearing the fault records does not end it.
