# Design assurance

NPR 7150.2D SWE-057 asks that requirements be transformed into a recorded architecture. SWE-058 asks for a design of the lower-level units that can be coded and tested. This note records the decisions already implemented in ARES 1.0.0. It does not redesign them.

The long-form description remains `docs/ARCHITECTURE.md`, `docs/FAULT_MODEL.md`, `docs/RECOVERY.md`, `docs/CHAOS_ENGINE.md`, `docs/FLIGHT_RECORDER.md`, `docs/REPLAY.md`, and `docs/MEMORY.md`.

## Simulation / flight boundary

`SpacecraftModel`, sensors, and `ChaosEngine` live on the simulation side. Flight code depends on hardware interfaces (`Gps`, `Imu`, `Power`, `Thermal`) and on the task supervisor. Flight code does not include the concrete simulated devices. Chaos may freeze a sensor sample or delay task execution. It does not call `FaultRegistry`, `RecoveryManager`, or `ModeMachine`.

## Ownership and concurrency

`TaskSupervisor` owns worker `std::jthread`s and joins them on stop. Tasks are not detached. `Health` / `FdirController` is the only writer of fault and recovery state. `FaultMailbox` is the handoff from producers to that writer: one pending sensor observation, a sticky deadline miss, and separate battery and task slots. Navigation publishes a generation. Recovery waits until that generation advances before it will count verification samples. A second navigation generation is not started until the previous worker has been joined.

`EventLog` and `FlightRecorder` observe. A recorder failure may change the process exit only when the flight result is Success (`RecorderFailed`). It does not change mode or fault policy.

## Bounded resources

Normal flight-state tables are fixed: fault registry 19, chaos schedule 16, chaos edges 32, event log 64, cycle log 64, miss log 32, recorder 256 with the last slot reserved for MissionEnd, recovery episodes 2. The logger may allocate a string per line. Replay may allocate while parsing. `snapshot()` allocates at shutdown. Those are outside the periodic flight-state tables.

## Mode and recovery legality

`RecoveryAction` is an input to mode policy. `RecoveryManager` does not call `ModeMachine`. `FdirController::apply` calls `FlightExecutive::request_mode`. SafeMode returns toward Standby only after the configured healthy cycles, and an open or failed navigation recovery blocks a return to Nominal. GPS failover isolates the primary and stays on the backup.

## Recording

The on-disk format is version 1.0 (`kFormatMajor` 1, `kFormatMinor` 0). Records are length-prefixed with an ISO-HDLC CRC. Sequence is the total order. Timestamps may move backward across producers. Replay rejects malformed input. It does not re-execute the flight software.

## Decisions that constrain later changes

- ManualClock and SteadyClock are different epochs. Tests that need a repeatable timeline use ManualClock.
- Fault and recovery identities are enumerations, not strings.
- Integer and time overflow paths saturate or reject. They do not wrap the spacecraft state.
- Host scheduling can miss a one-second chaos window. That is a property of SteadyClock demos, not of the ManualClock campaigns.
