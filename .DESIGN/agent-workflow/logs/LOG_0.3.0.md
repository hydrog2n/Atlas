# Atlas v0.3.0 Development Log

This log records the agent-assisted development and release workflow for Atlas v0.3.0.

## Phase 1 — Analyze & Plan

Result: completed  
Phase: 1 — Analyze & Plan  
Version: `v0.3.0`

**Repository State:**
- Current version/state: `v0.2.1`; the domain model and persistence foundation are implemented and validated through the v0.2.1 incremental release.
- Relevant existing implementation:
  - `atlas_domain` provides `Project`, `Map`, deterministic normalized JSON, and unknown-field preservation.
  - `atlas_persistence` provides canonical directory packages, hashes, checkpoints, recovery, migration reporting, failure simulation, and compatibility fixture replay.
  - The CLI provides package create, inspect, recover, and migrate workflows.
  - No command processor, immutable revision type, transaction history, undo/redo, dependency graph, scheduler, incremental validator, diagnostic model, repair-command model, or destructive-impact workflow exists.
  - Existing tests are domain and persistence tests only; no transaction or background-result tests exist.

**Release Scope:**
- Objective: Establish one deterministic command path for future edits with preview, commit/cancel, undo/redo, dependency invalidation, incremental diagnostics, and stale-result protection.
- Scope ledger:
  - `R030-001` Command and revision protocol: planned; owner `atlas-application`; evidence preview isolation, revision guards, impact sets, atomic commit, and cancel tests.
  - `R030-002` Undo redo journal: planned; owner `atlas-application`; evidence random command undo/redo restores normalized source, IDs, references, and selection context.
  - `R030-003` Dependency graph and scheduler: planned; owner `atlas-application`; evidence dependency declarations, invalidation records, version-tagged results, and stale-result rejection.
  - `R030-004` Diagnostics and repairs: planned; owner `atlas-application`; evidence stable diagnostic IDs, severities, revision tags, repair previews, and command-backed repair commits.
  - `R030-005` Transaction property tests: planned; owner `tests`; evidence randomized rollback, stale revisions, coalescing, failure injection, undo/redo, and rebuild isolation.
  - Ship items include immutable revisions, serialized commands, command contracts, coalescing/cancel, dependency graph, incremental diagnostics, repair actions, and destructive-impact resolution.
- Explicit non-goals:
  - No road-specific editing commands.
  - No optimized multithreaded scheduler; correctness and stale-result rejection come first.
  - No plugin mutation boundary beyond an interface placeholder.
  - No canvas/UI editing workflow from v0.4.0.
  - No road, lane, junction, elevation, submap, prefab, or procedural source commands.
- Roadmap packets: `R030-001` through `R030-005`.
- CORE requirements: `ARCH-CORE-008`, `ARCH-CORE-009`, `DEPS-CORE-001` through `DEPS-CORE-004`, `UNDO-CORE-001` through `UNDO-CORE-005`, `UX-CORE-005`, and `VAL-CORE-001` through `VAL-CORE-004`.
- Acceptance criteria: `AC-015` directly, plus preview/cancel/undo/revision gates from the roadmap.
- Release gates: stale results cannot replace newer caches; random full undo restores normalized source and selection; cancelled previews do not mutate state; rebuild failure preserves source and emits Error diagnostics; destructive deletion requires explicit impact resolution; v0.2.1 tests and fixtures remain green.

**Plan:**
1. Define immutable revisions and command contracts.
2. Implement preview, commit, cancel, stale revision rejection, and atomic history updates.
3. Implement undo/redo, selection context, coalescing, dependencies, and stale-result acceptance.
4. Implement stable diagnostics, repair commands, and destructive-impact resolution.
5. Add deterministic, property-style, failure-isolation, and integration tests.
6. Preserve v0.2.1 persistence meaning and compatibility fixtures.

**Ledger ownership and evidence plan:**
- `atlas-application` owns revisions, commands, transactions, history, dependencies, scheduler acceptance, diagnostics, repairs, and impact analysis.
- `atlas-domain` remains authoritative and independent of Qt, filesystem, renderer, and plugin code.
- `atlas-persistence` stores/restores authoritative state but is not the command source of truth.
- `tests/atlas/application` owns requirement-tagged transaction and stale-result tests.
- Every ledger row must reach `validated` through executable evidence; implementation alone remains `implemented`.

**Tests required:**
- Immutable revision, preview, cancel, stale revision, atomic commit, undo/redo, selection, coalescing, dependency, stale-result, diagnostic, repair, destructive-impact, failure-isolation, and full-undo tests.
- All v0.2.1 persistence and compatibility fixture tests.

**Documentation/versioning impact:**
- Add `atlas-application` target/module documentation.
- Keep application version `0.2.1` separate from project schema `1`, geometry, exporter, plugin API, and specification versions.
- Do not update release status or changelog during Phase 2.

**Persistence/compatibility impact:**
- v0.2.1 packages must continue to open and resave without semantic drift.
- Command history must not silently change package schema meaning.
- Undo/redo operates on authoritative source state, never derived cache bytes.

**Risks / Decisions:**
- Stop conditions: no ownership transfer to application code, no preview mutation, no background authoritative mutation, no road commands, no persisted meaning changes without ADR, and no stale-result claim without a race test.
- ADR or design decision needed: none for the correctness-first in-memory processor; an ADR is required for persisted command history or promoted multithreaded scheduling.

**Phase Gate:**
- Ready to implement: yes
- Ready for release integration: no
- Reason: v0.3.0 scope is bounded but not yet implemented or evidenced.

---

## Phase 2 — Implement (final)

Result: completed  
Phase: 2 — Implement  
Version: `v0.3.0`

**Implementation:**
- Added the Qt-independent `atlas_application` module with immutable-style revisions and selection context.
- Added command contracts with expected-revision guards, preview, impact IDs, invalidation records, commit, cancel, undo, and redo.
- Added continuous command coalescing, dependency edges, unique dependent lookup, version-tagged result acceptance, and stale-result rejection.
- Added stable diagnostics, repair commands, and explicit destructive-impact resolution.
- Added deterministic command failure isolation and full deterministic undo sequences.
- Preserved all v0.2.1 persistence behavior and compatibility tests.
- Scope ledger status: `R030-001` through `R030-005` implemented for the correctness-first in-memory scope; no road-specific commands or optimized multithreaded scheduler were introduced.
- Persistence/compatibility impact: no package schema or persisted meaning changed.

**Focused Checks:**
- Headless configure/build and `ctest --preset headless-vcpkg --output-on-failure`.
- Result: 42 out of 42 tests passed, including all v0.2.1 persistence tests and ten v0.3 application tests.

**Issues:**
- Deviations from Phase 1: property-scale behavior is represented by deterministic command sequences rather than a third-party property-testing framework; scheduler execution remains intentionally single-threaded.
- Remaining implementation work: no remaining v0.3 correctness-slice gap identified; optimized scheduling and broader generated-input campaigns remain outside this release’s explicit correctness-first boundary.
- ADR or design decision needed: none.

**Phase Gate:**
- Ready for validation: yes
- Reason: The v0.3 correctness-first command, undo, dependency, diagnostic, repair, and impact-resolution surface is implemented and tested.

---

## Phase 3 — Validate & Fix (final)

Result: completed  
Phase: 3 — Validate & Fix  
Version: `v0.3.0`

**Validation:**
- Headless configure/build passed.
- Official Qt desktop configure/build passed.
- 42 out of 42 tests passed.
- Architecture governance checks passed.
- Changed-file diagnostics passed.
- `git diff --check` passed.

**Conformance:**
- `R030-001` validated for revision guards, preview isolation, commit, cancel, and atomic history.
- `R030-002` validated for undo/redo, selection restoration, coalescing, failure isolation, and deterministic full undo.
- `R030-003` validated for dependency edges and stale version-tagged result rejection.
- `R030-004` validated for stable diagnostics, repair commands, and destructive-impact resolution.
- `R030-005` validated through deterministic transaction and failure-isolation tests.
- Current portions of `ARCH-CORE-008/009`, `DEPS-CORE-001/003`, `UNDO-CORE-001/002/003/004`, and `VAL-CORE-003/004` have executable evidence.
- v0.2.1 persistence tests and compatibility fixtures remain green.

**Defects:**
- Defects found: one JSON selection test fixture initialized an array instead of an object.
- Fixes made: corrected the fixture; no production defect remained.
- Regression coverage added: repair execution, failed-command isolation, deterministic full undo, coalescing, dependency graph, stale-result, and impact-resolution tests.
- Remaining failures/limitations: no optimized multithreaded scheduler and no plugin mutation boundary, both explicit v0.3 non-goals.
- Stop-ship defects: none for the scoped v0.3 correctness-first release.

**Phase Gate:**
- Ready for release integration: yes
- Reason: Both build paths, 42 tests, command/undo/dependency/diagnostic/repair behavior, stale-result protection, governance, and prior-release compatibility remain green within the declared v0.3 scope.

---

## Phase 4 — Release Integration

Result: completed  
Phase: 4 — Release Integration  
Version: `v0.3.0`

**Release Integration:**
- Scope shipped: immutable revisions, deterministic command preview/commit/cancel, undo/redo, selection restoration, coalescing, dependency edges, stale-result rejection, diagnostics, repair commands, and destructive-impact resolution.
- Final scope-ledger reconciliation: `R030-001` through `R030-005` are implemented and evidenced for the correctness-first in-memory scope; explicit v0.3 non-goals remain excluded.
- Application-version locations updated: CMake, vcpkg metadata, UI text, package generator metadata, README, build guide, roadmap, changelog, and canonical specification status.
- Independent compatibility versions changed: None. Project schema remains `1` and v0.2.1 package compatibility is preserved.
- Changelog/release notes updated: v0.3.0 release notes added.
- Documentation/status updated: README, build guide, roadmap, and Revision 3.0 current-release status identify v0.3.0.
- Compatibility/migration impact: no package schema change; existing v0.2.1 packages and fixtures remain supported.
- Known limitations: no road-specific commands, optimized multithreaded scheduler, plugin mutation boundary, or interactive canvas workflow.

**Final Verification:**
- Final commands/checks run:
  - Headless configure/build/test.
  - Official Qt configure/build.
  - Architecture governance, changed-file diagnostics, and `git diff --check`.
- Result: 42 out of 42 tests passed; both build paths and governance checks passed.
- Release gates confirmed: preview isolation, stale revision rejection, atomic command commit, cancel, undo/redo, coalescing, dependency edges, stale-result rejection, diagnostics, repair execution, destructive-impact resolution, and prior-release persistence compatibility passed.
- Release evidence status: Complete for the declared v0.3.0 correctness-first scope.
- Stop-ship defects: None identified.

**Release Status:**
- Release complete: yes
- ADR or design decision needed: None.
- Reason: Implementation, tests, scope ledger, version metadata, documentation, compatibility behavior, and validation evidence agree. Phase 4 is complete; no later phase was started.
