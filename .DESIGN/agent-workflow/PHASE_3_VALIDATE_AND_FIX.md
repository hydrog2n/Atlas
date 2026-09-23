# Phase 3 --- Validate & Fix

## Agent Operating Rules

This file is the execution contract for this phase of an Atlas release.

-   The target release is the version specified by the user's
    invocation. Treat it as `[VERSION]`.
-   Validate against the canonical Master Design Specification,
    `ROADMAP.md`, applicable ADRs, repository contracts, and the
    approved Phase 1 scope.
-   The Master Design Specification is the architectural authority.
-   Perform **only this phase**. Do not automatically perform release
    integration.
-   Stop and report rather than guess when a mandatory stop condition or
    unresolved architectural decision is encountered.

## Objective

Validate Atlas **\[VERSION\]** and fix defects discovered during
validation.

## Actions

1.  Configure/build with supported CMake presets and documented
    toolchain.
2.  Run focused tests and the complete affected test/conformance suite.
3.  Run applicable formatting, static-analysis, dependency/architecture,
    deterministic-output, fixture, and compatibility checks.
4.  Run applicable unit, property, golden, transaction, integration,
    visual, performance, fuzz/security, accessibility, save/load,
    migration, and regression tests according to release scope.
5.  Verify applicable CORE requirements, acceptance criteria, roadmap
    release gates, and prior-release contracts.
6.  Investigate failures to root cause, fix implementation, add/improve
    regression coverage where appropriate, and rerun affected validation
    until clean.
7.  Verify no stop-ship condition exists and earlier supported behavior
    remains valid.

## Scope and Release Gate Reconciliation

Use the Phase 1 scope ledger and the Phase 2 reconciliation as validation
inputs. Do not infer release completeness from build success or test count.

- Validate every roadmap packet and ship-in-version item, not only the files
    that changed. Each ledger row must end as `validated` or `blocked` with
    executable evidence or an explicit reason.
- Verify every applicable CORE requirement, acceptance criterion, release
    gate, compatibility fixture, and earlier-release regression named in the
    ledger. A partial implementation is a validation finding, not a pass.
- Check that implementation changes remain inside the version scope and do
    not silently add future-release behavior or weaken a canonical requirement.
- Treat missing fixtures, unrun failure campaigns, absent migration paths,
    and undocumented compatibility behavior as failed release evidence.
- If any in-scope ledger item is not validated, the Phase 3 gate MUST be
    `Ready for release integration: no`, regardless of build or unit-test
    results.
- Only a final run with every applicable ledger row validated may report
    `Ready for release integration: yes`.

Do not delete, weaken, skip, or rewrite valid tests merely to obtain a
pass. Do not silently regenerate golden files or compatibility fixtures
to conceal regressions.

## Phase Gate

Proceed to Phase 4 only when applicable tests, acceptance criteria, and
release gates pass; previous supported behavior remains valid; and no
known stop-ship defect remains.

## Required Completion Report

End this phase with:

``` text
Result: completed | partial | blocked
Phase: 3 — Validate & Fix
Version: [VERSION]

Validation:
- Build result:
- Scope ledger result:
- Test/check suites executed:
- Passed:
- Failed:
- Not run:
- Determinism/compatibility result:

Conformance:
- CORE requirements verified:
- Roadmap packets and ship items verified:
- Acceptance criteria verified:
- Release gates verified:

Defects:
- Defects found:
- Fixes made:
- Regression coverage added:
- Remaining failures/limitations:
- Stop-ship defects:

Phase Gate:
- Ready for release integration: yes | no
- Reason:
```

Report only validation actually performed. Do not treat an unrun
applicable check as passed.

## Release Development Log

After producing the Required Completion Report, append that report to the existing release log:

`.DESIGN/agent-workflow/logs/[VERSION].md`

Do not replace or modify earlier phase records.

Append:

---

## Phase 3 — Validate & Fix

<insert the complete Phase 3 Required Completion Report here>

RULES:

The report written to the log must match the report returned to the user. Preserve earlier phase records exactly. Record failures and fixes honestly; do not rewrite earlier reports based on later results.
If the expected release log does not exist, stop and report the missing development history rather than silently creating an incomplete log.
