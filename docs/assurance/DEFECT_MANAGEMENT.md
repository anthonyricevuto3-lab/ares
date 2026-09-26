# Defect and nonconformance management

This process addresses the intent of NPR 7150.2D SWE-201 (track nonconformances), SWE-202 (define severity), and SWE-204 (assess the process when severity is high). SWE-203, assessment of COTS and reused-component nonconformances before flight, is institutional and not applied. GoogleTest is a test harness, not flight software.

No historical defect database is constructed. Issues that are not recorded in this repository are not listed.

## How a defect is handled

1. Report it in the repository issue tracker or in the assurance report for this branch, with the failing command and the requirement ID if one exists.
2. Classify severity with the scale below.
3. Track it until it is fixed or explicitly accepted.
4. Resolve it with a change on a branch. Do not rewrite `v1.0.0`.
5. Verify the correction with the regression test named in the closure note.
6. Close it only when that test has been run and the result is recorded.

## Severity

ARES uses four engineering severities. They are not the same as the NASA-STD-8739.9 defect classes.

| ARES severity | Meaning on this simulator | Nearest NASA-STD-8739.9 class | Relationship |
| --- | --- | --- | --- |
| Critical | Corrupts flight state, breaks the simulation/flight boundary, or makes a recovery succeed when the policy says it failed | Major: a defect that would prevent a primary objective or system safety | Related. ARES has no system-safety objective, so Critical is about the simulator's own invariants. |
| High | Wrong mode, wrong exit, or an unbounded flight-state structure on a path the requirements name | Major | Same caution. High is still an ARES judgment. |
| Medium | Misleading operator output or a test that can pass for the wrong reason | Minor: a defect that would not prevent the primary objective | Often Minor. Some Medium items are only documentation. |
| Low | Wording, diagram, or portability notes | Clerical, when the issue does not change technical meaning | Not identical. A low technical defect is not clerical. |

NASA-STD-8739.9 section 6.3.6.7 defines Major and Minor in terms of mission objectives, system safety, and cost or schedule. Clerical defects are those that do not block understanding of the technical content. Do not relabel an ARES Critical as a NASA Major finding from an inspection that was not held.

The SWE-202 note asks, at a minimum, for classes covering loss of life or vehicle, mission success, user-visible with a workaround, and other. Those operational classes are not used. ARES does not fly.

## High-severity closure

A Critical or High defect is not closed until all of the following exist:

- a short root-cause note
- the corrective change
- a regression test that fails before the fix and passes after it
- a recorded verification run
- an update to the traceability row if a requirement was wrong or incomplete

Medium and Low items may be recorded and left for a later change when the fix is not trivial.

## This branch

No Critical or High defect was filed against the frozen flight code. Assurance gaps in `ASSURANCE_REPORT.md` are process gaps, not product defects, unless a row says otherwise.
