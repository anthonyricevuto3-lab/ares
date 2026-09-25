# Software hazard analysis exercise

ARES 1.0.0 is a workstation spacecraft-software simulator. It does not control a real spacecraft. This document is a software safety analysis exercise and an assurance artifact. It is not an approved vehicle or system hazard analysis, and it does not classify ARES as safety-critical under NASA-STD-8739.8B.

NASA-STD-8739.8B section 3.2 defines a hazard analysis as identifying and evaluating hazards and recommended mitigations, and a hazard control as a means of reducing the risk of exposure to a hazard. The effects below are effects inside the simulation: wrong mode, lost diagnostic evidence, or a stopped mission. They are not loss of crew or loss of a vehicle. Hypothetical effects on a real vehicle are out of scope and are not stated.

Each control traces to an ARES requirement. A named test is evidence only for the behavior that test asserts. An architectural boundary is evidence only when the status says code inspection.

## ARES-HZ-001

Navigation operates on stale or unavailable sensor data.

- Cause: a GPS sample is frozen or never arrives.
- Effect: a navigation solution built from a sample that is no longer fresh.
- Detection: freshness classification before the sample is used.
- Control: stale or unavailable becomes a fault. Persistent warning requests Degraded. Requirement ARES-REQ-NAV-001 and ARES-REQ-FDIR-001.
- Evidence: `SensorInjection.GpsFreezeBecomesStaleThroughFreshnessThenRestores`, `FaultPolicy.PersistentWarningRequestsDegraded`.
- Residual risk: a single warning is still Nominal by policy. That is intentional.
- Status: control verified in unit and injection tests.

## ARES-HZ-002

Navigation periodic execution repeatedly misses its deadline.

- Cause: work overrun or an injected execution delay.
- Effect: the task is late often enough that mode policy should leave Nominal.
- Detection: deadline monitor, then a consecutive-miss streak.
- Control: misses escalate advisory, warning, critical, then SafeMode. Requirements ARES-REQ-SCHED-002 and ARES-REQ-FDIR-003.
- Evidence: test `DeadlineEscalation.OneAndTwoMissesStayAdvisoryAndNominal`, `DeadlineEscalation.ThirdMissBecomesWarningAndMayDegrade`, and `DeadlineEscalation.FifthMissBecomesCriticalAndMayEnterSafeMode`. The fifth miss from Nominal enters SafeMode.
- Residual risk: one or two misses stay advisory on purpose.
- Status: control verified.

## ARES-HZ-003

Primary GPS fails and no verified navigation source is available yet.

- Cause: primary freshness or usability failure.
- Effect: navigation has no verified backup until three usable backup samples are counted.
- Detection: sensor fault and failover state in `RecoveryManager`.
- Control: isolate primary, select backup, verify, do not fail back. Requirements ARES-REQ-NAV-002 and ARES-REQ-REC-005.
- Evidence: `RecoveryManager.GpsFailoverVerifiesBackupAndDoesNotFailBack`.
- Residual risk: the interval before verification completes still has no confirmed backup.
- Status: control verified.

## ARES-HZ-004

Primary and backup GPS are both unusable.

- Cause: backup samples stay unusable after the switch.
- Effect: the single GPS switch fails. There is no third receiver.
- Detection: unusable-backup run length.
- Control: one terminal failure. Requirement ARES-REQ-REC-006.
- Evidence: `RecoveryManager.UnusableBackupFailsTheSingleSwitch`, `ChaosCampaign.DualGpsFailureEmitsOneTerminalFailure`.
- Residual risk: the simulation has no further navigation source. That is the product limit.
- Status: control verified.

## ARES-HZ-005

Recovery loops indefinitely.

- Cause: a restart is attempted again after both configured attempts failed.
- Effect: repeated task restarts without a terminal state.
- Detection: attempt count.
- Control: two attempts, then one RecoveryFailed, and no third attempt. `RecoveryFailed` is not an input to the process exit. Requirements ARES-REQ-REC-003 and ARES-REQ-REC-008.
- Evidence: test `RecoveryManager.TwoRestartAttemptsThenOneFailure` and `ChaosCampaign.HeldNavigationDelayFailsBothRestartAttempts` for the attempt cap. Test `ApplicationExit.RecoveryFailedLeavesSuccessWhenNoOtherFault` for the exit: recovery is `Failed`, and `combine_exit` stays `Success` when there is no worker fault and no miss overflow. Inspection: `finish_mission` does not read recovery state.
- Residual risk: a later miss-history overflow can still exit 9 after recovery has already failed. Operators must read the summary. That overflow is a separate rule.
- Status: attempt cap test-verified. Exit non-effect test-verified. Recorder override of a successful flight exit is code inspection, not this control.

## ARES-HZ-006

Fault state is cleared before recovery is verified.

- Cause: a clear or an early success mark.
- Effect: mode returns toward Nominal on unverified data.
- Detection: verification count and the rule that a clear is not a Nominal request.
- Control: three on-time completions from the new generation, and clearing a critical fault does not itself return to Nominal. Requirements ARES-REQ-REC-002 and ARES-REQ-FDIR-004.
- Evidence: `RecoveryManager.ThreeOnTimeCompletionsFromTheNewGenerationSucceed`, `FaultPolicy.ClearingCriticalDoesNotReturnToNominal`.
- Residual risk: none beyond the intentional persistence counts.
- Status: control verified.

## ARES-HZ-007

Recovery changes spacecraft mode directly and bypasses policy.

- Cause: `RecoveryManager` calls the mode machine.
- Effect: a transition that fault policy would have rejected.
- Detection: design constraint and the FDIR tests that keep Nominal blocked.
- Control: recovery emits events. `FdirController::apply` is the mode request. Requirement ARES-REQ-REC-007.
- Evidence: test `FdirController.FailedNavigationBlocksDegradedReturnToNominal` and `FdirController.SafeModeWaitsForNavigationVerification` for the policy result. Inspection: `include/ares/flight/recovery.hpp` does not reference `ModeMachine` or `request_mode`.
- Residual risk: a later edit could add a direct call. The coding standard forbids it. There is no automated architecture check.
- Status: policy result test-verified. The direct-call prohibition is code inspection.

## ARES-HZ-008

Chaos injection bypasses flight detection and writes fault state.

- Cause: the scenario engine calls the registry or the mode machine.
- Effect: a fault appears that no sensor or deadline path produced.
- Detection: injection tests observe the sensor report first.
- Control: chaos changes sensor values or task delay. Flight code records the fault when it observes the sensor. `ChaosEngine` does not call the registry, recovery, or the mode machine. Requirement ARES-REQ-CHAOS-003.
- Evidence: test `SensorInjection.GpsUnavailableIsReportedByTheSensorAndRecordedByFdir` for the sensor condition and the FDIR observation. Inspection: `include/ares/simulation/chaos_engine.hpp` includes clock and logger only.
- Residual risk: a new chaos action could call flight state if a later edit adds that dependency. The sensor test would not catch it.
- Status: sensor path test-verified. The write prohibition is code inspection, not a test.

## ARES-HZ-009

Recorder or observer failure changes authoritative flight behavior.

- Cause: the recorder throws into policy, or a full log blocks a mode change.
- Effect: flight mode follows the observer instead of the sensors.
- Detection: disabled-versus-recorded comparison, and the open-failure path.
- Control: the recorder is an observer. Open failure does not start workers. Requirements ARES-REQ-RECORD-001 and ARES-REQ-RECORD-002.
- Evidence: `Recording.DisabledRunMatchesARecordedRun`, `Application.RecordOpenFailureDoesNotStartAMission`.
- Residual risk: a recorder failure can still change the process exit when flight itself succeeded.
- Status: control verified for mode and fault policy. Exit-code coupling is documented.

## ARES-HZ-010

Integer or time overflow corrupts spacecraft truth, scheduling, or replay.

- Cause: an unrepresentable time step or an integration that overflows.
- Effect: a wrapped position or a schedule that jumps the wrong way.
- Detection: checked arithmetic.
- Control: reject unrepresentable time, saturate integration, and fail a generation that would wrap. Requirements ARES-REQ-CORE-004, ARES-REQ-CORE-005, and ARES-REQ-REC-004.
- Evidence: `CheckedTime.RejectsAStepPastTheRepresentableRange`, `SpacecraftIntegration.PositiveOverflowSaturates`, `RecoveryManager.GenerationOverflowFailsWithoutWrapping`.
- Residual risk: not every integer in the process is on a checked path. Logger formatting is outside this control.
- Status: control verified on the identified paths.

## ARES-HZ-011

Bounded history or storage exhaustion loses diagnostic evidence.

- Cause: more deadline misses than the 32-entry history.
- Effect: the oldest misses are overwritten and the process can exit `FaultHistoryOverflow`.
- Detection: the overflow flag on the bounded log.
- Control: the bound is visible. Worker faults still outrank overflow in `combine_exit`. Requirements ARES-REQ-SCHED-003 and ARES-REQ-CORE-002.
- Evidence: test `MissHistory.ProductionCapacityKeepsThirtyTwoThenOverflows` on `BoundedLog<DeadlineMissEvent<SteadyClock::time_point>, 32>`. Inspection: `MissionRuntime::misses_` in `src/application.cpp` is that type. Test `ApplicationExit.CombinePrefersWorkerFaultOverMissHistory` for the worker-fault priority. `BoundedLog.OverwritesTheOldestAndCountsOverflow` uses capacity 2 and is not this evidence.
- Residual risk: evidence older than the 32-entry window is gone. That is the bound.
- Status: capacity and overflow behavior test-verified on the production type. The member binding is code inspection.

## ARES-HZ-012

Shutdown or restart creates overlapping navigation task generations.

- Cause: a new worker starts before the previous one is joined, or stop leaves a thread running.
- Effect: two generations update navigation together.
- Detection: generation handoff and join on stop.
- Control: restart waits for the generation advance, and stop joins the worker. Requirements ARES-REQ-REC-001 and ARES-REQ-CORE-006.
- Evidence: `RecoveryManager.RestartStaysExecutingUntilTheGenerationAdvances`, `TaskSupervisor.StopUnblocksAWaitingTaskAndJoinsIt`.
- Residual risk: ThreadSanitizer was not run. The join is tested functionally.
- Status: control verified functionally. Concurrency sanitizer evidence is absent.

## Summary

Twelve modeled conditions were accepted. Each has a control. Runtime behavior is cited as a test. Architectural boundaries are cited as code inspection. None is closed in the sense of zero residual risk. The exercise does not authorize a safety-critical classification.
