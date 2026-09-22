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
