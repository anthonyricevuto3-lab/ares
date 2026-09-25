# Replay

`ares-replay` reads one recording and does not start `MissionRuntime`. It treats the file as untrusted input.

```
ares-replay FILE
ares-replay --summary FILE
ares-replay --verify FILE
```

The default prints the timeline and then the summary. `--summary` prints the summary only. `--verify` prints `valid` or `invalid: <reason>` and exits 0 or 1. A usage error exits 2. A file that cannot be read prints `record open failed` and exits 1. Running the tool twice on the same file prints the same text. The text does not include the host path.

## Parser

Every step checks that the requested bytes remain. Offset plus size uses checked addition and does not wrap. The parser rejects:

- an empty file
- a header shorter than 64 bytes
- a magic other than `ARESREC1`
- a format other than 1.0
- an endian marker other than `0x01020304`
- a header, record, or payload checksum mismatch
- a truncated record
- a payload length that is not the length for that type
- an unknown record type or a schema other than 1
- a declared count above 1,000,000 or a count that cannot fit
- bytes after the last declared record
- a file that is not finalized
- the I/O-error flag

An overflow flag does not reject the file. The prefix is still checked. A finalized overflow prefix may end while a fault is still active or a recovery is still open, because later records were refused. A clear with no earlier activation, or a recovery result with no earlier start, is still invalid.

Replay does not know whether the process that wrote the file flushed it. A file that passes these checks is a valid image of the bytes that were read. The writer process exit is what says the recording operation succeeded.

## Timeline

Times print as `T+SS.ffffff` or `T-SS.ffffff`, with six digits of microseconds. The kind is `MISSION`, `MODE`, `FAULT`, `RECOVERY`, `CHAOS`, `TASK`, or `GPS`. Lines follow sequence order. Replay does not sort by timestamp, so a later line can show an earlier time. The line under each time is taken from the typed payload. A chaos line uses the record timestamp, which is the observation time. The scheduled edge time is in the payload and is not the printed time.

## Validation

After the bytes check out, replay checks the mission:

- sequence numbers strictly increase
- a decreasing timestamp between sequence numbers is allowed
- `MissionStart` is first and occurs once
- `MissionEnd` occurs once and is last
- `MissionEnd` time does not precede `MissionStart` time
- duration is that endpoint span, and it must fit in a signed 64-bit count
- a mode change is a known legal transition
- a fault identity is in the closed type and source sets
- `FaultActivated` requires the identity to be inactive
- `FaultUpdated` and `FaultCleared` require it to be active
- `DeadlineMiss` does not itself toggle that table
- `RecoverySucceeded` and `RecoveryFailed` follow a `RecoveryStarted` for the same action and target; a later start supersedes an open attempt
- `TaskGenerationChanged` moves to a greater generation
- a GPS selection of backup counts as a failover

`MissionEnd` is a summary. Earlier records remain the history. A missing `MissionEnd`, or a finalized flag with no end record, is incomplete and fails.

## Summary

```
records
duration_ns
final_mode
fault_activations
fault_clears
recovery_starts
recovery_successes
recovery_failures
task_restarts
gps_failovers
exit_code
integrity
overflow
```

`task_restarts` counts `RecoveryStarted` for `RestartTask`. `gps_failovers` counts a backup selection record. `integrity` is `ok` or `failed`. A failed replay also prints `error`.

## What a campaign shows

A `gps_stale` recording contains, in order, mission start, the primary GPS freeze, the primary stale activation, `RecoveryStarted` for `SwitchSensor`, the backup selection, `RecoverySucceeded`, and mission end. `Nominal -> Degraded` and the later return to `Nominal` are in the mode records. The selection is recorded when failover happens, which is before verification finishes.

A one-second navigation delay that reaches `SafeMode` contains the deadline activation, `Degraded -> SafeMode`, the generation change, `RecoveryStarted` for `RestartTask`, the deadline clear, `RecoverySucceeded`, `SafeMode -> Standby`, and mission end. The clear can precede success: the first on-time completion clears the fault while verification still needs three on-time completions. The generation record is the navigation cycle that adopts the new generation, which is before the health cycle publishes `RecoveryStarted`.

The shipped `deadline_storm` schedule keeps the delay up long enough that both restart attempts fail. That recording shows `RecoveryFailed`, then the fault clear after the delay ends, then `SafeMode -> Standby` once the failed episode resets because the fault is inactive. A recording of a delay that lasts through both attempts shows `RecoveryStarted`, `RecoveryFailed`, `SafeMode`, and mission end.

## Corruption

Replay returns a reason and does not crash on a bad magic, an unsupported major version, a one-byte payload change, a truncated record, a truncated header, an illegal payload length, a sequence that moves backward, a checksum mismatch, a missing `MissionEnd`, or bytes appended after a finalized end.
