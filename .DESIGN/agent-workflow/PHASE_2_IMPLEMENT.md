# Phase 2 --- Implement

## Agent Operating Rules

This file is the execution contract for this phase of an Atlas release.

-   The target release is the version specified by the user's
    invocation. Treat it as `[VERSION]`.
-   Consult the canonical Atlas Master Design Specification,
    `ROADMAP.md`, applicable ADRs, repository documentation, actual
    repository state, and the approved Phase 1 plan.
-   The Master Design Specification is the architectural authority. The
    roadmap defines release scope, sequencing, work packets, and gates.
-   Perform **only this phase**. Do not automatically continue into
    validation or release integration.
-   Follow applicable CORE requirements, acceptance criteria, explicit
    non-goals, and compatibility contracts.
-   Stop and report rather than guess when a mandatory stop condition or
    unresolved architectural decision is encountered.

## Objective

Implement Atlas **\[VERSION\]** according to the approved Phase 1 plan.

## Actions

1.  Implement the smallest complete vertical scope required by
    applicable roadmap packets and specification requirements.
2.  Preserve architectural invariants, stable identity, deterministic
    behavior, module boundaries, and authoritative-source
    vs. derived-data separation.
3.  Respect persistence, migration, compatibility, validation,
    ownership, and dependency contracts.
4.  Add/update tests alongside implementation and run focused tests as
    components are completed.
5.  Document important interfaces and add comments explaining **why**:
    invariants, ownership, units, determinism, compatibility,
    state/lifetime assumptions, and non-obvious algorithms.
6.  Keep changes narrowly scoped. Do not introduce unrelated refactors,
    speculative infrastructure, or future-release functionality.
7.  Stop if implementation exposes an architectural ambiguity or
    mandatory stop condition.

Do **not** perform final release-version propagation, changelog updates,
or release-status promotion; those belong to Phase 4. Do not weaken
architecture or tests to simplify implementation.

## Phase Gate

Ready for validation when planned functionality is implemented, required
tests are present, focused tests pass, and no known architectural
conflict remains. This means **implementation complete**, not **release
complete**.

## Required Completion Report

End this phase with:

``` text
Result: completed | partial | blocked
Phase: 2 — Implement
Version: [VERSION]

Implementation:
- Scope implemented:
- Roadmap packets addressed:
- CORE requirements addressed:
- Source files/components changed:
- Tests added/updated:
- Documentation/comments added/updated:
- Persistence/compatibility impact:

Focused Checks:
- Checks/tests run:
- Result:

Issues:
- Deviations from Phase 1:
- Remaining implementation work:
- ADR or design decision needed:

Phase Gate:
- Ready for validation: yes | no
- Reason:
```

Do not claim full release validation or release readiness during this
phase.
