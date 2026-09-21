# ADR-0001: Technology Baseline

- Status: accepted
- Date: 2026-09-20
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `PROD-CORE-001`, `PROD-CORE-002`, `ARCH-CORE-001`, `ARCH-CORE-006`, `ARCH-CORE-008`, `COORD-CORE-001`, `PERF-CORE-001`

## Context

Atlas is a standalone desktop authoring application with double-precision world coordinates, parametric curves, lane and network semantics, incremental derived rebuilds, large-world indexes, keyboard workflows, accessibility requirements, and a GPU-backed 2D canvas. The repository is at `v0.1.0`; no implementation stack has been established yet.

The technology choice must support the canonical domain without making the domain depend on UI, filesystem, rendering, game-engine, or plugin implementations.

## Decision

Atlas adopts the following initial implementation baseline:

| Area | Decision |
|---|---|
| Core and application language | C++23 |
| Desktop UI | Qt 6 Widgets |
| Build system | CMake |
| Dependency management | vcpkg manifest mode with a committed lock state when supported by the selected workflow |
| Initial test framework | GoogleTest |
| Canonical package representation | UTF-8 JSON source records with deterministic normalization; the `.atlas` package remains the product-level format |
| Initial supported platform | Windows, with portable core modules and cross-platform validation as implementation permits |
| Future scripting | A later versioned API layered over validated commands; Python or Lua remains a future decision |

The UI will use Qt Widgets for the desktop shell, docking layout, hierarchy, inspector, dialogs, menus, keyboard interaction, and accessibility surface. The canvas will be a dedicated custom widget with GPU-backed derived rendering. The exact graphics backend is intentionally left to a focused prototype and performance decision; it must not change the authoritative source model.

The core module boundaries are:

```text
atlas-domain          authoritative records, invariants, IDs, units, and value types
atlas-application     commands, transactions, validation, dependencies, and revisions
atlas-geometry        analytic curves, offsets, intersections, polygons, and indexes
atlas-road            RoadSpline, RoadSegment, StationAnchor, lanes, and transitions
atlas-network         junctions, ports, connections, movements, and portals
atlas-persistence     canonical package adapters, migrations, recovery, and hashes
atlas-render          derived tessellation, semantic zoom, hit testing, and picking
atlas-ui              Qt Widgets shell, tools, inspectors, and accessibility integration
atlas-adapters        importers, exporters, engine integrations, and extension boundaries
tests                 tests through public module boundaries
```

`atlas-domain` and the other authoritative source modules MUST NOT depend on Qt, filesystem APIs, renderer implementations, game-engine SDKs, or plugin implementations. Persistence and export code may serialize or present domain records but cannot become their source of truth. Background workers return version-tagged results; only the command processor may accept them.

## Alternatives considered

- **C#/.NET with Avalonia:** strong productivity and safety, but a less direct fit for the intended native geometry and rendering ecosystem.
- **Rust with egui or Slint:** strong safety and good core potential, but a less mature fit for the planned CAD-like desktop workspace.
- **TypeScript with Electron:** productive UI development, but a weaker foundation for the native geometry, memory, and large-world rendering workload.
- **Python with PySide:** useful for experiments, but not selected as the foundation because a later performance rewrite would threaten source-model and API stability.
- **Unreal or Unity:** rejected because Atlas must remain standalone and engine-agnostic.
- **Qt Quick/QML as the initial shell:** deferred; Qt Widgets better matches the initial CAD/GIS-style desktop interaction model and accessibility needs.

## Consequences

Positive consequences:

- Native access to the computational geometry and rendering ecosystem.
- A mature desktop UI framework for docking, inspectors, keyboard input, accessibility, and high-DPI behavior.
- Clear separation between authoritative source, application commands, derived geometry, and presentation.
- A strong performance ceiling for large maps and incremental rebuilds.

Costs and risks:

- C++ increases implementation and memory-safety complexity.
- Build configuration and dependency management require discipline.
- Qt licensing and selected geometry-library licenses must be reviewed before distribution.
- Cross-platform support requires CI and validation rather than being assumed from portable source.
- The exact GPU canvas backend and computational-geometry library set remain technical decisions.

## Required validation before expanding implementation

1. Build a minimal CMake project with a Qt Widgets shell on Windows.
2. Compile and test `atlas-domain` without linking Qt.
3. Prove deterministic JSON normalization and a minimal empty-project round trip.
4. Prototype the canvas backend with camera-relative rendering, high-DPI behavior, picking, and keyboard accessibility.
5. Benchmark representative geometry and 10,000-object viewport workloads before selecting geometry libraries or optimizing hot paths.
6. Record geometry-library choices, licenses, algorithms, tolerance policy, and deterministic-version policy in a follow-up ADR.
7. Record the final package-manager lock and supported compiler/Qt versions in the repository build documentation.

## Persistence and migration impact

This decision does not change canonical domain meaning or the `.atlas` package contract. It establishes implementation technology only. JSON normalization, schema versions, migrations, unknown-data preservation, atomic save, and recovery behavior remain governed by the canonical specification.

## Follow-up decisions

- GPU canvas backend and rendering abstraction.
- Computational geometry libraries and license policy.
- Exact compiler, Qt, CMake, vcpkg baseline, and CI matrix.
- Canonical JSON library and formatting/normalization implementation.
- Plugin isolation and scripting API, before those capabilities enter their roadmap releases.
