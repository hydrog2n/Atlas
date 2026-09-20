# Atlas

**Parametric cartography for video-game worlds.**

Atlas is a standalone, game-agnostic editor for designing fictional and simulated worlds in real-world units. It combines direct vector drawing, parametric road construction, lane-level transport networks, hierarchical maps, and extensible semantic data in one 2D-first authoring environment.

> **Current version: `v0.1.0`** — Foundation and architecture phase

Atlas is currently in early development. The product model and core architectural rules are defined; implementation is focused on the project foundation, canonical data model, and first interactive editor shell.

## Why Atlas?

General-purpose vector tools can draw maps, but they do not understand lanes, junctions, elevation contexts, navigable topology, or game-world semantics. GIS tools understand geographic data, but they are not designed primarily for authoring fictional worlds. Atlas is intended to bridge that gap.

With Atlas, a road is more than a line. It is a measurable corridor with stable segments, lane identities, transitions, markings, connections, and metadata. A map is more than a flat image. It can contain multiple spatial levels, nested submaps, portals, semantic objects, and presentation rules while remaining portable between game engines.

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

### Parametric roads and transport networks

- Bezier, polyline, and fixed-radius road splines
- Real-world lane widths and configurable cross-sections
- Stable lane identity and lineage through splits, merges, additions, and removals
- Lane-level connections, junctions, turn movements, and routing semantics
- Detail that increases with zoom, from simplified corridors to lane-level geometry

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
| `v0.1.0` | Foundation | Repository and application skeleton, architecture contracts, canonical IDs and units, initial project schema, editor shell, and automated test foundation | **Current** |
| `v0.2.0` | Canvas and drafting | Infinite canvas, camera controls, grid, snapping, selection, measurements, reference overlays, and basic vector objects | Planned |
| `v0.3.0` | Parametric roads | Road splines, station anchors, segments, cross-sections, lane identity, derived road envelopes, markings, and semantic zoom | Planned |
| `v0.4.0` | Network editing | Ports, endpoint connections, T-junctions, lane continuations, basic turn movements, topology validation, and network inspection | Planned |
| `v0.5.0` | World semantics | Buildings, polygons, POIs, zones, display layers, basic spatial levels, tags, groups, schemas, styles, labels, and search | Planned |
| `v0.6.0` | Reliable editing | Inspector and hierarchy workflows, command transactions, undo/redo, diagnostics, repair actions, accessibility baseline, and crash recovery | Planned |
| `v0.7.0` | MVP integration | Versioned project packages, migrations, atomic save, autosave, incremental rebuilds, and PNG/SVG/canonical JSON export | Planned |
| `v0.8.0` | Vertical and nested worlds | Elevation profiles, vertical curves, bridges, tunnels, clearance, active-level controls, submaps, portals, and player-context preview | Planned |
| `v0.9.0` | Extensibility and scale | Advanced transitions and junctions, versioned prefabs, procedural objects, plugin exporter API, large-world optimization, and release hardening | Planned |
| `v1.0.0` | Production release | Complete documented V1 feature set, stable public project format, conformance suite, performance targets, accessibility review, and supported extension interfaces | Planned |

### Beyond `v1.0.0`

Potential future work includes advanced GIS and OpenStreetMap-derived imports, traffic and route simulation, derived 3D previews, collaborative editing, scripting and automation, procedural city generation, constraint-assisted interchange design, and live game-engine synchronization.

## Current Development Status — `v0.1.0`

Atlas is at the beginning of the roadmap.

**Current focus**

- Establish the application and repository structure.
- Encode the canonical project, map, object, road, and network data contracts.
- Implement stable identifiers, real-world units, tolerances, and schema versioning.
- Build the first editor shell and canvas integration points.
- Create test infrastructure for deterministic geometry, transactions, and persistence.
- Convert the master design specification into traceable implementation requirements.

**Not yet part of the current release**

- A production-ready road or lane editor
- Complete junction and routing workflows
- Vertical levels, submaps, portals, or prefabs
- Stable import/export or plugin APIs
- Production performance guarantees

The next milestone is **`v0.2.0` — Canvas and drafting**, which establishes the interactive authoring surface required by every later system.

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

Detailed build instructions, coding standards, contribution workflow, and issue templates will be added as the implementation stack is finalized.

## Documentation

The **Atlas Master Design Specification, Revision 2.0** is the canonical product, user-experience, architecture, persistence, validation, and conformance reference for the project. Repository documentation and implementation decisions should remain traceable to it.

## License

Licensing has not yet been finalized. Until a license is added, no permission is granted to copy, modify, or redistribute the source code.

---

Atlas is being built to make complex game worlds easier to design, reason about, validate, and move into production.
