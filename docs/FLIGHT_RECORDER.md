# Flight recorder

The flight recorder is an observer. It copies mission edges that flight software has already produced. It does not write the fault registry, the recovery manager, the mode machine, GPS selection, task restart, or the chaos engine. A full buffer or a failed write is an observability failure. It is not flight policy.

`EventLog` is the bounded in-memory operational history. It overwrites its oldest entry when full. `FlightRecorder` is the optional persistent copy. If the log drops an old event, the recorder can still hold that event if it already copied it. The recorder does not replace the log and is not authoritative.

## Ownership

`MissionRuntime` owns the recorder. It is declared before the task supervisor, so workers are joined before the recorder is destroyed. Producers are the health task (typed events, chaos edges, GPS failover), the navigation cycle hook (generation), and the main thread (mission start, mission end, and the file write). There is one recorder mutex. It covers the slot copy only. Disk I/O runs in `commit()` on the main thread after workers have stopped. The recorder does not take the fault mailbox, mode machine, or task supervisor lock, and it does not call a logging callback while its own lock is held.

## Bounded storage

The flight path keeps a fixed prefix of 256 slots. Append copies one 32-byte payload and is O(1). It does not allocate. The last slot is reserved for `MissionEnd`. A later non-end record is refused, `overflow` latches, and the ordered prefix stays. The sequence number does not wrap. Reaching the sequence limit latches the same overflow and refuses the record. Overflow does not change mode, fault state, or recovery state.

`encode()` builds the file image once, at finalization. That allocation is not on the per-event path. `commit()` writes that image. `ares-replay` may allocate because it is offline.

## File layout

Every recording starts with a 64-byte header. Every record is an explicit little-endian field encoding. The file does not dump C++ object memory. Format version 1.0 is separate from the ARES application version stored in the header. Replay accepts major 1 and minor 0 only. It does not reject a file because that application version changed.

```
+----------------------------------------------+
| File header, 64 bytes                        |
|  0  magic "ARESREC1"                         |
|  8  format major u16, minor u16              |
| 12  endian marker u32 = 0x01020304           |
| 16  ARES major, minor, patch u16             |
| 22  header size u16 = 64                     |
| 24  mission seed i64                         |
| 32  scenario name, 16 bytes, NUL padded      |
| 48  flags u32                                |
| 52  record count u32                         |
| 56  CRC32 of the record stream               |
| 60  CRC32 of bytes 0..59                     |
+----------------------------------------------+
| Record                                        |
|  0  type u16, schema u16 = 1                 |
|  4  payload size u16, reserved u16 = 0       |
|  8  ARES time ns i64                         |
| 16  sequence u32                             |
| 20  payload                                  |
|     CRC32 of prefix + payload                |
+----------------------------------------------+
```

Header flags: finalized = 1, overflow = 2, I/O error = 4. The scenario name keeps at most 15 characters. Replay does not use the filename.

## Record header and types

| Type | Value | Payload |
| --- | --- | --- |
| MissionStart | 1 | seed i64, name 16 |
| MissionEnd | 2 | mode, overflow, I/O error, finalized, exit i32, activation/clear/recovery counts |
| ModeChanged | 3 | from, to |
| FaultActivated | 4 | type, source, severity |
| FaultUpdated | 5 | type, source, severity, consecutive u32 |
| FaultCleared | 6 | type, source |
| RecoveryStarted | 7 | action, target, attempt, generation u32 |
| RecoverySucceeded | 8 | same |
| RecoveryFailed | 9 | same |
| ChaosStarted | 10 | kind, target, started, sequence u32, edge time ns i64 |
| ChaosEnded | 11 | same |
| DeadlineMiss | 12 | type, source, severity, consecutive u32 |
| TaskGenerationChanged | 13 | previous u32, generation u32 |
| GpsSelectionChanged | 14 | selection, isolated |

Schema 1 is required. An unknown type or a payload size other than the size for that type is rejected.

## Time, sequence, and checksums

Sequence number is the total order of records inside one file. It starts at 0 and strictly increases. It does not wrap. Replay prints records in that order and does not sort them by time.

The timestamp is the logical ARES time of that event, in nanoseconds. It is not the host wall clock, and it is not the serialization order. Navigation and health append from different threads, so a later sequence can carry an earlier logical time. This recording is valid:

```
sequence 40  Health FaultUpdated            5.250 s
sequence 41  Navigation TaskGenerationChanged  5.200 s
```

The generation stamp is the cycle's scheduled release. Chaos edges use the health observation time, and the scheduled edge time stays in the payload. Mode, fault, recovery, and GPS records keep the time those events already carried. A timestamp that moves backward between sequence numbers is not corruption.

`MissionStart` and `MissionEnd` are the mission endpoints. Duration is `MissionEnd` time minus `MissionStart` time. Replay rejects a finalized file whose end precedes its start. Intermediate records may sit outside that span.

A `ManualClock` campaign that appends in one thread, with the same scenario, produces the same timestamps and the same bytes. A `SteadyClock` mission may differ between processes, and its record order follows whichever producer appended first.

Each record carries an ISO-HDLC CRC32 of its prefix and payload. The header carries a CRC32 of the record stream and a CRC32 of the first 60 header bytes. The digest of `123456789` is `0xCBF43926`. Replay rejects a mismatch.

## I/O, shutdown, and exit status

`--record FILE` opens the file with `fopen` mode `wb+` before boot. That truncates an existing destination immediately. A missing path, a duplicate flag, or an empty path is a usage error. If the file cannot be opened, the process prints `record open failed` and exits 10 without starting workers. If the process stops after a successful open and before `commit`, the destination can be empty or incomplete. v0.6 does not replace the file atomically.

`finish_mission` seals `MissionEnd` with the final mode and the flight exit code, then writes the file. This runs for a normal end, SafeMode, a worker fault, a schedule fault, a shutdown miss, overflow, and a write failure. If the flight exit is success and the seal, write, or flush failed, or overflow latched, or I/O latched, the process exit becomes 10 (`RecorderFailed`). Any flight failure keeps the flight code. The spacecraft mode and the fault and recovery state are unchanged.

The header flags and the `MissionEnd` payload describe recorder state known when `encode` runs. `commit` does not encode again after `fwrite` or `fflush` fails, and it does not patch an `IoError` bit into bytes already written. A full `fwrite` followed by a failed `fflush` can leave a structurally valid finalized image on disk while the process still exits 10. The process exit is the result of the requested recording operation. Replay only checks the bytes it can read. It cannot prove that a previous process flushed those bytes to stable storage.

The last slot is reserved so `MissionEnd` still fits after ordinary records fill the prefix. That reservation does not bypass sequence exhaustion. If the sequence limit is already reached, `MissionEnd` is refused, the file stays unfinalized, and the sequence does not wrap. Production capacity is 256 records, so that limit is not reachable before the prefix overflows.

## Determinism

The same ordered records encode to the same bytes. The encoding uses explicit little-endian stores. It does not write pointers, padding, or host-formatted strings. Recording does not draw from the noise generator and does not read a sensor in order to record it. A recorded run and an unrecorded run of the same scenario keep the same fault, recovery, generation, GPS, and mode results.

## Limitations

Replay parses and checks the file. It does not restore C++ objects or re-run tasks. The buffer keeps a prefix; a long mission can drop later edges and still finalize. An overflowed prefix may contain an activation with no later clear, or a recovery start with no later result. A clear or a recovery result that appears with no earlier matching start is still rejected. Communications dropout, checkpoint rollback, automatic GPS failback, and atomic file replacement are not part of this recorder.
