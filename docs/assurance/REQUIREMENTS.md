# ARES software requirements

These requirements are owned by ARES. They describe the frozen 1.0.0 simulator. They are not NASA requirements. Each statement is written so a test can fail it. Thresholds match `include/ares/flight/fdir_limits.hpp`. The post-release assurance assessment did not change them.

Higher-level labels below are assurance-derived. They are not identifiers in `docs/PRODUCT_SPEC.md`. They name goals that document already states.

- OBJ-SIM: a workstation simulation with a flight/simulation boundary (Product Spec, Problem and Boundaries).
- OBJ-FDIR: detect the modeled faults and change mode only through policy (Product Spec, Functional behavior).
- OBJ-REC: verify navigation restart and GPS failover, then stop after the configured attempts (Product Spec, Functional behavior).
- OBJ-OBS: record a mission and check it without the recorder steering flight policy (Product Spec, Functional behavior and Known limitations).

## Core

| ID | Requirement | Objective |
| --- | --- | --- |
| ARES-REQ-CORE-001 | The process exit code is one of the stable codes Success (0), BootFailed (1), UsageError (2), WorkerException (3), ScheduleFault (4), HookFault (5), UnknownWorkerFault (6), StartupFailed (7), TimeError (8), FaultHistoryOverflow (9), or RecorderFailed (10). | OBJ-SIM |
| ARES-REQ-CORE-002 | When a worker fault and miss-history overflow are both present, the process exit is the worker fault. | OBJ-SIM |
| ARES-REQ-CORE-003 | An exception thrown by a task is contained at the worker boundary and stops the supervisor. It does not escape as an uncaught exception that aborts the process without a flight exit code. | OBJ-SIM |
| ARES-REQ-CORE-004 | A time step that cannot be represented in the simulation domain is rejected. | OBJ-SIM |
| ARES-REQ-CORE-005 | Spacecraft position integration saturates at the ends of the representable range instead of signed overflow. | OBJ-SIM |
| ARES-REQ-CORE-006 | Shutdown unblocks a waiting task and joins it. No task thread is detached. | OBJ-SIM |

## Scheduling

| ID | Requirement | Objective |
| --- | --- | --- |
| ARES-REQ-SCHED-001 | Work that finishes by its deadline is on time and arms the next period. | OBJ-FDIR |
| ARES-REQ-SCHED-002 | Work that overruns the deadline is a miss. | OBJ-FDIR |
| ARES-REQ-SCHED-003 | The navigation miss history retains at most 32 entries. Further misses are overflow, not silent growth. | OBJ-FDIR |

## Navigation and sensors

| ID | Requirement | Objective |
| --- | --- | --- |
| ARES-REQ-NAV-001 | A frozen primary GPS sample becomes stale through the freshness check. It is not treated as a fresh fix. | OBJ-FDIR |
| ARES-REQ-NAV-002 | After failover, the navigation solution uses the selected backup sample. | OBJ-REC |
| ARES-REQ-NAV-003 | Primary and backup GPS streams do not draw each other's noise. | OBJ-SIM |

## FDIR

| ID | Requirement | Objective |
| --- | --- | --- |
| ARES-REQ-FDIR-001 | One warning does not leave Nominal. Three consecutive warnings request Degraded. | OBJ-FDIR |
| ARES-REQ-FDIR-002 | A critical fault requests SafeMode from Nominal or Degraded. | OBJ-FDIR |
| ARES-REQ-FDIR-003 | One or two navigation deadline misses stay advisory and Nominal. The third is a warning and requests Degraded. The fifth is critical and, from Nominal, requests and enters SafeMode. | OBJ-FDIR |
| ARES-REQ-FDIR-004 | Clearing a critical fault does not itself return the mode from SafeMode to Nominal. | OBJ-FDIR |
| ARES-REQ-FDIR-005 | A saturated fault registry requests SafeMode. | OBJ-FDIR |
| ARES-REQ-FDIR-006 | The active fault table holds the production identities without growing past the fixed registry. | OBJ-FDIR |

## Recovery

| ID | Requirement | Objective |
| --- | --- | --- |
| ARES-REQ-REC-001 | Navigation restart stays in progress until the task generation advances. | OBJ-REC |
| ARES-REQ-REC-002 | Recovery succeeds only after three on-time completions from the new generation. | OBJ-REC |
| ARES-REQ-REC-003 | Navigation restart is attempted at most twice. The second failure is one RecoveryFailed and does not start a third attempt. | OBJ-REC |
| ARES-REQ-REC-004 | A generation counter that cannot advance fails the recovery without wrapping. | OBJ-REC |
| ARES-REQ-REC-005 | GPS failover verifies the backup and does not return to the primary. | OBJ-REC |
| ARES-REQ-REC-006 | An unusable backup fails the single GPS switch. | OBJ-REC |
| ARES-REQ-REC-007 | SafeMode recovery waits for navigation verification, then Standby. It does not resume Nominal while navigation recovery is open or failed. | OBJ-REC |
| ARES-REQ-REC-008 | RecoveryFailed does not by itself change a successful flight exit into a failure exit. | OBJ-REC |

## Chaos

| ID | Requirement | Objective |
| --- | --- | --- |
| ARES-REQ-CHAOS-001 | An unknown scenario name is rejected before a mission starts. | OBJ-SIM |
| ARES-REQ-CHAOS-002 | Two ManualClock runs of the same scenario and seed produce the same fault and mode trace. | OBJ-SIM |
| ARES-REQ-CHAOS-003 | A GPS-unavailable injection is reported by the sensor, and FDIR records it when flight code observes that sensor. `ChaosEngine` does not call `FaultRegistry`, `RecoveryManager`, or `ModeMachine`. | OBJ-FDIR |
| ARES-REQ-CHAOS-004 | A held navigation execution delay fails both restart attempts. A short delay restarts once and then verifies. | OBJ-REC |

## Recorder and replay

| ID | Requirement | Objective |
| --- | --- | --- |
| ARES-REQ-RECORD-001 | A recorded run and a disabled-recorder run of the same scenario agree on flight outcome. | OBJ-OBS |
| ARES-REQ-RECORD-002 | Opening a recording that cannot be created does not start workers and returns RecorderFailed. | OBJ-OBS |
| ARES-REQ-RECORD-003 | A GPS failover recording replays byte-stable, and a recovery-failure recording still shows the failure. | OBJ-OBS |
| ARES-REQ-REPLAY-001 | Replay rejects an empty or truncated header, a bad magic or unsupported major version, checksum and sequence errors, trailing bytes, a missing MissionEnd, a MissionEnd that precedes the start, and a clear with no activation. | OBJ-OBS |
| ARES-REQ-REPLAY-002 | Replay checks the recording. `ares-replay` does not start `TaskSupervisor` or the mission runtime. | OBJ-OBS |

## Assurance constraints

| ID | Requirement | Objective |
| --- | --- | --- |
| ARES-REQ-ASSURANCE-001 | The application version string of this baseline is 1.0.0. The recording format major.minor remains 1.0. | OBJ-SIM |
| ARES-REQ-ASSURANCE-002 | The deterministic noise source repeats for the same seed. Flight and simulation sources do not call `std::random_device`. | OBJ-SIM |
