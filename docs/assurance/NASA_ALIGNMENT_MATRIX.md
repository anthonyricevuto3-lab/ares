# NASA alignment matrix

ARES applies selected NASA software engineering and software assurance practices with documented project-specific tailoring. This matrix is not an approved NASA requirements mapping matrix. NPR 7150.2D SWE-125 asks a NASA project manager to maintain a mapping against that NPR, and the note under SWE-125 says relief from an applicable requirement is granted by designated Technical Authorities. ARES has no Technical Authority. Status values here are project judgments.

Sources read for this matrix:

- NPR 7150.2D, effective 8 March 2022, Chapter 3 through Chapter 5. Page references below are the PDF page labels in `N_PR_7150_002D_.pdf` where the extract preserved them, otherwise the section number.
- NASA-STD-8739.8B, 8 September 2022, sections 1, 3.2, 4.3, and 4.4.
- NASA-STD-8739.9 Change 1, sections 1.2, 4, 5, 6, and 7.7. The standards catalog lists this document inactive.

Paraphrases are short. They are not the requirement text.

## Life cycle and planning

| ID | Section | Paraphrase | Status | ARES evidence |
| --- | --- | --- | --- | --- |
| SWE-013 | 3.1.3 | Develop, maintain, and execute software plans that cover the life cycle and address this NPR with approved tailoring. | Partially Implemented | This plan set covers the implemented simulator. Security plans and approved NASA tailoring are absent. |
| SWE-034 | 3.1.5 | Define and document software acceptance criteria. | Voluntarily Adopted | Acceptance criteria in `ASSURANCE_REPORT.md`. They are project criteria, not a NASA customer acceptance. |
| SWE-036 | 3.1.6 | Establish processes, documentation, electronic products, deliverables, and tasks, including the action required on receipt. | Partially Implemented | CMake, CI, docs, and this assurance set. No government receipt action exists. |
| SWE-037 | 3.1.7 | Define milestones at which developer progress is reviewed and audited. | Partially Implemented | `git tag --list` shows `v0.2.1`, `v0.3.0`, `v0.4.0`, `v0.5.0`, `v0.6.0`, `v0.7.0`, and `v1.0.0`, plus `docs/ROADMAP.md`. There is no `v0.1.0` tag. No NASA audit. |
| SWE-040 | 3.1.9 | Provide NASA with products, traceability, change tracking, nonconformances, and metrics in electronic form. | Institutional / Not Applicable to Independent Project | No NASA recipient. The git repository is the electronic record. |
| SWE-042 | 3.1.10 | Provide NASA with electronic access to modifiable source. | Institutional / Not Applicable to Independent Project | No NASA facility copy is required. Source is in this repository. |
| SWE-121 | 3.1.12 | Where approved, document tailored requirements in the controlling plans. | Voluntarily Adopted | This matrix is the tailoring record. Approval by a Technical Authority was not obtained and is not claimed. |
| SWE-125 | 3.1.13 | Maintain a requirements mapping matrix against this NPR. | Partially Implemented | This file. It has no Authority signature. |

## Requirements and traceability

| ID | Section | Paraphrase | Status | ARES evidence |
| --- | --- | --- | --- | --- |
| SWE-052 | 3.12.1 | Maintain bi-directional traceability among higher-level requirements, software requirements, hazards, design, code, verification, and nonconformances, with the depth depending on class. | Partially Implemented | `REQUIREMENTS_TRACEABILITY.md` for the ARES requirements defined on this branch. Class-based NASA depth is not assigned because ARES is not classified. |
| SWE-050 | 4.1.2 | Establish, capture, record, approve, and maintain software requirements. | Partially Implemented | `REQUIREMENTS.md` records them. There is no separate approval board. |
| SWE-051 | 4.1.3 | Analyze requirements flowed down from systems engineering, safety, reliability, and hardware design. | Partially Implemented | Requirements were derived from the implemented product and its own fault model. There is no NASA systems-engineering flow-down. |
| SWE-053 | 4.1.5 | Track and manage changes to software requirements. | Partially Implemented | Git history. Requirements added on this branch do not change frozen behavior. |
| SWE-054 | 4.1.6 | Identify inconsistencies among requirements, plans, and products and track them to closure. | Partially Implemented | `DEFECT_MANAGEMENT.md`. No open inconsistency was found that required a flight change. |
| SWE-055 | 4.1.7 | Validate that the software will perform as intended in the customer environment. | Partially Implemented | The customer environment is a workstation simulator. Demos and GoogleTest exercise that environment. There is no operational spacecraft environment. |

## Architecture, design, and implementation

| ID | Section | Paraphrase | Status | ARES evidence |
| --- | --- | --- | --- | --- |
| SWE-057 | 4.2.3 | Transform software requirements into a recorded software architecture. | Implemented | `docs/ARCHITECTURE.md` and `DESIGN_ASSURANCE.md`. |
| SWE-058 | 4.3.2 | Record a design of the lower-level units so they can be coded, compiled, and tested. | Implemented | Headers under `include/ares/` and the unit tests that name those types. |
| SWE-061 | 4.4.3 | Select, define, and adhere to coding methods, standards, and criteria. | Voluntarily Adopted | `CODING_STANDARD.md`, `.clang-format`, `.clang-tidy`, warnings as errors. |
| SWE-135 | 4.4.4 | Use static analysis during development and test to detect defects, software security issues, code coverage, and complexity. | Partially Implemented | clang-tidy and compiler warnings address defects and a subset of coding mistakes. They do not measure coverage or complexity. Coverage is gcovr. Complexity is lizard. Those are separate tools, recorded in `TOOLCHAIN_CONFIDENCE.md`. |
| SWE-062 | 4.4.5 | Unit test the software. | Implemented | `tests/unit/`. |
| SWE-186 | 4.4.6 | Assure that unit test results are repeatable. | Implemented | ManualClock and seeded RNG. Host SteadyClock demos are documented as not byte-repeatable. |
| SWE-136 | 4.4.8 | Validate and accredit tools required to develop or maintain the software. | Not Implemented | `TOOLCHAIN_CONFIDENCE.md` records confidence and cross-checks. **NASA tool accreditation not performed.** |

## Test, coverage, and regression

| ID | Section | Paraphrase | Status | ARES evidence |
| --- | --- | --- | --- | --- |
| SWE-065 | 4.5.2 | Establish and maintain test plans, procedures, tests, and reports. | Partially Implemented | `VERIFICATION_PLAN.md`, the GoogleTest sources, and `VERIFICATION_REPORT.md`. |
| SWE-066 | 4.5.3 | Test the software against its requirements. | Partially Implemented | Traceability maps each behavioral clause to a test that asserts it. Architectural clauses are marked Inspection and are not claimed as tests. Untested behavior that was never written as a requirement remains a gap, not hidden coverage. |
| SWE-068 | 4.5.5 | Evaluate test results and record the evaluation. | Implemented | `VERIFICATION_REPORT.md` for this branch. |
| SWE-189 | 4.5.9 | Select, implement, track, record, and report code coverage measurements. | Voluntarily Adopted | gcovr line, function, and branch coverage. Not MC/DC. |
| SWE-190 | 4.5.10 | Verify coverage by analysis of test execution. | Voluntarily Adopted | Counters come from executing `ctest` on the instrumented build. |
| SWE-191 | 4.5.11 | Plan and conduct regression testing so that previously integrated software is not broken and a security vulnerability is not introduced. | Implemented | CI jobs and local Debug/Release suites. Security regression is limited to what the existing tests and clang-tidy actually check. |

## Configuration management

| ID | Section | Paraphrase | Status | ARES evidence |
| --- | --- | --- | --- | --- |
| SWE-079 | 5.1.2 | Develop a software configuration management plan. | Voluntarily Adopted | `CONFIGURATION_MANAGEMENT_PLAN.md`. |
| SWE-080 | 5.1.3 | Track and evaluate changes to software products. | Implemented | Git history. `v1.0.0` is the frozen baseline. |
| SWE-081 | 5.1.4 | Identify configuration items and versions to control. | Implemented | CM plan list. |
| SWE-082 | 5.1.5 | Establish control levels and who may authorize and make changes. | Partially Implemented | Maintainer authority. No NASA change board. |
| SWE-083 | 5.1.6 | Maintain records of configuration status. | Implemented | Tags and `git status` against `v1.0.0`. |
| SWE-084 | 5.1.7 | Audit configuration items against the records that define them. | Partially Implemented | This branch compares the tree to tag `v1.0.0` and records the result in the assurance report. |
| SWE-085 | 5.1.8 | Establish procedures for storage, handling, delivery, release, and maintenance of deliverables. | Partially Implemented | Annotated tags, CPack, and `docs/RELEASE_NOTES.md`. No NASA delivery. |

## Risk, defects, and review

| ID | Section | Paraphrase | Status | ARES evidence |
| --- | --- | --- | --- | --- |
| SWE-086 | 5.2 | Record, analyze, plan, track, control, and communicate software risks and mitigations. | Voluntarily Adopted | `RISK_REGISTER.md`. Likelihood and impact are qualitative. |
| SWE-201 | 5.5.1 | Track and maintain software nonconformances, including tool and ground-software defects. | Voluntarily Adopted | `DEFECT_MANAGEMENT.md`. No fabricated historical database. |
| SWE-202 | 5.5.2 | Define severity levels for nonconformances. | Voluntarily Adopted | Critical, High, Medium, Low. These are not the NASA loss-of-life classes in the SWE-202 note. |
| SWE-203 | 5.5.3 | Assess reported nonconformances in COTS, GOTS, MOTS, OSS, and reused components. | Institutional / Not Applicable to Independent Project | No flight-software COTS component. GoogleTest is test-only. |
| SWE-204 | 5.5.4 | Perform process assessments for all high-severity nonconformances. | Voluntarily Adopted | Required by the defect process when a Critical or High item is closed. None is open on this branch. |
| SWE-087 | 5.3.2 | Perform and report peer reviews or inspections of requirements, plans, selected design, code, and test procedures. | Not Implemented | Checklist and plan exist. A human inspection team has not executed them. |
| SWE-088 | 5.3.3 | For each review, use a checklist, readiness and completion criteria, action tracking, and identified participants. | Deferred | Defined in `INSPECTION_PLAN.md` for a future inspection. |
| SWE-089 | 5.3.4 | Record measurements for each planned review or inspection. | Deferred | Template fields exist. They are blank until a human inspection is held. |

## Safety-critical and IV&V items that are not claimed

| ID | Source | Why it is not claimed |
| --- | --- | --- |
| SWE-219 | NPR 7150.2D 3.7.4 | 100 percent MC/DC applies if the project has safety-critical software. ARES is not formally classified. MC/DC was not measured. |
| SWE-220 | NPR 7150.2D 3.7.5 | Cyclomatic complexity of 15 or lower, with waiver by technical approval authority, applies to identified safety-critical components. ARES uses 15 only as a voluntary review threshold. |
| SASS-01 | NASA-STD-8739.8B 4.3 | The project manager performs assurance activities per the NPR matrix, and the Center SMA Director designates Technical Authority. Neither role exists here. |
| SWE-141 / section 4.4 | NASA-STD-8739.8B 4.4 and NPR 7150.2D | IV&V is required for specified NASA project categories. ARES is not such a project. **IV&V NOT PERFORMED.** |
| SFI-001 through SFI-024 | NASA-STD-8739.9 | Formal inspection, trained moderator, and a team of at least three were not executed. |

## Hazard-analysis note

NASA-STD-8739.8B section 3.2 defines hazard analysis as identifying and evaluating hazards and recommended mitigations, and it says software is safety-critical when that determination is traceable to a hazard analysis. Appendix guidance in that standard is aimed at system hazard reports. `HAZARD_ANALYSIS.md` follows the shape of that practice for modeled simulator conditions. It does not create a NASA hazard report and does not classify ARES as safety-critical.
