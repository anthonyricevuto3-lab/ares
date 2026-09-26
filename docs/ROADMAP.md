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

## Current

**v0.7** Release hardening: debug and release presets, an ASan/UBSan preset, CI, scenario listing, a mission summary, demo scripts, install and package rules, and the documents a reviewer needs. No new flight subsystem.

## Next

**v1.0** A final demonstration release of the behavior frozen above: the same recovery actions, the same recorder, and a documented mission a reviewer can run from a clean build.

Communications dropout, automatic GPS failback, and checkpoint rollback are not scheduled. They stay out of the product until a later milestone defines them.
