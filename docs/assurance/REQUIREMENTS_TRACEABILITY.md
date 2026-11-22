# Requirements traceability

Each row is one ARES requirement. The links are specific. "The src directory" is not a design component. No open defect is attached. This branch did not find a Critical or High defect in the frozen flight code.

Hazard IDs are defined in `HAZARD_ANALYSIS.md`. A dash means the requirement is not a modeled hazard control.

| Requirement | Design | Implementation | Verification | Hazard |
| --- | --- | --- | --- | --- |
| ARES-REQ-CORE-001 | `ExitCode` in `include/ares/application.hpp` | `exit_code_for` in `src/application.cpp` | `ApplicationExit.NamesMatchTheStableCodes` | — |
| ARES-REQ-CORE-002 | `combine_exit` in `include/ares/application.hpp` | call at the end of `src/application.cpp` | `ApplicationExit.CombinePrefersWorkerFaultOverMissHistory` | ARES-HZ-011 |
| ARES-REQ-CORE-003 | `TaskSupervisor` worker loop | `src/core/task_supervisor.cpp` | `TaskSupervisor.ThrowingTaskDoesNotTerminateAndStopsTheSupervisor`, `MissionExit.ThrowingWorkerThroughRunIsNonzero` | — |
| ARES-REQ-CORE-004 | `ManualClock` / checked time | `src/core/clock.cpp` | `CheckedTime.RejectsAStepPastTheRepresentableRange`, `ManualClock.AdvanceToTheLimitIsRejected` | ARES-HZ-010 |
| ARES-REQ-CORE-005 | spacecraft integrator | `src/simulation/spacecraft_model.cpp` | `SpacecraftIntegration.PositiveOverflowSaturates`, `SpacecraftIntegration.NegativeOverflowSaturates` | ARES-HZ-010 |
| ARES-REQ-CORE-006 | supervisor lifetime | `TaskSupervisor::stop` in `src/core/task_supervisor.cpp` | Test: `TaskSupervisor.StopUnblocksAWaitingTaskAndJoinsIt` joins a waiting worker. Inspection: `include/ares/core/task_supervisor.hpp` says threads are never detached, and `src/` has no `detach` call. | ARES-HZ-012 |
| ARES-REQ-SCHED-001 | `PeriodicTask` | `src/core/periodic_task.cpp` | `PeriodicTask.OnTimeWorkDoesNotMissAndSchedulesNextPeriod` | ARES-HZ-002 |
| ARES-REQ-SCHED-002 | deadline monitor | `include/ares/core/deadline_monitor.hpp` | `DeadlineMonitor.OneNanosecondLateIsAMiss`, `PeriodicTask.SlowWorkAndLateWakeAreMisses` | ARES-HZ-002 |
| ARES-REQ-SCHED-003 | navigation miss history | `MissionRuntime::misses_` in `src/application.cpp` is `BoundedLog<DeadlineMissEvent<SteadyClock::time_point>, 32>` | Test: `MissHistory.ProductionCapacityKeepsThirtyTwoThenOverflows` pushes 32 then one more on that type, checks overwrite, and checks `combine_exit`. Inspection: the member declaration is that same type. `BoundedLog.OverwritesTheOldestAndCountsOverflow` covers only a capacity-2 log and is not this evidence. | ARES-HZ-011 |
| ARES-REQ-NAV-001 | freshness then fault detection | `include/ares/flight/fault_detection.hpp` | `SensorInjection.GpsFreezeBecomesStaleThroughFreshnessThenRestores` | ARES-HZ-001 |
| ARES-REQ-NAV-002 | `GpsSelector` and navigation cadence | `include/ares/flight/gps_selector.hpp` | `NavigationCadence.SolutionUsesTheSelectedBackupSample` | ARES-HZ-003 |
| ARES-REQ-NAV-003 | per-stream RNG | `include/ares/simulation/random_source.hpp` | `GpsStreams.PrimaryAndBackupDrawsDoNotCross` | — |
| ARES-REQ-FDIR-001 | warning persistence is 3 | `fault_policy` in `include/ares/flight/fault_policy.hpp` | `FaultPolicy.SingleWarningDoesNotLeaveNominal`, `FaultPolicy.PersistentWarningRequestsDegraded` | ARES-HZ-001 |
| ARES-REQ-FDIR-002 | critical requests SafeMode | `FdirController::apply` in `include/ares/flight/fdir.hpp` | `FaultPolicy.CriticalRequestsSafeModeFromNominalAndDegraded` | ARES-HZ-002 |
| ARES-REQ-FDIR-003 | deadline streak 1–2 advisory, 3 warning, 5 critical and SafeMode | `include/ares/flight/fault_detection.hpp` | Test: `DeadlineEscalation.OneAndTwoMissesStayAdvisoryAndNominal`, `DeadlineEscalation.ThirdMissBecomesWarningAndMayDegrade`, `DeadlineEscalation.FifthMissBecomesCriticalAndMayEnterSafeMode` | ARES-HZ-002 |
| ARES-REQ-FDIR-004 | mode policy does not treat clear as Nominal | `include/ares/flight/fault_policy.hpp` | `FaultPolicy.ClearingCriticalDoesNotReturnToNominal` | ARES-HZ-006 |
| ARES-REQ-FDIR-005 | registry saturation | `include/ares/flight/fault_registry.hpp` | `FaultPolicy.SaturatedRegistryRequestsSafeMode`, `FaultRegistry.FullActiveTableRejectsAndLatchesSaturation` | — |
| ARES-REQ-FDIR-006 | fixed registry capacity | `FaultRegistry` | `FaultRegistry.ProductionCapacityAcceptsEveryLogicalIdentity`, `ResourceBudget.RegistryUpdatesStayInsideTheFixedTable` | — |
| ARES-REQ-REC-001 | restart waits for the new generation | `RecoveryManager` in `include/ares/flight/recovery.hpp` | `RecoveryManager.RestartStaysExecutingUntilTheGenerationAdvances` | ARES-HZ-012 |
| ARES-REQ-REC-002 | three on-time completions | `kRecoveryVerifyCount` in `include/ares/flight/fdir_limits.hpp` | `RecoveryManager.ThreeOnTimeCompletionsFromTheNewGenerationSucceed` | ARES-HZ-006 |
| ARES-REQ-REC-003 | two attempts then one failure | `kNavigationRestartAttempts` | `RecoveryManager.TwoRestartAttemptsThenOneFailure` | ARES-HZ-005 |
| ARES-REQ-REC-004 | generation does not wrap | `RecoveryManager` | `RecoveryManager.GenerationOverflowFailsWithoutWrapping` | ARES-HZ-010 |
| ARES-REQ-REC-005 | failover, no failback | `GpsSelector` plus `RecoveryManager` | `RecoveryManager.GpsFailoverVerifiesBackupAndDoesNotFailBack` | ARES-HZ-003 |
| ARES-REQ-REC-006 | unusable backup fails the switch | `RecoveryManager` | `RecoveryManager.UnusableBackupFailsTheSingleSwitch`, `ChaosCampaign.DualGpsFailureEmitsOneTerminalFailure` | ARES-HZ-004 |
| ARES-REQ-REC-007 | SafeMode then Standby, not Nominal, while navigation recovery is open or failed | `FdirController` | `FdirController.SafeModeWaitsForNavigationVerification`, `FdirController.FailedNavigationBlocksDegradedReturnToNominal` | ARES-HZ-007 |
| ARES-REQ-REC-008 | `combine_exit` inputs are worker faults and miss overflow | `combine_exit` and `exit_code_for` in `include/ares/application.hpp`; `finish_mission` in `src/application.cpp` | Test: `ApplicationExit.RecoveryFailedLeavesSuccessWhenNoOtherFault` reaches `RecoveryState::Failed` and asserts `combine_exit` is `Success` with no worker fault and no miss overflow. Inspection: `finish_mission` does not read recovery state. `Recording.RecoveryFailureStaysVisible` shows the failure in the recording only and is not this evidence. | ARES-HZ-005 |
| ARES-REQ-CHAOS-001 | scenario catalog | `include/ares/simulation/scenarios.hpp` | `ChaosCampaign.UnknownScenarioNameIsRejected` | — |
| ARES-REQ-CHAOS-002 | ManualClock determinism | `ChaosEngine` in `include/ares/simulation/chaos_engine.hpp` | `ChaosCampaign.RepeatedRunMatchesTheFaultAndModeTrace` | — |
| ARES-REQ-CHAOS-003 | sensor injection, then flight observation | `src/simulation/sensors.cpp`; `include/ares/simulation/chaos_engine.hpp` | Test: `SensorInjection.GpsUnavailableIsReportedByTheSensorAndRecordedByFdir` shows the sensor status and that FDIR records it when the test calls `observe_sensor`. Inspection: `chaos_engine.hpp` includes clock and logger only. It does not call `FaultRegistry`, `RecoveryManager`, or `ModeMachine`. The test does not prove that prohibition. | ARES-HZ-008 |
| ARES-REQ-CHAOS-004 | task execution delay, not a direct recovery write | scenario events in `scenarios.hpp` | `ChaosCampaign.HeldNavigationDelayFailsBothRestartAttempts`, `ChaosCampaign.NavigationDelayRestartsThenVerifies` | ARES-HZ-005 |
| ARES-REQ-RECORD-001 | recorder is an observer | `include/ares/recorder/flight_recorder.hpp` | `Recording.DisabledRunMatchesARecordedRun` | ARES-HZ-009 |
| ARES-REQ-RECORD-002 | open failure before boot | `src/application.cpp` record path | Test: `Application.RecordOpenFailureDoesNotStartAMission` returns `RecorderFailed` and does not log `tasks started`. | ARES-HZ-009 |
| ARES-REQ-RECORD-003 | format 1.0, CRC, sequence | `include/ares/recorder/format.hpp` | `Recording.GpsFailoverReplayIsByteStable`, `Recording.RecoveryFailureStaysVisible` | ARES-HZ-009 |
| ARES-REQ-REPLAY-001 | checked parse | `src/recorder/replay.cpp` | `Replay.RejectsEmptyAndTruncatedHeader`, `Replay.RejectsBadMagicAndUnsupportedMajor`, `Replay.RejectsChecksumTruncationLengthSequenceAndTrailingBytes`, `Replay.RejectsAMissingMissionEnd`, `Replay.RejectsAMissionEndThatPrecedesTheStart`, `Replay.RejectsAClearWithNoActivation` | — |
| ARES-REQ-REPLAY-002 | offline parser and CLI | `src/recorder/replay_main.cpp`, `src/recorder/replay.cpp` | Inspection: those files do not reference `TaskSupervisor`, `MissionRuntime`, or `ares::run`. `replay_main` calls `replay_bytes` and prints the report. `Replay.TextIsStable` checks report text only and is not evidence of this requirement. | — |
| ARES-REQ-ASSURANCE-001 | version header and format constants | `cmake/version.hpp.in`, `kFormatMajor` in `include/ares/recorder/format.hpp` | Test: `Application.ZeroDurationBootsAndShutsDown` expects `ARES 1.0.0`. `LaunchOptions.HelpDoesNotRequireAProgramName` expects `ARES v1.0.0`. `ResourceBudget.FixedTablesStayWithinASmallCeiling` expects format major 1 and minor 0. | — |
| ARES-REQ-ASSURANCE-002 | SplitMix64 | `include/ares/simulation/random_source.hpp` | Test: `DeterministicRng.SameSeedRepeats` repeats a seeded draw. Inspection: `include/` and `src/` contain no `std::random_device` call. The header states that the generator does not use it. The repeat test does not prove that absence. | — |

## Coverage of the requirement set

Every requirement in `REQUIREMENTS.md` appears in the table above. A verification cell that does not say otherwise is a test that asserts the named behavior. Cells that say Test and Inspection use the test only for the clause named Test, and the source reading only for the clause named Inspection. Structural coverage is a separate measurement in `VERIFICATION_REPORT.md`.

## Nonconformances

No requirement in this set is linked to an open defect. SWE-052 also asks for requirements-to-nonconformances traceability. The project has no defect database to attach. A later defect that affects a requirement should add a row here. Severity and closure rules are in `SOFTWARE_ASSURANCE_PLAN.md`.
