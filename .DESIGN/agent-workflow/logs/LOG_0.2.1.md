# Atlas v0.2.1 Development Log

This log records the incremental persistence-hardening release for Atlas v0.2.1.

## Phase 2 — Implement

Result: completed  
Phase: 2 — Implement  
Version: `v0.2.1`

- Added nested map unknown-field preservation.
- Added explicit project/map record envelopes, complete generated authoritative-file inventories, and line-ending-independent hashes.
- Added JSON depth/node limits, manifest inventory checks, checkpoint rotation, disposable-cache coverage, migration dry-run coverage, and compatibility fixture replay.
- Added 31 focused domain and persistence tests.

## Phase 3 — Validate & Fix

Result: completed  
Phase: 3 — Validate & Fix  
Version: `v0.2.1`

- Headless configure, build, and 31-test suite passed.
- Official Qt desktop configure and build passed.
- CLI create, inspect, recover, and migrate workflows passed.
- Architecture governance, diagnostics, and formatting checks passed.
- Compatibility fixture loaded and resaved without semantic drift.

## Phase 4 — Release Integration

Result: completed  
Phase: 4 — Release Integration  
Version: `v0.2.1`

**Release Integration:**
- Application version propagated to CMake, vcpkg metadata, UI text, package generator metadata, README, build guide, roadmap, changelog, and canonical specification status.
- Project schema version remains `1`; no independent compatibility dimension changed.
- v0.2.1 is documented as persistence contract hardening with no new persisted schema.

**Final Verification:**
- Final headless build/test, Qt build, CLI workflows, architecture governance, diagnostics, and whitespace checks passed.
- Release complete: yes.
- Phase 4 completed without starting a later phase.
