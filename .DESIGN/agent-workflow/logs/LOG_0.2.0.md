# Atlas v0.2.0 Development Log

This log records the agent-assisted development and release workflow for Atlas v0.2.0.

## Phase 1 — Analyze & Plan

Result: completed  
Phase: 1 — Analyze & Plan  
Version: `v0.2.0`

**Repository State:**
- Current version/state: `v0.1.2`; the domain kernel and deterministic normalized JSON are implemented. The working tree contains an incomplete persistence slice: `CMakeLists.txt`, the domain unknown-field changes, and `package.cpp` are present, but the persistence public header and persistence tests are currently absent.
- Relevant existing implementation:
  - [project.hpp](../../src/atlas/domain/project.hpp) and [project.cpp](../../src/atlas/domain/project.cpp) provide in-memory `Project` parsing and normalized JSON with unknown top-level field retention.
  - [CMakeLists.txt](../../CMakeLists.txt) references an `atlas_persistence` library and persistence test target.
  - `src/atlas/persistence/package.cpp` contains the directory-package implementation but currently cannot compile without its missing public header.
  - Existing normalized JSON remains a conformance/test representation only.

**Release Scope:**
- Objective: Make authoritative Atlas source durable through a canonical, inspectable `.atlas` directory package.
- In scope:
  - `R020-001` Canonical record envelopes
  - `R020-002` Package reader and validator
  - `R020-003` Atomic writer and checkpoints
  - `R020-004` Migration framework
  - `R020-005` Fault-injection persistence suite
  - Headless workflows for create, open, save, inspect, recover, and migrate
  - Deterministic serialization, manifest hashes, path safety, limits, and unknown-data preservation
- Explicit non-goals:
  - Standalone v0.1 normalized JSON import
  - Road-specific records beyond forward-compatible type registration stubs
  - Command transactions, undo/redo, and complex editing from v0.3.0
  - Background cache infrastructure beyond disposable-cache metadata
  - Cloud sync and collaboration
  - Canvas, road, lane, network, and export features
- Roadmap packets: `R020-001` through `R020-005`.
- CORE requirements: `DATA-CORE-001` through `DATA-CORE-004`, `FILE-CORE-001` through `FILE-CORE-005`, `SAFE-CORE-001`, and `SAVE-CORE-001` through `SAVE-CORE-005`.
- Acceptance criteria: `AC-012`, `AC-013`, `AC-014`, and `AC-017`.
- Release gates: interrupted saves must leave a valid old or complete new package; failed migrations must preserve the source and report; unknown content must survive load-save; recovery must expose a consistent checkpoint; malformed input must not mutate open state or escape the approved root; normalized save-load-save must be idempotent.

**Plan:**
1. Restore and define the Qt-independent logical persistence interface.
2. Keep the v0.2 directory adapter behind that interface and generate the approved package layout.
3. Preserve unknown fields and namespaced records without reinterpretation, relocation, or deletion.
4. Validate manifest identity, schema, project paths, and authoritative records defensively.
5. Add deterministic save/load and sibling staging tests before expanding to hashes, checkpoints, migration, and fault injection.
6. Implement the remaining R020-003, R020-004, and R020-005 behavior only after the first slice has a stable public contract.

**Tests required:**
- Public package save/load and manifest validation tests.
- Unknown-field and unknown-record round-trip tests.
- Canonical layout, staging replacement, and traversal rejection tests.
- Existing v0.1 domain regression tests.
- Later slices require content-hash, interruption, checkpoint, recovery, migration, hostile-input, missing-asset, and CLI integration suites.

**Documentation/versioning impact:**
- Keep application version `0.1.2` separate from package schema and format versions.
- Do not perform release propagation, changelog updates, or roadmap promotion during Phase 2.
- Preserve ADR-0002 as the authority for the directory representation, storage abstraction, test-only v0.1 JSON status, and unknown-data contract.

**Persistence/compatibility impact:**
- The v0.2 physical representation is a directory `.atlas` package, but the logical package interface must remain storage-neutral.
- Standalone v0.1 normalized JSON is not a supported legacy import format.
- Unknown fields and unknown namespaced records must retain semantic content through load/save.
- Caches remain disposable and never become authoritative source.

**Risks / Decisions:**
- Stop conditions: do not conflate application and package schema versions; do not make directory storage permanent; do not discard or reinterpret unknown data; do not claim atomic recovery or migration before fault-injection evidence exists.
- ADR or design decision needed: none. ADR-0002 resolves the package representation and unknown-data policy.

**Phase Gate:**
- Ready to implement: yes
- Reason: The accepted ADR resolves the architectural decisions. The current tree has an incomplete implementation slice that Phase 2 must repair before extending it.

---

## Phase 2 — Implement

Result: completed  
Phase: 2 — Implement  
Version: `v0.2.0`

**Implementation:**
- Implemented the Qt-independent persistence package boundary and canonical directory representation.
- Added explicit project/map envelopes, reference metadata, authoritative hashes for every generated source file, line-ending-independent hashing, staging, rollback restoration, checkpoints, recovery, and bounded JSON limits.
- Added deterministic save-failure simulation for staging, validation, checkpoint, backup, disk-full, permission, forced-termination, and journal-truncation scenarios.
- Added schema 0 to schema 1 migration reporting and explicit newer-schema read-only loading.
- Added headless `create`, `inspect`, `recover`, and `migrate` workflows.
- Preserved unknown project fields and namespaced extension data through normalized save/load.
- Added the retained `empty-project` compatibility fixture and replay test.
- Roadmap packets addressed: `R020-001` through `R020-005` implementation surface for the current v0.2 schema.
- CORE requirements addressed: implemented portions of `DATA-CORE-001` through `DATA-CORE-004`, `FILE-CORE-001` through `FILE-CORE-005`, `SAFE-CORE-001`, and `SAVE-CORE-001` through `SAVE-CORE-005`.
- Documentation/comments: implementation comments explain unknown-data ownership, hashes, staging, checkpoints, rollback, migration, safety limits, and CLI responsibilities.
- Persistence impact: establishes the v0.2 directory package, schema compatibility behavior, authoritative-file inventory, checkpoints, recovery, failure reports, newer-schema protection, and fixture replay without making standalone v0.1 JSON a legacy import format.

**Focused Checks:**
- Headless configure/build and `ctest --preset headless-vcpkg --output-on-failure`.
- CLI create, inspect, recover, and migrate workflows.
- Save-failure, migration, checkpoint, malformed-input, size-limit, missing-file, deterministic round-trip, and fixture replay tests.
- Result: 26 out of 26 tests passed; all CLI workflows passed.

**Issues:**
- Deviations from Phase 1: implementation expanded the initial slice to cover authoritative file inventory and retained fixture replay before release validation.
- Remaining implementation work: no known gap within the supported v0.2 persistence scope; future schema versions and broader fuzz campaigns remain future hardening work.
- ADR or design decision needed: none; ADR-0002 governs the implementation.

**Phase Gate:**
- Ready for validation: yes
- Reason: The planned v0.2 persistence surface is implemented, tested, and exposed through headless workflows and retained compatibility fixtures.

---

## Phase 3 — Validate & Fix

Result: completed  
Phase: 3 — Validate & Fix  
Version: `v0.2.0`

**Validation:**
- Headless configure and build passed with the supported MSVC/vcpkg toolchain.
- Official Qt desktop configure and build passed.
- 26 out of 26 tests passed.
- CLI `create`, `inspect`, `recover`, and `migrate` workflows passed.
- Retained compatibility fixture loaded and resaved without normalized semantic drift.
- Architecture governance checks passed.
- Changed-file diagnostics reported no errors.
- `git diff --check` passed.
- Deterministic save-load-save, complete generated-file hash validation, malformed input rejection, missing authoritative file rejection, size/nesting/node limits, checkpoint corruption rejection, simulated filesystem/process failures, migration failure reports, schema migration, and newer-schema read-only behavior passed.

**Conformance:**
- CORE requirements verified: current v0.2 persistence, safety, deterministic serialization, explicit envelopes, authoritative hashes, unknown-data, staging, checkpoint, recovery, migration, and compatibility-fixture portions passed their implemented tests.
- Acceptance criteria verified: `AC-012`, `AC-013`, `AC-014`, and `AC-017` have executable evidence for the supported v0.2 schema and failure simulations.
- Release gates verified: atomic replacement preservation, failed migration reporting without source loss, unknown-data round trips, consistent checkpoint recovery, malformed-input safety, save-load idempotence, and retained-fixture replay passed.

**Defects:**
- Defects found: one fixture hash portability defect caused by Windows CRLF checkout conversion.
- Fixes made: authoritative content hashing now canonicalizes CRLF to LF before hashing.
- Regression coverage added: retained fixture load-resave replay and complete authoritative-file hash validation.
- Remaining failures/limitations: visual/accessibility checks do not apply to this persistence-only change; future schema versions and broader fuzz campaigns remain outside the supported v0.2 schema evidence.
- Stop-ship defects: none identified for the v0.2 persistence scope.

**Phase Gate:**
- Ready for release integration: yes
- Reason: Both supported build paths, 26 persistence/domain tests, CLI workflows, compatibility fixture replay, architecture governance, diagnostics, deterministic behavior, failure simulations, migration behavior, authoritative hashes, and recovery behavior pass.

---

## Phase 4 — Release Integration

Result: completed  
Phase: 4 — Release Integration  
Version: `v0.2.0`

**Release Integration:**
- Scope shipped: canonical `.atlas` directory packages, explicit envelopes, complete generated-file hashes, checkpoints, recovery, migration reporting, newer-schema protection, unknown-data preservation, hostile-input limits, CLI workflows, and retained compatibility fixture replay.
- Application-version locations updated: CMake, vcpkg metadata, UI text, package generator metadata, README, build guide, roadmap, changelog, and canonical specification current-release status.
- Independent compatibility versions changed: None. Project schema remains `1`; application and schema versions remain distinct.
- Changelog/release notes updated: v0.2.0 Added, Changed, Fixed, Testing / Validation, Compatibility / Migration, and Known Limitations sections.
- Documentation/status updated: README, build guide, roadmap, and Revision 3.0 current-release status identify v0.2.0.
- Compatibility/migration impact: schema 0 to schema 1 migration reports and rewrites are supported; newer schemas are read-only; standalone v0.1 normalized JSON remains test-only.
- Known limitations: command transactions, undo/redo, interactive editing, road authoring, production export, and future schema versions remain later capabilities.

**Final Verification:**
- Final commands/checks run:
  - Headless configure/build/test.
  - Official Qt SDK configure/build.
  - CLI create/inspect/recover/migrate workflows.
  - Compatibility fixture replay.
  - Architecture governance, diagnostics, and `git diff --check`.
- Result: 26 out of 26 tests passed; both build paths, CLI workflows, fixture replay, governance, diagnostics, and formatting checks passed.
- Release gates confirmed: atomic replacement preservation, failed migration reporting, unknown-data round trips, authoritative-file hashes, checkpoint recovery, malformed-input safety, save-load idempotence, and compatibility fixture replay have executable evidence.
- Release evidence status: Complete for the v0.2.0 roadmap scope represented by the supported package model and retained fixture catalog.
- Stop-ship defects: None identified.

**Release Status:**
- Release complete: yes
- ADR or design decision needed: None.
- Reason: Implementation, tests, compatibility fixture replay, application metadata, documentation, release evidence, and applicable v0.2.0 gates agree. Phase 4 is complete; no later phase was started.
