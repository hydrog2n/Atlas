# Atlas v0.4.0 Development Log

This log records the agent-assisted development and release workflow for Atlas v0.4.0.

## Phase 1 — Analyze & Plan

Result: completed  
Phase: 1 — Analyze & Plan  
Version: `v0.4.0`

**Repository State:**
- Atlas was at v0.3.1 with validated domain, persistence, command, dependency, diagnostic, repair, and stale-result foundations.
- The Qt shell was a minimal QLabel with no canvas, renderer, generic MapObject model, selection, snapping, hierarchy, inspector, or accessibility workflow.
- The existing headless suite contained 53 passing tests and project schema version `1` remained the active persistence contract.

**Release Scope:**
- Objective: deliver the first interactive two-dimensional authoring workspace while preserving double-precision authoritative coordinates, stable identity, command-based mutation, source/derived separation, deterministic selection, and keyboard-accessible workflows.
- Roadmap packets: `R040-001` canvas camera and render layers; `R040-002` snapping and constraints; `R040-003` generic geometry tools; `R040-004` selection hierarchy and inspector; `R040-005` interactive accessibility suite.
- First-enforced requirements: `A11Y-CORE-001`, `A11Y-CORE-002`, `DRAFT-CORE-001`, `DRAFT-CORE-002`, `UX-CORE-001`, `UX-CORE-002`, `UX-CORE-003`, and `UX-CORE-004`.
- Release gates: generic workflows support preview/commit/cancel/undo/redo/save-load/selection restoration; overlap selection is deterministic; zoom and units do not mutate stored geometry; calibration previews effects without silent source transformation; shipped workflows are keyboard-operable; v0.3.1 compatibility remains green.
- Explicit non-goals: roads, lanes, junctions, GIS interpretation, elevation, 3D preview, child Maps, PortalLinks, plugins, exporters, multithreaded scheduling, and precision-reducing optimization.

**Decisions:**
- ADR-0003 selects `QOpenGLWidget` behind an Atlas-owned renderer abstraction for the prototype.
- ADR-0004 defines an Atlas-owned geometry/tolerance interface and internal-versus-Boost.Geometry evaluation without exposing Boost types.
- ADR-0005 rejects coincident or below-tolerance reference calibration commits while preserving diagnostic preview behavior.

**Phase Gate:**
- Ready to implement: yes for the scoped v0.4 implementation.
- Ready for release integration: no; implementation and validation were pending.

---

## Phase 2 — Implement

Result: completed  
Phase: 2 — Implement  
Version: `v0.4.0`

**Implementation:**
- Added generic `MapObject` records with stable IDs, geometry, visibility, locking, tags, DisplayLayer references, SpatialLevel references, deterministic normalization, and immutable map/project replacement helpers.
- Added package persistence for generic objects, layers, levels, unknown object types, and normalized round trips without changing schema version `1`.
- Added duplicate-ID and DisplayLayer/SpatialLevel reference validation, authoritative object-record tamper detection, and project-relative reference-image path validation.
- Added command-backed generic object creation, editing, deletion, atomic validation, undo, redo, and generic object-family coverage.
- Added Atlas-owned geometry tolerance, grid snapping, deterministic snap candidates, angle/distance constraints, reference calibration, and below-tolerance rejection.
- Added deterministic selection state, overlap hit testing, hidden/locked filtering, render-layer composition, camera fit, keyboard canvas navigation, and a Qt workspace with hierarchy and inspector docks.
- Added accessible canvas naming, focus policy, keyboard interaction tests for cycling and cancellation, a command-palette action, and non-color locked-object cues.
- Accepted ADR-0003 through ADR-0005 before implementation and preserved the final geometry-library choice as a deferred follow-up decision.

**Scope Ledger:**
- `R040-001`: implemented for camera transforms, fit, derived grid/axes, render-layer ordering, keyboard pan/zoom, and Qt canvas integration.
- `R040-002`: implemented for tolerance policy, grid/angle/distance constraints, deterministic snap candidates, and hit-test candidate ordering. Library evaluation remains behind the Atlas-owned interface.
- `R040-003`: implemented for generic object records, all required object families, command mutation, calibration semantics, persistence, and integration workflow.
- `R040-004`: implemented for selection state, overlap cycling, precedence, hidden/locked filtering, layer/level references, hierarchy shell, and inspector shell.
- `R040-005`: implemented for accessible canvas naming, focus, keyboard pan/zoom/cycle, command-palette access, and non-color state cues; UI test coverage is executable under the official Qt build.
- v0.3.1 regression suite and compatibility fixtures: preserved and passing.

**Requirements addressed:**
- `A11Y-CORE-001`, `A11Y-CORE-002`, `DRAFT-CORE-001`, `DRAFT-CORE-002`, `UX-CORE-001`, `UX-CORE-002`, `UX-CORE-003`, and `UX-CORE-004` received implementation and test evidence.
- Source-versus-derived and precision boundaries remain enforced by the domain/render split and camera round-trip tests.

**Files/components changed:**
- Extended domain, persistence, application, render, and UI modules.
- Added geometry, selection, hit-testing, render-layer, calibration, integration, performance, and UI test modules.
- Updated CMake targets and accepted ADR/backlog status.

**Focused Checks:**
- Domain, persistence, application, geometry, render, and integration tests passed during implementation.
- Official Qt target and UI test target built successfully.
- Added regression tests for all generic object-family persistence, duplicate and invalid references, tampered object records, degenerate constraints, selection restoration, reference-path safety, and keyboard selection behavior. New tests include purpose comments immediately before each test case.

**Persistence/compatibility impact:**
- Project schema remains version `1`.
- Generic object records are optional and forward-compatible; unknown object types are preserved.
- The v0.3.1 empty-project compatibility fixture remains valid.

**Phase Gate:**
- Ready for validation: yes.
- Reason: All planned implementation slices and focused tests were present, with no unresolved architectural conflict.

---

## Phase 3 — Validate & Fix

Result: completed  
Phase: 3 — Validate & Fix  
Version: `v0.4.0`

**Validation:**
- Headless configure and build passed in the initialized Visual Studio x64 environment.
- Headless CTest passed: 101 out of 101 tests, with no failures or skips.
- Official Qt configure and build passed.
- UI tests passed: 4 out of 4.
- `windeployqt` deployment completed successfully.
- Architecture governance checks passed.
- `git diff --check` passed.

**Scope ledger result:**
- `R040-001` through `R040-005`: validated by focused unit, integration, render, performance, persistence, and Qt UI evidence.
- v0.3.1 regression suite and retained compatibility fixture: validated within the 101-test headless suite.

**Conformance:**
- Camera/world round trips, large coordinates, pan, zoom, fit, and source immutability passed.
- Generic object families, command transactions, undo/redo, save/load, unknown types, duplicate/reference validation, tamper detection, calibration rejection, snapping edge cases, selection cycling, hit-test precedence, and hidden/locked behavior passed.
- Keyboard canvas navigation, accessible naming, focus policy, selection cycling/cancellation, and non-color locked-object cues passed through the Qt UI test target.
- Dense 10,000-object hit testing recorded a 61 ms debug-build baseline with deterministic results.

**Release gates:**
- Generic workflow transaction and persistence evidence passed through command and integration tests.
- Deterministic overlap selection and hidden/locked filtering passed.
- Zoom, pan, and camera transforms did not mutate authoritative source data.
- Invalid reference calibration produced no transform and preserved source coordinates.
- Keyboard-accessible canvas behavior and accessible naming passed in the Qt UI suite.

**Defects:**
- Defects found during final validation: none.
- Fixes made: corrected hit-test target linking, Qt menu-header linkage, camera keyboard direction, and dense hit-test ordering during the implementation/validation loop.
- Regression coverage added: generic object families, persistence round trips, duplicate/reference integrity, object-record tamper detection, calibration, degenerate constraints, selection restoration, hit testing, performance baseline, integration workflow, render layers, reference-path safety, and UI keyboard/accessibility tests.
- Remaining limitations: final long-term geometry-library selection remains deferred behind ADR-0004; the current v0.4 implementation uses the Atlas-owned internal tolerance/snap path. Broader WCAG audit and cross-platform graphics validation remain future hardening work.
- Stop-ship defects: none for the v0.4.0 scope validated here.

**Phase Gate:**
- Ready for release integration: yes.
- Reason: All five R040 packets, applicable v0.4 release gates, prior-release regressions, compatibility fixtures, supported builds, and governance checks have executable evidence. Phase 4 has not been started.

---

## Phase 4 — Release Integration

Result: completed  
Phase: 4 — Release Integration  
Version: `v0.4.0`

**Release Integration:**
- Scope shipped: real-scale camera navigation, generic geometry records and commands, layers and levels, snapping and constraints, reference calibration, deterministic selection and hit testing, persistence, hierarchy and inspector shell, keyboard canvas workflows, accessible naming, and non-color state cues.
- Final scope-ledger reconciliation: `R040-001` through `R040-005` are implemented and evidenced for the v0.4.0 scope; explicitly deferred roads, GIS interpretation, elevation, child Maps, plugins, exporters, and multithreaded scheduling remain excluded.
- Application-version locations updated: CMake project metadata, vcpkg manifest, README, build guide, roadmap current status, canonical specification current status, package generator metadata, UI/workspace documentation, changelog, and v0.4.0 development log.
- Independent compatibility versions changed: None. Project schema remains `1`; geometry-engine, exporter, plugin API, and specification compatibility dimensions remain unchanged.
- Changelog/release notes updated: v0.4.0 release notes added.
- Documentation/status updated: v0.4.0 is now the active release; v0.3.1 remains preserved as historical and compatibility context.
- Compatibility/migration impact: Generic object records remain schema-version `1` compatible, unknown object types are preserved, and v0.3.1 packages and retained fixtures remain supported.
- Known limitations: Final long-term geometry-library selection remains deferred behind ADR-0004; broader WCAG certification and cross-platform graphics validation remain future hardening work.

**Final Verification:**
- Final commands/checks run:
	- Headless configure, build, and CTest.
	- Official Qt configure, build, UI tests, and `windeployqt` deployment.
	- Architecture governance checks.
	- Active-version stale-claim search.
	- `git diff --check`.
- Result: 101 out of 101 headless tests passed; 4 out of 4 Qt UI tests passed; both builds and deployment passed; governance and diff checks passed.
- Release gates confirmed: generic transaction/persistence workflows, deterministic selection and hit testing, camera/source immutability, calibration rejection, keyboard canvas behavior, accessibility naming/focus, compatibility fixtures, and prior-release regressions.
- Release evidence status: Complete for the declared v0.4.0 scope.
- Stop-ship defects: None identified.
- Unvalidated or partial roadmap items: No in-scope R040 packet remains partial. Deferred capabilities are recorded as non-goals and are not release requirements for v0.4.0.

**Release Status:**
- Release complete: yes
- ADR or design decision needed: None for the v0.4.0 release scope. ADR-0004 remains the approved boundary for a later final geometry-library selection.
- Reason: Implementation, tests, application-version metadata, documentation, compatibility behavior, release evidence, and applicable release gates agree for Atlas v0.4.0.
