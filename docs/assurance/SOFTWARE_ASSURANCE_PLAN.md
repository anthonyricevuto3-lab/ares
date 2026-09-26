# ARES Software Assurance Plan

This is an independent educational alignment exercise. It has not been approved by NASA. ARES applies selected NASA software engineering and software assurance practices with documented project-specific tailoring.

## Purpose

Record how ARES 1.0.0 is checked against selected practices in:

- NPR 7150.2D, *NASA Software Engineering Requirements*, effective 8 March 2022 (NODIS PDF `N_PR_7150_002D_.pdf`)
- NASA-STD-8739.8B, *Software Assurance and Software Safety Standard*, approved 8 September 2022
- NASA-STD-8739.9, *Software Formal Inspections Standard*, approved 17 June 2013 with Change 1 dated 7 October 2016

The standards catalog page for NASA-STD-8739.9 lists that document as inactive. This plan uses it as a source of inspection practice, not as a current mandatory NASA standard.

## Scope

Baseline: annotated tag `v1.0.0` at commit `fcb6eda24fbeaf5e42c0f403ba977af1e427c944`. This branch is `assurance/nasa-alignment`. It is not ARES v1.1 and does not add spacecraft features. The `v1.0.0` tag is not moved.

In scope: requirements derived from the implemented simulator, traceability, design notes, a C++20 coding standard, verification records, configuration management, toolchain confidence, a software-safety exercise, a risk register, defect handling, an inspection plan ready for a future human team, and measured metrics.

Out of scope: flight qualification, a new fault family, threshold changes, recording-format changes, and any claim of NASA approval.

## Applicability

NPR 7150.2D states that it applies to software created, acquired, or maintained by or for NASA to the extent specified in a contract, grant, or agreement, and that applicability inside NASA uses the software classes in its Appendix D. NASA-STD-8739.8B likewise applies to software created by or for NASA, and to other parties to the extent specified in their agreements. ARES has no NASA contract, no Center, no Safety and Mission Assurance organization, and no Technical Authority.

Institutional requirements that need those organizations are classified **Institutional / Not Applicable to Independent Project**. Selected engineering practices are **Voluntarily Adopted** or **Partially Implemented**. The mapping is `NASA_ALIGNMENT_MATRIX.md`. Tailoring is recorded there. There is no Technical Authority signature, which SWE-121 and SWE-125 require for relief from an applicable NASA requirement. That signature is not available, so the matrix is a project tailoring record, not an approved NASA requirements mapping matrix.

## Roles

| Role | Who | Limit |
| --- | --- | --- |
| Maintainer | Repository author | Owns the baseline, reviews, and release tags |
| Assurance author | This branch | Writes artifacts and runs the tools named in `METRICS.md` |
| Future moderator, reader, recorder, inspectors | Not assigned | Required before any inspection under NASA-STD-8739.9 can be claimed |
| NASA IV&V | None | IV&V was not performed |

NASA-STD-8739.8B defines Independent Verification and Validation as verification and validation by an organization that is technically, managerially, and financially independent of the development organization. Neither the author reviewing this code nor an assistant reviewing it for the author meets that definition. **IV&V NOT PERFORMED.**

## Requirements, design, and implementation

Software requirements are ARES-owned statements in `REQUIREMENTS.md`. They describe the system that exists. They are not copies of SWE or SASS statements. Traceability is in `REQUIREMENTS_TRACEABILITY.md`. Design evidence is `DESIGN_ASSURANCE.md` plus the existing architecture documents. Implementation rules are `CODING_STANDARD.md`.

## Verification

`VERIFICATION_PLAN.md` separates unit tests, integration tests, fault-injection campaigns, regression, sanitizers, static analysis, record/replay checks, coverage, complexity, and the release demos. `VERIFICATION_REPORT.md` records what this branch actually ran.

## Configuration, risk, and defects

Controlled items and the frozen tag are in `CONFIGURATION_MANAGEMENT_PLAN.md`. Risks are qualitative and live in `RISK_REGISTER.md`. Future defects follow `DEFECT_MANAGEMENT.md`. No historical defect database is invented.

## Hazard analysis

`HAZARD_ANALYSIS.md` is a software safety analysis exercise for a workstation simulator. It is not an approved vehicle or system hazard analysis. ARES 1.0.0 does not control a real spacecraft.

## Safety-critical applicability

NASA-STD-8739.8B defines safety-critical software as software that causes or contributes to a system hazard, controls or mitigates one, controls a safety-critical function, mitigates damage, or detects and corrects a potentially hazardous state, when that determination is traceable to a hazard analysis. NPR 7150.2D section 3.7 then adds further requirements, including SWE-219 (100 percent MC/DC for identified safety-critical components) and SWE-220 (cyclomatic complexity of 15 or lower for those components, with waiver by the project manager or technical approval authority).

ARES does not control physical hardware. No actual system hazard analysis has classified it. No NASA Technical Authority has classified it. NASA safety-critical requirements are therefore not formally invoked. Selected practices may be applied voluntarily as a stronger engineering target.

This branch does **not** claim:

- official safety-critical classification
- SWE-219 or 100 percent MC/DC
- SWE-220 cyclomatic-complexity compliance
- NASA Technical Authority approval
- NASA IV&V
- NASA Formal Inspection or SFI compliance

Cyclomatic complexity is measured against a voluntary review threshold of 15 for flight-side functions. That threshold is the same number SWE-220 uses. Using the number is not a compliance claim.

## Peer review

`INSPECTION_PLAN.md` and `SOURCE_CODE_INSPECTION_CHECKLIST.md` are ready for a future human team. NASA-STD-8739.9 section 5.2 requires a team of at least three inspectors, including a moderator and the author, and trained moderators (SFI-005, SFI-006). This branch does not have that team. The review performed while writing these artifacts is an **AI-assisted assurance review** and a **NASA-STD-8739.9-inspired source code review**. It is not a NASA Formal Inspection.

## Metrics and release gates

`METRICS.md` lists only values produced by a named tool and command. Acceptance criteria for this branch are in `ASSURANCE_REPORT.md`. NASA approval is not an acceptance criterion.

## Claims explicitly not made

ARES is not NASA compliant, NASA certified, NASA approved, flight qualified, DO-178C compliant, formally NASA IV&V'd, or the subject of a completed NASA Software Formal Inspection. NASA tool accreditation was not performed (see `TOOLCHAIN_CONFIDENCE.md`, which addresses the intent of SWE-136 without claiming accreditation).
