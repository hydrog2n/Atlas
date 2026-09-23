# Changelog

All notable changes to Atlas will be documented in this file. This is the running project changelog for implemented releases, and it is intended to be updated at the end of each milestone with a concise summary of what shipped, what was validated, and what remains next.

The format follows a release-focused pattern:

- Version
- Release date
- Scope
- Implemented features
- Validation evidence
- Known limitations / next milestone

---

## v0.3.0 - 2026-09-22

### Added

- Immutable-style project revisions and a Qt-independent command processor.
- Preview, commit, cancel, stale-revision rejection, undo/redo, selection-context restoration, and continuous command coalescing.
- Dependency edges, version-tagged result acceptance, stale-result rejection, stable diagnostics, repair commands, and explicit destructive-impact resolution.

### Changed

- Promoted v0.3.0 to the current release while preserving project schema version `1` and the v0.2.1 persistence contract.

### Fixed

- Failed commands leave the authoritative revision and undo history unchanged.

### Testing / Validation

- 42 out of 42 tests passed.
- Official Qt desktop build passed.
- Architecture governance, diagnostics, and formatting checks passed.

### Compatibility / Migration

- No project package schema change.
- Existing v0.2.1 packages and compatibility fixtures remain supported.

### Known Limitations

- No road-specific commands, optimized multithreaded scheduler, plugin mutation boundary, or interactive canvas workflow.

---

## v0.2.1 - 2026-09-22

### Added

- Nested map unknown-field preservation.
- Authoritative-file inventory, manifest consistency, checkpoint rotation, cache-disposability, and compatibility-fixture replay tests.
- JSON node/depth limits and line-ending-independent authoritative hashing.

### Changed

- Hardened the v0.2 persistence contract without changing project schema version `1`.

### Fixed

- Corrected compatibility fixture hashing across Windows line-ending normalization.

### Testing / Validation

- 31 out of 31 headless tests passed.
- Official Qt desktop build passed.
- CLI persistence workflows and architecture governance checks passed.

### Compatibility / Migration

- No persisted schema change. Existing v0.2 packages remain compatible.

### Known Limitations

- Command transactions, undo/redo, interactive editing, road authoring, and production export remain planned for later releases.

---

## v0.2.0 - 2026-09-22

### Added

- Canonical inspectable `.atlas` directory packages with manifests, explicit record envelopes, root-map records, reserved asset/export/cache directories, and deterministic hashes for every generated authoritative file.
- Qt-independent persistence APIs and headless `create`, `inspect`, `recover`, and `migrate` workflows.
- Checkpoint rotation, recovery loading, save-failure simulation, schema migration reporting, and newer-schema read-only loading.
- Unknown project-field preservation and bounded malformed-input handling.
- Retained `empty-project` compatibility fixture with load-resave semantic-drift coverage.

### Changed

- Promoted v0.2.0 to the current release and moved v0.1.2 to completed history.

### Fixed

- Preserved previous valid packages when staged saves encounter simulated filesystem or process failures.
- Rejected tampered authoritative content, unsafe paths, malformed packages, missing files, and oversized JSON records.

### Testing / Validation

- 26 out of 26 headless tests passed.
- Official Qt desktop configure and build passed.
- CLI persistence workflows passed.
- Architecture governance, diagnostics, and whitespace checks passed.

### Compatibility / Migration

- Schema 0 to schema 1 migration reports and rewrites are supported.
- Newer unsupported schemas can be loaded read-only and cannot be overwritten.
- Standalone v0.1 normalized JSON remains a conformance/test representation, not a legacy project import format.

### Known Limitations

- Command transactions, undo/redo, interactive editing, road authoring, and production export remain planned for later releases.

---

## v0.1.2 - 2026-09-22

### Release scope

Desktop build workflow and developer-run integration for the v0.1.x foundation.

### Implemented

- Added the official Qt 6.11.2 MSVC SDK as the supported Windows desktop build path.
- Deprecated the unreliable Qt-through-vcpkg desktop preset while keeping the headless vcpkg path supported.
- Added Qt runtime deployment with `windeployqt` so the desktop executable can run outside the Qt environment.
- Added VS Code Run and Debug profiles for the CLI and Windows desktop application.
- Simplified the preparation tasks so each debugger profile configures, builds, tests or deploys, and then launches exactly once.

### Validation evidence

- Official Qt SDK desktop configure and build completed successfully.
- `atlas.exe` launched successfully after Qt runtime deployment.
- Headless configure, build, and all 5 domain tests passed.

### Known limitations

- Project persistence, transactions, undo/redo, and interactive authoring remain planned for later releases.

### Next milestone

- v0.2.0: Project Package Persistence and Recovery

---

## v0.1.1 - 2026-09-21

### Release scope

Release stabilization for the v0.1.x foundation. This patch release preserves the v0.1.0 domain kernel and conformance work while fixing the Windows CI/build environment so the project can configure, compile, and test reliably under the actual Visual Studio/MSVC toolchain.

### Implemented

- Fixed the GitHub Actions toolchain mismatch by ensuring the workflow initializes the Visual Studio developer environment before configure/build/test.
- Kept the repo's CMake preset using the stable Ninja generator while explicitly loading the correct compiler environment for Windows builds.
- Verified the headless build and test process under the MSVC toolchain.
- Preserved the v0.1.0 foundation build and the conformance test suite.

### Validation evidence

Verified commands:

- `cmake --preset headless-vcpkg`
- `cmake --build --preset headless-vcpkg`
- `ctest --preset headless-vcpkg --output-on-failure`

Result:

- build passed
- 5 out of 5 tests passed
- output reported: `100% tests passed out of 5`

### Known limitations

- This release does not add persistence, transactions, or higher-order editing features.
- The project remains a foundation release in the v0.1.x track.

### Next milestone

- v0.2.0: Project Package Persistence and Recovery

---

## v0.1.0 - 2026-09-20

### Release scope

Foundation and engineering contract for the Atlas domain kernel. This release establishes the repository structure, implementation baseline, deterministic source model, and the first tested core project contracts needed before higher-level editing and persistence features are built.

### Implemented

- Initial repo scaffold for the C++23 project using CMake and vcpkg.
- Windows-first build presets for the headless configuration and optional UI configuration.
- Initial baseline alignment with ADR-0001 and the canonical design specification.
- Domain model for `Project` and `Map` with stable root-map ownership.
- Deterministic normalized JSON serialization for the empty project model.
- JSON round-trip parsing with validation for required fields.
- Preservation of unknown extension metadata during parse/normalize cycles.
- Headless build and test pipeline validated under Visual Studio 2022/MSVC.
- Architecture governance notes and decision backlog to keep the v0.1.0 scope explicit.
- Requirement-tagged conformance tests aligned to release packet naming.

### Validation evidence

The build and tests were executed in the Windows Visual Studio developer shell using the project CMake presets.

Verified commands:

- `cmake --preset headless-vcpkg`
- `cmake --build --preset headless-vcpkg`
- `ctest --preset headless-vcpkg --output-on-failure`

Result:

- build passed
- 5 out of 5 tests passed
- output reported: `100% tests passed out of 5`

### Architectural constraints enforced

- Authoritative domain logic remains independent of Qt and filesystem concerns.
- Canonical source values are normalized deterministically.
- Root Map ownership is explicit and stable.
- Extension metadata is preserved without changing the canonical project model.
- The project retains a clear v0.1.0 scope boundary before persistence, transaction, and road-authoring features begin.

### Known limitations

- No persistence package format or migration system yet.
- No transactional edit history or undo/redo system yet.
- No canvas, road, lane, junction, or export features yet.
- No advanced geometry or rendering backend selection beyond the foundation decision set.

### Next milestone

- v0.2.0: Project Package Persistence and Recovery

---

## Unreleased / Future entries

### Build workflow note

- Deprecated the `windows-vcpkg` Qt desktop preset after repeated configuration stalls and failures while building Qt through vcpkg.
- Made the official Qt SDK preset the supported Windows desktop build path.
- Kept `headless-vcpkg` as the supported domain, CLI, and test path.

Entries for future versions will be added here as each version is completed and validated. Keep each entry concise, evidence-based, and aligned with the project roadmap and canonical specification.
