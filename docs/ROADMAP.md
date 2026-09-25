# Roadmap

## Done

- **v0.1** Deterministic flight-software foundation: scheduler, supervisor, deadline monitor, mode machine.
- **v0.2** Simulated spacecraft behind hardware interfaces.
- **v0.2.1** Sensor freshness, deterministic noise, checked integration arithmetic.
- **v0.3** Fault detection, isolation, and a bounded registry.
- **v0.3.1** Deadline escalation and SafeMode recovery.
- **v0.4** Deterministic chaos engine and named scenarios.
- **v0.5** Autonomous navigation restart and primary-to-backup GPS failover.
- **v0.6** Bounded flight recorder, format 1.0, and `ares-replay`.
- **v0.7** Release hardening: presets, CI, demos, install, and operator-facing output.

## Current

**v1.0** Final demonstration of that system. Four canonical missions, record and replay, and the documents a reviewer needs. No new spacecraft subsystem.

## Future work

Not part of v1.0, and not scheduled:

- communications dropout
- automatic GPS failback
- checkpoint rollback
- hardware-in-the-loop
