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

## Scope Control and Release Ledger

Before declaring the Phase 1 gate ready, create a release-scope ledger from
the exact roadmap section for `[VERSION]`. The ledger is the controlling
contract for Phases 2 through 4.

- List every roadmap work packet, ship-in-version item, explicit non-goal,
    CORE requirement, acceptance criterion, release gate, and compatibility
    fixture required by `[VERSION]`.
- For each item, record its owner, planned implementation files or module,
    required test or evidence, and one of: `planned`, `implemented`,
    `validated`, or `blocked`.
- Mark an item `validated` only when an executable check, retained fixture,
    or documented evidence demonstrates the exact requirement. Passing nearby
    tests is not evidence for a missing packet or gate.
- Do not collapse partial packet work into a complete release claim. A packet
    is complete only when every objective, dependency, test obligation, and
    release-gate contribution listed by the roadmap is implemented and
    evidenced.
- Treat the roadmap's version-completion statement as a hard gate. If any
    listed packet, gate, compatibility fixture, or earlier-release regression
    is incomplete, set the Phase 1 gate to `Ready to implement: no` for release
    completion, or explicitly label the plan as an incremental sub-slice that
    cannot promote the release.
- Separate two decisions in the report: `ready to implement` for the planned
    work packet and `ready for release integration` for the entire version.
    Phase 2 may implement a useful vertical slice, but it must not imply that
    the roadmap version is complete unless the ledger is fully validated.
- Do not begin Phase 4 from a green build alone. Phase 4 must reconcile the
    ledger, implementation, tests, fixtures, documentation, and release gates;
    any mismatch is a failed release gate and requires another scoped Phase 2
    and Phase 3 cycle.
- Keep the ledger in the Phase 1 report and development log. Later phases may
    append status and evidence, but must not silently rewrite the original
    scope, non-goals, or completion conditions.

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
- Scope ledger:
- In scope:
- Explicit non-goals:
- Roadmap packets:
- CORE requirements:
- Acceptance criteria:
- Release gates:

Plan:
- Implementation steps:
- Ledger ownership and evidence plan:
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

## Release Development Log

After producing the Required Completion Report, write that report to:

`.DESIGN/agent-workflow/logs/LOG_[VERSION].md`

Create the `logs` directory if it does not exist.

Phase 1 initializes the development log for this release. If the `[VERSION].md` log already exists, **replace its contents** rather than appending to it.

Use this structure:

# Atlas [VERSION] Development Log

This log records the agent-assisted development and release workflow for Atlas [VERSION].

## Phase 1 — Analyze & Plan

<insert the complete Phase 1 Required Completion Report here>

RULES:

The report written to the log must match the report returned to the user. Do not omit failed, blocked, incomplete, or non-applicable findings.
