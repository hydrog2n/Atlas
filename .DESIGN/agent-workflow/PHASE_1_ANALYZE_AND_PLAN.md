# Phase 1 --- Analyze & Plan

## Agent Operating Rules

This file is the execution contract for this phase of an Atlas release.

-   The target release is the version specified by the user's
    invocation. Treat it as `[VERSION]`.
-   Consult the canonical Atlas Master Design Specification,
    `ROADMAP.md`, applicable ADRs, repository documentation, and actual
    repository state.
-   The Master Design Specification is the architectural authority. The
    roadmap defines release scope, sequencing, work packets, and gates.
    Existing code is evidence of behavior, not permission to redefine
    architecture.
-   Perform **only this phase**. Do not automatically continue into the
    next phase.
-   Follow applicable CORE requirements, acceptance criteria, release
    gates, explicit non-goals, and compatibility contracts.
-   Stop and report rather than guess when a mandatory stop condition or
    unresolved architectural decision is encountered.

## Objective

Prepare a technically sound implementation plan for Atlas
**\[VERSION\]**. **Do not modify repository files.**

## Actions

1.  Read the relevant specification, roadmap release/work packets,
    README, build documentation, ADRs, and repository guidance.
2.  Inspect source, tests, fixtures, CMake, dependencies, CI, version
    metadata, and existing implementation.
3.  Determine `[VERSION]` scope: roadmap packets, CORE requirements,
    acceptance criteria, release gates, dependencies, and explicit
    non-goals.
4.  Compare required scope with actual implementation; do not assume
    documentation and implementation are synchronized.
5.  Identify affected modules/interfaces, persistence/compatibility
    implications, tests, documentation impact, and likely files to
    change.
6.  Produce an ordered plan for the smallest complete vertical
    implementation without speculative future-release work.
7.  Map each planned change to its requirement/work packet and intended
    validation.

Flag changes to authoritative ownership, stable identity, units,
persisted meaning, migration guarantees, deterministic semantics, or
other CORE invariants. If requirements conflict or an architectural
decision is missing, stop and identify the required ADR/design decision.

## Phase Gate

Before implementation, establish what `[VERSION]` accomplishes,
in/out-of-scope work, applicable requirements/gates, contracts that must
remain unchanged, and required implementation/testing work.

## Required Completion Report

End this phase with:

``` text
Result: completed | blocked
Phase: 1 — Analyze & Plan
Version: [VERSION]

Repository State:
- Current version/state:
- Relevant existing implementation:

Release Scope:
- Objective:
- In scope:
- Explicit non-goals:
- Roadmap packets:
- CORE requirements:
- Acceptance criteria:
- Release gates:

Plan:
- Implementation steps:
- Tests required:
- Documentation/versioning impact:
- Persistence/compatibility impact:

Risks / Decisions:
- Stop conditions:
- ADR or design decision needed:

Phase Gate:
- Ready to implement: yes | no
- Reason:
```

Report only evidence established during this phase. Do not claim
implementation or validation that has not occurred.
