# Inspection plan

NPR 7150.2D SWE-087 asks for peer reviews or inspections of requirements, plans, selected design, code, and test procedures. SWE-088 asks for a checklist or reading technique, readiness and completion criteria, action tracking, and identified participants. SWE-089 asks for measurements. NASA-STD-8739.9 section 4 describes the stages Planning, Overview, Preparation, Inspection meeting, Rework, and Follow-up (the standard's section 6.3).

This plan is ready for a future human team. It does not record a completed inspection.

NASA-STD-8739.9 SFI-006 says the team shall be at least three inspectors, including a moderator and the author. SFI-005 says the moderator shall have formal NASA inspection training and prior participation as an inspector. SFI-008 names the roles Author, Moderator, Reader, and Recorder. None of those conditions is met on this branch. An AI-assisted read of the source is not a NASA Formal Inspection and is not SFI compliance.

## Entry criteria

All of the following are true before a meeting is scheduled:

- the scoped code builds
- the Debug suite passes
- the Release suite passes
- `ares-format-check` passes
- static analysis output is available (`ares-tidy`)
- the requirements and design references for the scope are identified
- the inspection scope is frozen
- the source version and commit are recorded

NASA-STD-8739.9 SFI-013 says the moderator ensures entry criteria are met. SFI-014 says a product that fails entry goes back to the author.

## Stages

1. Planning. The moderator, once a person is assigned, selects the scope, the package, and the inspectors. Suggested rate from section 7.7.4 is at most 10 pages of source per hour. Do not schedule the whole repository as one meeting.
2. Overview. Hold only if the moderator decides the inspectors need it (section 6.3.4).
3. Preparation. Each inspector reads the package against `SOURCE_CODE_INSPECTION_CHECKLIST.md` and sends defects before the meeting.
4. Inspection meeting. The reader walks the product. Discussion stays on defects. The recorder logs them. Classification uses Major, Minor, and Clerical as defined in NASA-STD-8739.9 section 6.3.6.7, and also records the ARES severity from `DEFECT_MANAGEMENT.md`. The two scales stay visible because they are not the same.
5. Rework. The author corrects every Major defect (SFI-024). Other defects are fixed when the moderator and the maintainer agree.
6. Follow-up. The moderator checks that Major defects are corrected and that the exit criteria hold before the product is treated as inspected.

## Exit criteria

- every Major finding is corrected
- required Medium, Minor, and Clerical dispositions are written down
- corrective changes are verified
- regression tests pass
- the inspection report is complete
- follow-up is complete

## Measurements to record

Section 8.1 of NASA-STD-8739.9 lists product size, versions, inspector roles, meeting duration, preparation time, and defect counts by Major and Minor. The template has those fields. They stay blank until a meeting happens. This branch does not invent those numbers.

## This branch

The writing of these artifacts included an AI-assisted assurance review of the frozen architecture and the requirements set. Call it a NASA-STD-8739.9-inspired source code review. Do not call it an inspection that passed.
