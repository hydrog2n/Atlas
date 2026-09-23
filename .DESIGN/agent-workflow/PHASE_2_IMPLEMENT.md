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
8.  Keep implementation code readable and intentional:
        - Use descriptive local variables for paths, parsed data, options,
            intermediate results, and repeated expressions when doing so makes
            control flow easier to follow.
        - Cache a value when it prevents repeated work or makes an invariant
            visible; do not duplicate a long expression merely to avoid a local
            name.
        - Use narrow namespace aliases, type aliases, and small helpers when
            they reduce clutter without hiding an important ownership or module
            boundary. Keep public APIs explicit at architectural boundaries.
        - Remove repeated literals, qualified calls, and equivalent branches
            when a named helper or constant makes the intended behavior clearer.
        - Prefer simple control flow and cohesive functions over compressed
            one-liners, clever templates, or abstractions added only to reduce
            line count.
9.  Comment the code in a normal engineering tone:
        - Comments should explain intent, constraints, invariants, compatibility
            behavior, failure safety, or a non-obvious algorithm.
        - Use a concise single-line comment when one line is sufficient.
        - Keep comments next to the code they explain and update them when the
            behavior changes.
        - Do not narrate obvious syntax, restate the function name, or write
            tutorial-style commentary about basic C++ operations.
        - When a helper or variable is introduced to improve readability, use a
            comment only when its purpose is not already clear from its name.

## Scope and Architecture Checkpoint

Before the first implementation edit, read the Phase 1 release-scope ledger
and treat it as binding.

- Implement only items marked in scope for `[VERSION]`. Do not promote a
    future-release capability because it makes the current implementation more
    convenient.
- For every changed file, record the roadmap packet, CORE requirement, or
    acceptance criterion it serves. If no ledger item owns the change, stop and
    classify it as an explicit non-goal or request a decision.
- Keep the ledger status honest: `implemented` means code and focused tests
    exist; it does not mean the release gate has passed.
- Preserve the specification's authority order, ownership, identity, units,
    source-versus-derived separation, and compatibility dimensions. A local
    implementation shortcut is not permission to weaken a canonical invariant.
- Before the Phase 2 gate, reconcile every ledger item against the actual
    diff. Report every partial packet, unimplemented acceptance criterion,
    missing fixture, and unverified release gate explicitly.
- A green focused test suite can establish an incremental implementation
    slice, but it cannot promote the roadmap version or mark release readiness.

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
- Scope ledger status:
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
- Unimplemented or partial ledger items:
- Remaining implementation work:
- ADR or design decision needed:

Phase Gate:
- Ready for validation: yes | no
- Reason:
```

Do not claim full release validation or release readiness during this
phase.

## Release Development Log

After producing the Required Completion Report, append that report to the existing release log:

`.DESIGN/agent-workflow/logs/[VERSION].md`

Do not replace or modify the existing Phase 1 record.

Append:

---

## Phase 2 — Implement

<insert the complete Phase 2 Required Completion Report here>

RULES:

The report written to the log must match the report returned to the user. Preserve earlier phase records exactly. Do not omit failed, blocked, incomplete, or non-applicable findings.
If the expected release log does not exist, stop and report that Phase 1 has not established the required development log rather than silently creating an incomplete history.
