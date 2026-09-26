# Source code inspection checklist

NASA-STD-8739.9 section 7.7.1 says a source-code checklist should address technical accuracy and completeness against requirements, implementation of the design, required standards, latent errors such as index, buffer, and divide-by-zero mistakes, traceability to requirements and to design, and the results of static or dynamic analysis when those results exist. Section 7.7.5 says the package should include the design and the static-analysis output.

The questions below include that minimum and the ARES-specific boundaries. A "no" is a candidate defect. It is not, by itself, a formatting change. Clerical layout issues are logged only when they block understanding.

## Requirements and design

- Does the code do what the cited ARES requirement says, including the threshold values in `fdir_limits.hpp`?
- Is behavior that the architecture describes either implemented or explicitly out of scope?
- Does the implementation match `DESIGN_ASSURANCE.md` and `docs/ARCHITECTURE.md`?
- Can the requirement and the design both be named for this change?

## Coding standard

- Does the change follow `CODING_STANDARD.md`?
- Are enumerations strongly typed and handled exhaustively?
- Are persistent and protocol fields explicit-width integers?
- Is a C++ struct dumped into a binary file? It must not be.

## Bounds and arithmetic

- Are indexes and buffer lengths checked before use?
- Can integer addition, subtraction, or multiplication overflow or underflow on this path?
- Is signed overflow relied on?
- Is there a division or remainder whose divisor can be zero?
- Is time arithmetic checked when a result might leave the representable domain?

## Data and lifetime

- Is any value read before it is initialized?
- Do pointers and references stay inside the lifetime of the object?
- Is ownership expressed with RAII, with no raw owning pointer and no detached thread?

## Concurrency and shutdown

- Is there a data race on this state?
- Is lock order documented where more than one mutex is held?
- Does the atomic handoff publish a complete value, including the navigation generation?
- Does shutdown join every worker this change starts?
- Can two navigation generations run together?

## Exceptions and resources

- Can an exception leave a worker thread?
- Can a flight-state container grow without a bound on the periodic path?
- Is dynamic allocation introduced on a periodic flight path? If so, is it one of the documented exceptions?

## State, faults, and recovery

- Are mode transitions only those the mode machine allows?
- Does a fault clear return the vehicle to Nominal by itself?
- Is detection performed on the sensor or deadline path rather than by writing the registry from chaos?
- Is recovery success counted only after the verification samples?
- Can recovery attempt another restart after `kNavigationRestartAttempts`?
- Does recovery call the mode machine directly?

## Boundaries

- Does flight code include a concrete simulated device?
- Does chaos write fault, recovery, or mode state?
- Can the recorder or another observer change mode or clear a fault?
- Does deterministic simulation call `std::random_device`?
- Are fault identities strings in flight state?

## Binary records

- Are parsed lengths bounded by the bytes actually present?
- Does the CRC cover the bytes that are trusted?
- Does replay re-enter the flight executive? It must not.

## Evidence

- What did clang-tidy report for this scope, and which findings are in project code?
- What did ASan or UBSan report on the last Linux run that applies to this scope?
- Which requirement-linked tests exercise this change?
- What is the cyclomatic complexity, and is a value above 15 explained?
- Is uncovered code classified as a missing requirement, a missing test, a defensive path, a platform path, or deactivated code?

## Analysis results to attach

- `ares-tidy` output for the scope, or a statement that tidy was not run
- coverage summary, if the change is flight code
- complexity rows for functions the change touches
