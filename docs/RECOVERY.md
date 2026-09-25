# ARES recovery — v0.5

v0.5 adds two subsystem recoveries. Navigation can be restarted inside its existing worker. GPS can fail over from the primary device to the backup device. Nothing else is recovered.

`RecoveryManager` is owned by `FdirController` and is called only from the health task, after ingest and before mode policy. It does not detect faults, write `FaultRegistry`, clear a `FaultRecord`, or call `request_mode`. It does not know `ChaosEngine`.

`FaultRegistry` remains the fault state. `RecoveryManager` remains the recovery state. `ModeMachine` remains the mode authority.

## Actions

Subsystem actions are `RestartTask` and `SwitchSensor`. They are not the mode `RecoveryAction` values (`None`, `ContinueDegraded`, `EnterSafeMode`, `RecoverToStandby`).

Each action has its own episode: `Idle`, `Executing`, `Verifying`, `Succeeded`, `Failed`. `Requested` is part of the type and is not used as a resting state. An episode that is `Executing`, `Verifying`, or `Failed` blocks `Degraded -> Nominal` and `SafeMode -> Standby`. `Succeeded` and `Idle` do not.

## Events

One `RecoveryEvent` is published when an attempt starts, and one when that episode reaches `Succeeded` or `Failed`. Verification samples do not publish events. The record holds the action, target, attempt, navigation generation when the action is a restart, and the ARES time. Identities are enumerators. A full event log does not change the recovery state.

## Navigation restart

The trigger is the existing `DeadlineMiss` on `NavigationTask` at `Critical` (five or more misses). The fault is not cleared when the restart is requested.

The health task posts one atomic request: target generation = current + 1. A repeated health cycle while that request is pending does not post another one. The navigation worker is the only thread that honors it, and only after `poll` has returned. It disarms generation N, clears that task's deadline record and any pending simulated-execution extra, checks stop, samples the clock, and checks stop once more immediately before storing generation N+1 and arming it. Those two commits happen together. The same `jthread` continues. No second navigation thread is created.

Generation starts at 0 and lives for the mission. It does not wrap. At `UINT32_MAX` the episode fails and no new generation starts.

What a restart resets: the armed flag, the next release, the in-task deadline record, the simulated-execution extra, and, on the next work entry, the navigation solution and last usable sample. What it does not reset: the `DeadlineMiss` record, its occurrence count, the chaos schedule, the sensor RNG, the thread, or a worker fault.

Stop dominates. If stop is already set when the handoff runs, or becomes set before the generation is stored, generation N+1 is not stored and is not armed. Shutdown continues.

An episode allows two executed restarts. The attempt count is recovery state, not `occurrence_count`. The next attempt starts only when the new generation itself reaches five misses. After the second such failure the episode is `Failed`, one `RecoveryFailed` is published, and the fault stays. When that fault later becomes inactive, the episode returns to `Idle` and a later critical streak may have two new attempts.

## Verification of a restart

Executing the handoff is not success. The new generation must publish three consecutive on-time navigation completions. A miss zeros that run. The mailbox keeps the final consecutive run across a health cycle, so `OnTime, OnTime` is 2, `OnTime, Miss` is 0, `Miss, OnTime` is 1, and `Miss, OnTime, OnTime` is 2. The sticky miss bit used by FDIR is unchanged: a miss in the window is still a miss even if a later sample in that window is on time.

Each deadline publish carries the generation that produced it. A publish from another generation zeros the run. An old on-time cannot verify the new generation, and an old miss cannot fail it. The first on-time can still clear `DeadlineMiss` through normal FDIR while verification still needs three. Mode policy does not leave `SafeMode` during that verification.

## GPS redundancy

Primary and backup are two `IGps` devices. Both read the same spacecraft truth. Each has its own RNG stream (`SensorStream::Gps` and `SensorStream::BackupGps`), freeze cache, chaos target, status, and fault source (`PrimaryGps`, `BackupGps`). One device's freeze or extra draw does not move the other stream.

`GpsSelector` does not know `SimulatedGps` or `ChaosEngine`. It starts on primary. Health stores selection and isolation in one atomic word. Navigation loads that word once and reads each device once. The navigation solution uses only the selected sample. Both samples are published for FDIR.

Failover starts only when primary is selected and a primary sensor-health warning (`Unavailable`, `Invalid`, or `Stale`) has `consecutive_count >= 3`. A backup fault while primary is selected does not switch. `Future` and `TimeError` do not start failover.

The switch marks primary isolated, selects backup, and publishes `RecoveryStarted`. Isolation does not clear the primary fault. Primary is still sampled, so the fault can clear on its own. There is no automatic return to primary.

## Backup verification

The switch is not success. The mailbox keeps a consecutive usable run and a consecutive non-usable run for backup, updated on every physical publish and not cleared by consume. Health reads that absolute run. It does not add one step per health cycle. Any non-usable sample, including `Unavailable`, `Invalid`, `Stale`, `Future`, and `TimeError`, zeros the usable run. Three usable publishes succeed the episode. Three non-usable publishes fail it. The run is rebaselined when the switch is commanded, so samples from before the switch do not count.

There is one switch. If backup cannot be verified, the episode is `Failed`, one failure event is published, and selection stays on backup. A sensor warning alone still does not enter `SafeMode`; `LowBattery` and a critical deadline still do.

After success, selection stays on backup even if primary becomes healthy. The primary record stays in the registry. While the GPS episode is `Succeeded` and primary is isolated, that primary sensor-health warning is ignored by mode policy. It is not deleted. A deadline or `LowBattery` is never ignored by this rule. A backup fault is never ignored.

## Terminal GPS failure

A failed automatic GPS recovery is terminal for that mission. GPS has one automatic failover attempt per `MissionRuntime`.

Fault health and recovery confidence are not the same thing. Later healthy samples may clear `FaultRecord`s through ordinary detection. Clearing those records does not erase a terminal `RecoveryFailed`, does not select primary, and does not start another GPS episode. The recovery state stays `Failed`. That state remains a mode-recovery blocker: `Degraded -> Nominal` and `SafeMode -> Standby` stay suppressed for the rest of the mission. Selection remains backup. There is no second switch and no automatic failback.

The reason is the attempt budget. ARES used its one allowed automatic redundant-source recovery and could not verify it. Later good samples do not prove that recovery should be trusted. Trusting it again would be another recovery lifecycle, which v0.5 does not perform. Resetting this terminal state requires a new `MissionRuntime`.

A finished navigation episode is different. When its deadline record becomes inactive, that episode returns to `Idle`. A failed GPS episode does not.

## Registry and chaos

The closed identity space is 19: five sensor sources times three sensor-health types, three task deadlines, and `LowBattery`. The production registry capacity is 19.

`ChaosTarget::Gps` and `ChaosTarget::PrimaryGps` are the same physical device. An overlapping freeze on `Gps` and invalid on `PrimaryGps` is rejected. `BackupGps` is a different target and may overlap the primary device. Chaos still does not know the recovery manager, the selector, the registry, or the mode machine.

## Shutdown

Global stop prevents generation N+1. The supervisor is still destroyed first and joins the worker before the selector, the sensors, and the recovery state are destroyed. A pending restart does not keep shutdown alive.
