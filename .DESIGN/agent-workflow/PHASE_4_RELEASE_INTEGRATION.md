# Phase 4 --- Release Integration

## Agent Operating Rules

This file is the execution contract for this phase of an Atlas release.

-   The target release is the version specified by the user's
    invocation. Treat it as `[VERSION]`.
-   Consult the canonical Atlas Master Design Specification,
    `ROADMAP.md`, applicable ADRs, repository documentation, actual
    repository state, and Phase 3 validation evidence.
-   The Master Design Specification is the architectural authority.
-   Perform **only this phase**.
-   Stop and report rather than guess when a mandatory stop condition or
    unresolved architectural decision is encountered.

## Objective

Prepare the validated implementation as the Atlas **\[VERSION\]**
release.

## Versioning

Propagate the **application version `[VERSION]`** consistently through
applicable build metadata, source constants, README/status information,
CI/release configuration, and package/release metadata.

Do **not** automatically change project-schema, geometry-engine,
exporter, plugin-API, or specification versions. These are independent
compatibility dimensions and change only when their corresponding
contract changed. Search the repository for stale version references
rather than relying on a predetermined file list.

## Documentation and Comments

Update applicable README/current status, roadmap status, build/developer
documentation, relevant design/interface documentation, examples,
developer instructions, and comments/API documentation made stale by
implementation.

Documentation must accurately distinguish shipped, current, and planned
capabilities. Do not modify canonical architecture merely to legitimize
an implementation shortcut.

## Changelog / Release Notes

Create/update `[VERSION]` release notes with applicable sections:

-   Added
-   Changed
-   Fixed
-   Testing / Validation
-   Compatibility / Migration
-   Known Limitations

## Final Verification

1.  Search for stale versions and incorrect current-release claims.
2.  Verify documentation/comments against actual behavior.
3.  Rerun the appropriate build and test/conformance workflow.
4.  Verify compatibility fixtures and release gates remain valid.
5.  Confirm build/test instructions remain reproducible.
6.  Confirm required release evidence is available.
7.  Confirm no Atlas stop-ship condition remains.

Do not alter tests, architecture, fixtures, or canonical documentation
merely to make the release appear complete.

## Phase Gate

Declare `[VERSION]` complete only when implementation, tests, version
metadata, documentation, compatibility information, release evidence,
and applicable release gates agree. Missing required evidence is a
failed release gate, not deferred cleanup.

## Required Completion Report

End this phase with:

``` text
Result: completed | partial | blocked
Phase: 4 — Release Integration
Version: [VERSION]

Release Integration:
- Scope shipped:
- Application-version locations updated:
- Independent compatibility versions changed:
- Changelog/release notes updated:
- Documentation/status updated:
- Compatibility/migration impact:
- Known limitations:

Final Verification:
- Final commands/checks run:
- Result:
- Release gates confirmed:
- Release evidence status:
- Stop-ship defects:

Release Status:
- Release complete: yes | no
- ADR or design decision needed:
- Reason:
```

Declare the release complete only when the applicable release gates and
required evidence are demonstrated.
