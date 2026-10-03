# Atlas

**Parametric cartography for video-game worlds.**

Atlas is a standalone, game-agnostic editor for designing fictional and simulated worlds in real-world units. It combines direct vector drawing, parametric road construction, lane-level transport networks, hierarchical maps, and extensible semantic data in one 2D-first authoring environment.

> **Current version: `v0.5.1`** — Qt interaction usability patch for road and generic-point authoring

Atlas v0.5.1 hardens the v0.5.0 Qt workflow with direct handle and point manipulation, discoverable tools, visible measurement and segment feedback, and command-backed World/Local transforms. The patch adds no persisted feature scope.

## Why Atlas?

General-purpose vector tools can draw maps, but they do not understand lanes, junctions, elevation contexts, navigable topology, or game-world semantics. GIS tools understand geographic data, but they are not designed primarily for authoring fictional worlds. Atlas is intended to bridge that gap.

Atlas is designed to model roads as measurable corridors rather than decorative lines. In v0.5.0, RoadSplines have stable station anchors, RoadSegments, transactional edits, and reproducible derived envelopes; lane identities, transitions, markings, and network connections are planned for later releases. Maps can grow to include multiple spatial levels, nested submaps, portals, semantic objects, and presentation rules while remaining portable between game engines.

## Project Goals

- Author maps on an infinite 2D canvas using real-world coordinates and units.
- Build roads as parametric, lane-native corridors rather than decorative strokes.
- Keep physical geometry separate from semantic and navigational connectivity.
- Support bridges, tunnels, stacked spaces, interiors, and other vertical contexts.
- Organize large worlds through maps, submaps, layers, levels, groups, tags, and search.
- Adapt to different games through schemas, metadata, styles, prefabs, validators, and exporters.
- Preserve stable object identity through edits, save/load cycles, and migrations.
- Produce deterministic, inspectable output for downstream tools and game engines.

## Planned Capabilities

### Road authoring and transport networks

- **Shipped through v0.5.1:** polyline, cubic Bezier, and fixed-radius RoadSplines; station and segment editing; split, merge, reverse, measure, undo/redo; direct handle editing; a uniform-width derived envelope and diagnostics.
- **Planned for v0.6.0:** physically authored lanes, cross-section editing, lane ports, and lane identity/lineage.
- **Planned for v0.7.0:** explicit road connections, junctions, turn movements, and routing semantics.
- Detail that increases with zoom, from simplified corridors to lane-level geometry remains planned.

### Hierarchical and vertical maps

- Independent display layers and spatial levels
- Elevation profiles, bridges, tunnels, and clearance validation
- Nested maps for interiors, parking structures, facilities, and other detailed spaces
- Explicit portals and transforms between parent and child maps
- Context-aware previews of the map visible from a player location

### World authoring

- Buildings, footprints, blocks, parcels, polygons, zones, and points of interest
- Game-defined semantic object schemas without hard-coded genre assumptions
- Styles, labels, symbols, and semantic zoom rules
- Parametric prefabs and spline-distributed objects
- Reference images, drafting tools, snapping, constraints, and measurement

### Production workflow

- Transactional editing with undo and redo
- Versioned project packages, migrations, atomic saves, autosave, and recovery
- Incremental background rebuilding for large projects
- Actionable validation diagnostics and guided repair
- PNG, SVG, and canonical JSON export
- Extensible importers, exporters, validators, styles, and tools

## Design Principles

Atlas is built around a small set of non-negotiable ideas:

1. **World units are authoritative.** Pixels affect presentation, never physical dimensions.
2. **Roads are lane-native.** Road geometry is generated from a directional centerline and structured cross-section data.
3. **Source and derived data are distinct.** Generated geometry, previews, graphs, and caches can always be rebuilt from canonical source data.
4. **Geometry and connectivity are separate.** Crossing paths do not imply a navigable connection.
5. **Identity is stable.** Object and lane IDs survive ordinary geometric edits; array position is never identity.
6. **Hierarchy is explicit.** Maps, levels, layers, groups, and elevation each have distinct responsibilities.
7. **Edits are deterministic and reversible.** Topology changes are validated transactions with reliable undo and redo.
8. **The core remains engine-agnostic.** Engine integrations are adapters, not dependencies of the native project model.

## Roadmap

The roadmap is capability-based. Exact contents may evolve as implementation and testing expose better sequencing, but the architectural invariants will remain stable or change only through an explicit design decision and migration plan.

| Version | Milestone | Primary scope | Status |
| --- | --- | --- | --- |
| `v0.1.0` | Domain kernel and engineering contract | Stable typed records, canonical IDs and units, ownership validation, deterministic normalization, module boundaries, and conformance foundations | Completed |
| `v0.1.1` | CI/toolchain stabilization | Corrected Windows build environment, Visual Studio developer-shell initialization, and reproducible CI validation for the foundation build | Completed |
| `v0.1.2` | Desktop build workflow | Official Qt SDK desktop build, Qt runtime deployment, and VS Code run/debug integration | Completed |
| `v0.2.0` | Project package persistence and recovery | Canonical packages, deterministic serialization, atomic save, checkpoints, recovery, migrations, and unknown-data preservation | Completed |
| `v0.2.1` | Persistence contract hardening | Nested unknown-data preservation, authoritative-file inventory, input limits, compatibility fixtures, and expanded recovery tests | Completed |
| `v0.3.0` | Command transactions, dependencies, and validation | Previewable commands, commit/cancel, undo/redo, dependency invalidation, diagnostics, repair commands, and stale-result protection | Completed |
| `v0.3.1` | v0.3 validation audit and test hardening | Expanded invalidation, reference, precondition, rebuild, cache, repair, and deterministic undo evidence | Completed |
| `v0.4.0` | Canvas drafting and generic object editing | Infinite real-scale canvas, generic geometry, layers and levels, references, selection, inspection, and keyboard workflows | Completed |
| `v0.5.0` | RoadSpline stationing and derived geometry | Road splines, station anchors, segments, deterministic edits, derived envelopes, geometry diagnostics, and accessible desktop road authoring | Completed |
| `v0.5.1` | Qt interaction usability patch | Direct, accessible canvas interactions and transient transforms without persisted scope changes | **Current** |
| `v0.6.0` | Lane-native cross-sections and stable lineage | Physically scaled lanes and road elements, lane ports, cross-section editing, lineage, and deterministic reverse/reorder behavior | Planned |
| `v0.7.0` | Transport network and basic junctions | Explicit connections, T-junctions, lane mappings, movements, graph inspection, and topology validation | Planned |
| `v0.8.0` | World semantics, styles, and semantic zoom | Buildings, zones, POIs, parcels, schemas, metadata, layers, search, styles, labels, and scale-dependent representations | Planned |
| `v0.9.0` | MVP integration, export, and hardening | End-to-end authoring, PNG/SVG/canonical JSON export, recovery, performance, security, accessibility, and release hardening | Planned |
| `v1.0.0` | Production MVP | Supported, stable, game-agnostic real-scale world authoring with basic junction topology and published compatibility guarantees | Planned |
| `v1.1.0` | RoadTransitions and advanced junction editing | Lane topology changes, transition ownership, advanced movements, and transition-aware editing | Planned |
| `v1.2.0` | Elevation, bridges, and tunnels | Grades, vertical curves, clearance, stacked roads, active levels, and elevation-aware connectivity | Planned |
| `v1.3.0` | Hierarchical maps, submaps, and portals | Nested maps, lazy loading, PortalLinks, child-map navigation, and player-context preview | Planned |
| `v1.4.0` | Versioned parametric prefabs | Immutable prefab versions, instances, overrides, reconciliation, conflict handling, and detachment | Planned |
| `v1.5.0` | Road-adjacent and procedural world systems | Stable hosted bindings, spline-distributed objects, parcels, procedural generation, overrides, and regeneration | Planned |
| `v1.6.0` | Extensions, advanced export, and analysis | Safe plugin boundaries, importer/exporter APIs, validators, schemas, styles, queries, and analysis tools | Planned |
| `v1.7.0` | Production scale and long-term support | Large-world performance, bounded resource use, health reporting, compatibility, soak testing, and LTS readiness | Planned |

### Beyond `v1.0.0`

Potential future work includes advanced GIS and OpenStreetMap-derived imports, traffic and route simulation, derived 3D previews, collaborative editing, scripting and automation, procedural city generation, constraint-assisted interchange design, and live game-engine synchronization.

## Current Development Status — `v0.5.1`

Atlas now provides a hardened Qt desktop authoring workflow, including direct road-handle and generic-point editing, provisional drafting, visible measurement and segment feedback, World/Local road transforms, package open/save, and undo/redo.

**Current focus**

- Preserve the v0.4.0 real-scale canvas, generic geometry, layers and levels, references, selection, inspection, snapping, and keyboard workflows.
- Route road and generic-point edits through commands, explicit anchor resolution, preview/cancel where needed, undo/redo, diagnostics, and deterministic derived rebuilds.
- Preserve schema-1 packages; require explicit schema-2 migration before saving RoadSpline data into a schema-1 package.
- Maintain the implementation baseline recorded in [ADR-0001](.DESIGN/adr/ADR-0001-technology-baseline.md): C++23, Qt 6 Widgets, CMake, vcpkg, GoogleTest, and deterministic JSON source records.
- Keep architecture decisions, requirement-to-test traceability, and the decision backlog aligned with the project’s release scope.

**Not yet part of the current release**

- Lane-native cross-section editing and lineage, road connectivity and junctions, elevation, submaps, prefabs, and procedural authoring
- Production exporters, public plugin APIs, and production-scale performance guarantees

The next milestone is **`v0.6.0` — Lane-native cross-sections and stable lineage**.

## Project Boundaries

Atlas is:

- A standalone authoring application
- A 2D-first editor with optional 2.5D elevation data and derived previews
- A tool for fictional and simulated worlds in accurate physical units
- A portable source of structured map, network, and semantic data

Atlas is not:

- An Unreal Engine or Unity editor extension
- A civil-engineering certification tool
- A full geographic information system
- A photorealistic 3D world editor
- Tied to a particular genre, game, or engine

## Contributing

Atlas is in an architecture-sensitive stage. Before proposing a major feature or changing the project model:

1. Check the master design specification and existing architecture decisions.
2. Identify affected invariants, data contracts, migrations, and acceptance criteria.
3. Keep authoritative source data separate from regenerable output.
4. Include tests for deterministic behavior, undo/redo, validation, and save/load round trips where applicable.
5. Document intentional architectural changes before implementation.

The initial implementation stack is documented in [ADR-0001](.DESIGN/adr/ADR-0001-technology-baseline.md). Geometry-library selection, the GPU canvas backend, and exact toolchain versions require the validation described there before they become fixed dependencies.

Build prerequisites and commands are documented in [README.build.md](README.build.md).

Detailed coding standards, contribution workflow, and issue templates will be added as the implementation stack matures.

## Documentation

The [Atlas Master Design Specification, Revision 3.0](.DESIGN/Atlas_Master_Design_Specification_Revision_3_0.md) is the canonical product, user-experience, architecture, persistence, validation, release, and conformance reference for the project. Repository documentation and implementation decisions should remain traceable to it. The [Implementation Roadmap](ROADMAP.md) sequences its requirements into releases and work packets.

## License

Licensing has not yet been finalized. Until a license is added, no permission is granted to copy, modify, or redistribute the source code.

---

Atlas is being built to make complex game worlds easier to design, reason about, validate, and move into production.
