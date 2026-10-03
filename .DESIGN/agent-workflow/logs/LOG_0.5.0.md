# Atlas v0.5.0 Development Log

This log records the agent-assisted development and release workflow for Atlas v0.5.0.

## Phase 1 — Analyze & Plan (revision 2)

Result: completed
Phase: 1 — Analyze & Plan
Version: `v0.5.0`
Note: This revision replaces the 2026-09-24 draft after an architectural critique found the release scope underspecified in several places and too large a single implementation gate. It subdivides the release into internal checkpoints and gives each checkpoint a comprehensive, self-contained technical requirement set. The roadmap's public version remains `v0.5.0`; checkpoint tags (`v0.5.0-A` ... `v0.5.0-G`) are internal tracking only and do not appear in `ROADMAP.md`, application version metadata, or release status until the full v0.5.0 completion statement is satisfied.

### Repository State

- Atlas v0.4.0 is the current validated release: Qt canvas, generic MapObjects, layers/levels, persistence, snapping/constraints, selection/hit testing, calibration, keyboard canvas navigation, and full v0.3.x/v0.4.x regression coverage (101 headless tests, 4 UI tests).
- No RoadSpline, RoadSegment, StationAnchor, analytic curve primitive, road envelope, road lineage, or road command module exists yet.
- Current geometry code (`atlas-geometry`) provides tolerance policy, grid/angle/distance snapping, deterministic candidate ordering, and calibration — but no arc-length evaluation, moving frames, or curve primitives.
- Current implemented persistence schema generation: `1`. (Wording corrected from "schema version 1" to avoid conflating the persistence schema generation with the application version, geometry-engine version, or specification revision — all independent compatibility dimensions per the canonical spec's Resolved Decisions.)

### Release Scope

- Objective: establish the authoritative road centerline and stable plan-stationing model before lane topology and network connectivity.
- User-visible outcome: a user can draw, edit, reverse, measure, split, and merge open road corridors while station anchors remap deterministically and derived envelopes remain reproducible and disposable.
- Ship-in-version items (unchanged from the roadmap; the full technical requirements for each are delineated per checkpoint below):
  - Open RoadSpline using polyline, cubic-Bezier, and fixed-radius primitives with stable primitive/control-point IDs.
  - Canonical Start-to-End orientation, two-dimensional plan arc-length stationing, sampled frames, analytic/tolerance-bounded evaluation.
  - StationAnchor with a remap-signature structure and all five edit-affinity modes.
  - Anchor remap preview and explicit resolution for ambiguous or out-of-domain edits.
  - Automatic one-segment full-domain RoadSegment creation for every new RoadSpline.
  - RoadSegment half-open interval coverage, split, boundary move, equivalence-checked merge, and lineage.
  - Initial uniform cross-section placeholder sufficient to derive a road envelope.
  - Offset diagnostics for cusps, tight radii, self-intersections, miter limits, and bounded invalid previews.
  - Compound RoadSpline reverse command with station, direction, side, and reference remapping.

**Explicit non-goals** (unchanged): no lane-level network or automatic intersections; no topological lane transitions; no elevation profile; no junctions, portals, submaps, prefabs, or procedural roadside systems; no lane-native cross-section editing beyond the uniform placeholder.

### Required ADRs

| ADR | Title | Topic | Decision recorded this phase | Formal draft required before |
|---|---|---|---|---|
| ADR-0006 | Road Curve Primitive Definitions and Continuity | Polyline/Bezier/arc definitions, C0-only continuity, degenerate rejection | Yes — see Checkpoint A | Checkpoint A |
| ADR-0007 | Station Parameterization and Inversion Contract | Station/parameter conversion, inversion tolerance, lookup-table policy | Yes — see Checkpoint A | Checkpoint A |
| ADR-0008 | StationAnchor Remap Signature and Primitive Lineage | Anchor data model, remap-table mechanism | Yes — see Checkpoint C | Checkpoint C |
| ADR-0009 | Road Source Validity vs. Derived Envelope Validity | Offset/envelope failure commit policy | Yes — Model B accepted, see Checkpoint F | Checkpoint F |
| ADR-0010 | Road Persistence Record Architecture | First-class road records, schema generation 2 migration | Yes — first-class records accepted, see Checkpoint B / G | Checkpoint B (data model) / Checkpoint G (persistence + migration) |

All five ADRs must exist as accepted documents in `.DESIGN/adr/` before their listed checkpoint's implementation begins, mirroring the ADR-0003/0004/0005 pattern from v0.4.0. Drafting them is a follow-up action, not yet performed in this phase.

### Internal Implementation Checkpoints

Each checkpoint is self-contained: it lists its own complete technical requirements, not just a cross-reference. A checkpoint may not begin until its required ADR(s) are accepted, and a dependent checkpoint may not begin until its predecessor's mini-gate is met.

---

#### Checkpoint A — Curve Kernel (`v0.5.0-A`, part of `R050-001`)

**Requires:** ADR-0006, ADR-0007 accepted. **Depends on:** v0.4.0 (`atlas-geometry`).

**Technical requirements — curve primitives:**
- **Polyline primitive**: exactly one straight line segment between two stable control points (start, end). A multi-vertex polyline RoadSpline is a sequence of these single-segment primitives, each with its own ID; there is no multi-vertex polyline primitive type.
- **Bezier primitive**: cubic only (4 stable control points: start, control1, control2, end). Quadratic and arbitrary-degree are out of scope for v0.5.0.
- **Fixed-radius primitive**: a circular arc defined by two stable control points (start, end), a scalar radius, and an `arcSide` enum (`left`/`right` of the Start→End chord) selecting the minor arc on that side. Radius MUST be `>= half the chord length`; a smaller radius is a structural validation error, not a silent clamp. Major arcs and center-based parameterization are not supported in v0.5.0.
- **Continuity between primitives**: positional continuity only (C0). Adjacent primitives share the same control-point ID at their joint. Tangent discontinuities are permitted and treated as explicit corners; no tangent (C1) continuity is enforced or solved for.
- **Degenerate/zero-length primitives**: rejected as committed source (structural validation error). May exist transiently during interactive preview per `VAL-CORE-002`.
- **Reversing-tangent Bezier segments**: a cubic Bezier primitive whose tangent reaches zero or reverses direction within its domain is a structural validation error (required for station monotonicity below).
- **Naming clarification**: `RoadSpline` is a piecewise parametric curve composed of heterogeneous primitives (polyline/cubic-Bezier/arc segments) joined at shared control points — it is the product/domain name and does not imply a single B-spline or NURBS representation.

**Technical requirements — station parameterization and inversion contract:**
- Geometry-engine interface (application code never manipulates raw curve parameters):
  ```text
  evaluateAtStation(s) -> { position, tangent, normal }
  stationOfPrimitiveParameter(primitiveId, t) -> s
  primitiveParameterAtStation(s) -> { primitiveId, t }
  ```
- Station is monotonically non-decreasing along the RoadSpline by construction, guaranteed by the primitive validation rules above.
- Primitive parameter domains are half-open `[start, end)` except the RoadSpline's final primitive, which is closed — mirroring the `SEGM-CORE-001`/`SEGM-CORE-002` half-open convention so a station never has two owning primitives.
- Forward conversion (`stationOfPrimitiveParameter`) is analytic/closed-form for polyline and arc primitives; numerical (tolerance-bounded quadrature) for Bezier primitives.
- Inversion (`primitiveParameterAtStation`) uses a bracketed Newton/bisection hybrid: convergence tolerance = `stationEpsilon` (1e-4 m, per the existing tolerance table), iteration cap of 64, deterministic bisection fallback if Newton fails to converge or leaves the bracket.
- Values within `stationEpsilon` of a primitive boundary snap exactly to that boundary; ties at a shared joint resolve to the earlier primitive's half-open upper bound.
- Degenerate curves (already rejected at the primitive level) never reach inversion; an out-of-domain station is an explicit error, not a silent failure.
- Arc-length lookup tables MAY be used as a derived acceleration structure but are never authoritative, are never persisted, and are invalidated/regenerated whenever source geometry, tolerance policy, or geometry-engine version changes.

**Ship:** primitive types above; `evaluateAtStation`, `stationOfPrimitiveParameter`, `primitiveParameterAtStation`; tangent/normal (moving frame); deterministic sampling; primitive-level structural validation (degenerate, reversing-tangent, sub-minimum-radius rejection).

**Tests:** unit (per-primitive evaluation, length, frame), property (station monotonicity, inversion round-trip within `stationEpsilon`), golden fixtures per primitive type with recorded geometry-engine version.

**Mini-gate:** analytic/tolerance-bounded evaluation is deterministic and version-tagged; inversion round-trips within tolerance; degenerate/invalid primitives are rejected at construction, not silently clamped.

---

#### Checkpoint B — Road Source Model (`v0.5.0-B`, completes `R050-001` data model)

**Requires:** ADR-0010 accepted (record-architecture direction; full persistence wiring deferred to G). **Depends on:** Checkpoint A.

**Technical requirements — record architecture:**
- `RoadSpline` and `RoadSegment` are first-class authoritative record families (new top-level arrays alongside `MapObjects`), not typed extensions of the generic MapObject model.
- `RoadSpline` fields: `id`, `mapId`, ordered primitive list (shared-joint control-point IDs), canonical `direction` (Start→End), computed `stationDomain` (derived, not persisted independently of the primitives that produce it), `segmentIds` (populated once RoadSegment exists), `styleRef`, `metadata`, `networkRefs` (reserved, unused until v0.7).
- `RoadSegment` fields (stub in this checkpoint, commands arrive in D): `id`, `roadSplineId`, `startAnchorId`, `endAnchorId`, placeholder `crossSectionState`.
- Ownership: every `RoadSpline` MUST belong to exactly one Map (`MAP-CORE-001`); it MUST NOT span coordinate contexts.
- Identity: primitive IDs and control-point IDs are stable and independent of array position (`ARCH-CORE-010`); a RoadSpline created new receives exactly one full-domain RoadSegment (`ARCH-CORE-003`, `SEGM-CORE-002`); RoadSegments are ordered intervals on their parent RoadSpline with no independent centerline (`ARCH-CORE-004`).

**Ship:** `RoadSpline` record; full-domain `RoadSegment` stub record (data only — commands arrive in checkpoint D).

**Tests:** unit (record construction, one-Map ownership, ID stability across construction), deterministic normalization test.

**Mini-gate:** domain records normalize deterministically; `MAP-CORE-001` and `ARCH-CORE-002`/`003`/`004` are structurally encoded and testable without any command layer yet.

---

#### Checkpoint C — StationAnchor (`v0.5.0-C`, `R050-002`)

**Requires:** ADR-0008 accepted. **Depends on:** Checkpoints A, B.

**Technical requirements — StationAnchor data model:**
```text
StationAnchor
    id
    resolvedStation        // current semantic location; the only value application code treats as authoritative
    affinity                // Start locked | End locked | Geometry locked | World locked | Normalized
    remapSignature
        normalizedStation   // fraction of total plan length
        primitiveId         // nearest stable curve primitive at time of last resolve
        primitiveT          // local primitive parameter
        worldPosition       // last resolved world position
```
- `remapSignature` fields are remapping evidence, not competing authoritative locations — only `resolvedStation` is authoritative.
- Default affinity for segment boundaries and network attachments MUST be Geometry locked (`STAT-CORE-002`).
- Affinity behaviors: Start locked preserves distance from RoadSpline Start; End locked preserves distance from End; Geometry locked follows the same stable curve primitive and local parameter when it survives; World locked projects the previous world position to the edited RoadSpline within a configured search distance; Normalized preserves the fraction of total plan length.

**Technical requirements — primitive lineage and remap table:**
- Every primitive-structural edit (split, merge, insert, delete, reverse) MUST produce a deterministic, transaction-local parameter remap table before any anchor remapping runs:
  ```text
  old primitive P
      [0.00, 0.37) -> P1 [0.00, 1.00)
      [0.37, 1.00) -> P2 [0.00, 1.00)
  ```
- Anchor remapping consults this table first. Nearest-point/world-locked geometric search is a fallback only for anchors whose referenced primitive no longer exists and has no recorded remap entry (e.g., deleted outright rather than split/merged).
- If the preferred remapping cannot be satisfied unambiguously, the edit MUST preview the affected anchors and require explicit resolution before commit (`STAT-CORE-003`).
- Shortening a RoadSpline past an anchor MUST clamp only during interactive preview; commit requires Move, Delete dependent, or Cancel (`STAT-CORE-004`).
- A geometry edit and all accepted anchor remaps MUST commit as one transaction (`STAT-CORE-005`).

**Ship:** `StationAnchor` per the data model above; primitive lineage/remap-table mechanism; deterministic remap per affinity mode; ambiguity detection and preview diagnostic.

**Tests:** property (remap determinism across generated edit sequences), one unit test per affinity mode, the split-primitive remap-table scenario (`P → P1/P2`) above, ambiguity-preview test.

**Mini-gate:** `AC-003` passes for Geometry-locked anchors; ambiguous remaps block commit until explicitly resolved.

---

#### Checkpoint D — Road Editing Commands (`v0.5.0-D`, `R050-003`)

**Requires:** Checkpoints A–C complete. No new ADR.

**Technical requirements — commands and reversal contract:**
- Commands: create, edit-control-point, extend, shorten, reverse, delete — each implemented through the existing v0.3 `CommandProcessor` (preview/commit/cancel/undo/redo/stale-revision guard, consistent with the v0.3/v0.4 command contract).
- Anchor remap executes inside the same transaction as the geometry edit (`STAT-CORE-005`); shortening past an anchor requires the Move/Delete dependent/Cancel resolution defined in checkpoint C.
- Reversal is an explicit compound transaction (`ROAD-CORE-003`) that remaps: station values (new station = totalLength − old station), canonical direction, left/right groups, directional fields, ports (reserved), and dependent references.
- **Formal equivalence definitions** used to evaluate this checkpoint's invariants:
  - *Exact identity*: identical stable IDs and exactly equal canonical values.
  - *Normalized source equivalence*: canonical source meaning and identity are equivalent; irrelevant serialization ordering/formatting differences are ignored (comparison basis for `TEST-CORE-003` save-load round trips).
  - *Geometric equivalence*: geometry represents the same physical result within the named tolerance policy, even if source representation differs.
- **Reverse-twice invariant**: `reverse(reverse(R))` MUST be **normalized-source-equivalent** to `R`, with stable IDs preserved or restored — not merely geometrically equivalent. A reversal implementation that regenerates primitive IDs unnecessarily fails this invariant even if it passes a geometric-only comparison.

**Ship:** create/edit-control-point/extend/shorten/reverse/delete commands; atomic anchor remap; explicit reversal contract.

**Tests:** transaction tests (preview/commit/cancel/undo/redo/stale revision) for every command; reverse-twice property test asserting normalized-source-equivalence with stable IDs; shorten-with-anchor resolution tests for all three resolutions.

**Mini-gate:** `AC-001`'s source-model half (one full-domain RoadSegment + valid anchors on creation) passes; reverse-twice invariant passes.

---

#### Checkpoint E — Segmentation (`v0.5.0-E`, `R050-004`)

**Requires:** Checkpoint D complete. No new ADR.

**Technical requirements — coverage and split/merge rules:**
- A RoadSpline MUST have one or more RoadSegments whose ordered intervals cover the complete station domain without gaps or overlap (`SEGM-CORE-001`); intervals are half-open except the final segment, which is closed.
- Split resolves the requested split to a StationAnchor outside endpoint tolerance; replaces the original segment with upstream/downstream children; copies properties by schema-defined copy rules; records `parentSegmentId` in both lineage records; preserves each full-length cross-section element ID on the upstream child and creates a new downstream ID linked by `continuesFrom`; moves station-scoped attachments to the correct child without changing their object IDs; invalidates only affected derived geometry/validation regions; commits the split and all reference updates as one transaction.
- Merge is allowed directly only when cross-section states, style overrides, schema properties, spatial references, and boundary attachments are equivalent (per the formal equivalence definitions in checkpoint D); otherwise the editor must show a field-by-field conflict resolution and never silently drop a value.
- A successful merge retains the upstream segment ID by default, archives both input lineage records, and remaps compatible downstream references deterministically (`SEGM-CORE-004`).
- Moving a segment boundary changes only interval ownership unless the user explicitly chooses to scale/move an attached transition (`SEGM-CORE-005`).

**Ship:** split, boundary move, equivalence-checked merge, lineage records, station-scoped attachment/reference remapping, half-open coverage invariant enforcement.

**Tests:** property test asserting complete non-overlapping coverage under fuzzed edit sequences, split/merge/lineage unit tests, split-with-undo exact-normalized-state test, conflict-resolution test for non-equivalent merge attempts.

**Mini-gate:** `SEGM-CORE-001`–`005` evidenced; split-then-undo restores exact prior normalized state.

---

#### Checkpoint F — Envelope Geometry (`v0.5.0-F`, `R050-005`)

**Requires:** ADR-0009 accepted. **Depends on:** Checkpoint E.

**Technical requirements — offset/envelope failure policy (accepted: Model B):**
- A structurally valid RoadSpline MAY commit even when its required road envelope cannot currently be derived. Envelope generation failure (cusp, tight radius, self-intersection, orientation inversion) produces an `Error` diagnostic and a bounded invalid preview; it does NOT block the source commit.
- Operations that require a valid envelope (e.g., later export) remain blocked until the geometry is repaired.
- Source-level failures (NaN coordinates, corrupted ownership, invalid references, impossible primitive definitions, invalid station domains) remain uncommittable regardless of this policy — these are distinct from derived-geometry failures.

**Technical requirements — envelope generation:**
- Evaluate a stable two-dimensional moving frame along the RoadSpline; tangent follows the canonical direction; left normal defines positive lateral offset (`GEOM-CORE-001`: from analytic/tolerance-bounded curve representations, never from view-dependent render tessellation).
- Offset generation MUST detect cusps, local radius smaller than the requested offset, self-intersection, and orientation inversion (`GEOM-CORE-002`).
- A failed offset MUST produce an Error diagnostic and a bounded preview; the system MUST NOT commit silently corrupt geometry (`GEOM-CORE-003`).
- Polyline corners MUST use an explicit join style: round (default), miter (requires a project miter limit), or bevel (`GEOM-CORE-004`).
- Derived output MUST be deterministic for identical source data, tolerance policy, geometry-engine version, and export profile (`GEOM-CORE-005`) — every derived-geometry cache carries an explicit algorithm/tolerance version alongside its hash.
- The initial cross-section is a uniform placeholder sufficient to derive a road envelope; no lane-native cross-section editing is in scope (deferred to v0.6.0).
- Envelope invalidation is range-scoped, reusing the v0.3 dependency graph's station-range invalidation records.

**Ship:** uniform cross-section placeholder; offset/join generation; cusp/tight-radius/self-intersection/orientation-inversion diagnostics; bounded invalid preview; Model B commit behavior wired into the command layer.

**Tests:** golden envelope fixtures (version-tagged), failure-injection tests for each diagnostic category, determinism test (identical source + tolerance + engine version ⇒ identical derived hash), range-scoped invalidation test.

**Mini-gate:** `AC-001`'s envelope half passes; invalid offsets never silently commit corrupt derived geometry; a failed envelope leaves the RoadSpline source committed with an `Error` diagnostic, per the accepted Model B.

---

#### Checkpoint G — Conformance and Compatibility (`v0.5.0-G`, `R050-006`)

**Requires:** Checkpoints A–F complete; ADR-0010's migration path drafted.

**Technical requirements — persistence and migration:**
- `RoadSpline`/`RoadSegment` become new top-level authoritative arrays in the package format, additive and backward-compatible with existing packages (empty road arrays on old packages).
- This is a persistence schema generation increment (generation `1` → `2`), not an application-version change. Migration MUST be transactional, create a recoverable backup, run within one transaction, emit a migration report, and never overwrite the last known readable version on failure (`SAVE-CORE-005`).
- Never serialize raw collection indices or raw curve parameters as semantic locations — stations and StationAnchor IDs are the only persisted semantic locations.
- `minimal-road` (one RoadSpline, one full-domain RoadSegment, simple two-lane placeholder cross-section) and `split-road` (multiple segments, anchors, metadata, lane lineage) become retained canonical fixtures.

**Ship:** full persistence integration for the new road record families; schema generation `1→2` transactional migration with backup and report; `minimal-road` and `split-road` canonical fixtures; consolidated property/golden/fuzz/security suite; performance baseline; full requirement-to-test traceability pass.

**Tests:** everything in the Test Plan below, plus the generation-1-to-2 migration test (including induced-failure/backup behavior) and a full v0.4.x regression replay.

**Mini-gate:** the roadmap's v0.5.0 version-completion statement is fully satisfiable — every listed packet merged, every gate evidenced, all earlier-release tests green, all retained fixtures resave without semantic drift.

---

### Scope Ledger

| Checkpoint | Roadmap packet | Owner / planned files | Required evidence | Status |
|---|---|---|---|---|
| A — Curve Kernel | `R050-001` (part) | `atlas-geometry` curve/station modules | Unit, property, golden per-primitive fixtures | planned |
| B — Road Source Model | `R050-001` (part) | New `atlas-domain` road records | Unit, normalization determinism | planned |
| C — StationAnchor | `R050-002` | Road source model + application remap planner | Property, per-affinity unit, remap-table scenario, ambiguity preview | planned |
| D — Road Editing Commands | `R050-003` | `atlas-application` road commands | Transaction tests, reverse-twice property test | planned |
| E — Segmentation | `R050-004` | Road segment source model + commands | Coverage property test, lineage/merge unit tests | planned |
| F — Envelope Geometry | `R050-005` | Derived road geometry module | Golden envelope, failure-injection, determinism tests | planned |
| G — Conformance & Compatibility | `R050-006` | `tests/atlas/road`, fixtures, persistence/migration | Full suite + migration + regression replay | planned |
| v0.4.x regression and compatibility suite | — | Existing domain/persistence/application/geometry/render/integration/UI tests | Full headless/Qt workflow replay | validated by v0.4.0 evidence |
| Release gates | All checkpoints | All R050 owners | AC-001, AC-003, reversal, split/merge, offset-failure, determinism, compatibility evidence | planned |

### CORE Requirements, Acceptance Criteria, Release Gates

- First-enforced CORE requirements: `ARCH-CORE-002`, `ARCH-CORE-003`, `ARCH-CORE-004`, `GEOM-CORE-001`–`005`, `MAP-CORE-001`, `ROAD-CORE-001`–`003`, `SEGM-CORE-001`–`005`, `STAT-CORE-001`–`005`.
- Acceptance criteria: `AC-001` (checkpoints D + F), `AC-003` (checkpoint C).
- Release gates: AC-001 full pass, AC-003 pass, reverse-twice equivalence, split/merge/station/tolerance property coverage, invalid-offset bounded-preview-without-commit-corruption, and full v0.4.x/v0.3.x regression + fixture replay.

### Test Plan

- Unit: curve evaluation, length, frames, station conversion, endpoint tolerance, IDs, lineage, tolerance policy, per-affinity StationAnchor behavior.
- Property: station monotonicity, inversion round-trip, split/merge coverage completeness, reverse-twice normalized-source-equivalence, anchor remap determinism, deterministic sampling, normalized save-load.
- Golden geometry: polyline/Bezier/arc evaluation, frames, station, joins, envelopes — each fixture records schema generation and geometry-engine version (`TEST-CORE-002`).
- Transaction: preview/commit/cancel, stale revisions, undo/redo, reverse, shorten-with-anchor-resolution, split, merge, failed-envelope-does-not-block-source-commit.
- Integration: create minimal road → edit control point → remap anchor → split → merge → save/reopen → reverse twice → compare normalized source.
- Persistence: `minimal-road`/`split-road` round trips, unknown road fields, malformed curve records, duplicate IDs, invalid ownership, schema generation 1→2 migration (including failure/backup behavior per `SAVE-CORE-005`).
- Fuzz/security: degenerate curves, zero-length primitives, extreme coordinates, invalid radii, malformed joins, bounded resource inputs.
- Performance: named hardware/dataset, cold/warm cache, analytic evaluation, station queries, envelope generation, median and 95th percentile.
- Every added or modified test includes a concise purpose comment immediately before the test, consistent with the v0.3.x/v0.4.x suites.

### Documentation/Versioning Impact

- Phase 2 documents the road source/derived boundary and requirement traceability per checkpoint; it does not perform release promotion.
- Phase 4 promotes application metadata to v0.5.0 and updates roadmap, README, build guide, canonical current status, changelog, and evidence references — only once checkpoint G's mini-gate and the full version-completion statement are met.
- Schema generation `2` is an independent compatibility dimension from the application version; its migration path, backup behavior, and report are evidenced in checkpoint G, not silently bundled into the application version bump.
- Preserve geometry-engine, exporter, plugin API, and specification versions unless their own contracts change.

### Persistence/Compatibility Impact

- Road records become first-class authoritative arrays (`RoadSplines`, `RoadSegments`) alongside `MapObjects`, per the accepted persistence-architecture decision.
- This requires a persistence schema generation increment (`1 → 2`), additive and backward-compatible; existing packages without road arrays remain fully readable.
- Never serialize raw collection indices or raw curve parameters as semantic locations — stations and StationAnchor IDs are the only persisted semantic locations.
- `minimal-road` and `split-road` become retained canonical fixtures (checkpoint G).
- Any further schema, ownership, unit, tolerance-hash, or migration change beyond what is described here requires its own ADR and compatibility test.

### Risks / Decisions

**Stop conditions** (checkpoint-scoped):
- Stop if a curve representation cannot provide deterministic length, frame, station, and sampling behavior independent of view tessellation (checkpoint A).
- Stop if anchor remapping is ambiguous without an explicit preview and Move/Delete dependent/Cancel resolution (checkpoint C).
- Stop if split/merge cannot preserve stable IDs, lineage, complete half-open coverage, or references atomically (checkpoint E).
- Stop if invalid offsets can silently commit corrupt derived geometry, or if the accepted Model B policy cannot be implemented without violating `GEOM-CORE-003` (checkpoint F).
- Stop if road ownership conflicts with the one-Map rule, or if self-crossings are interpreted as network connections (any checkpoint).
- Stop if the schema generation 1→2 migration cannot provide a transactional path with backup and report (checkpoint G).

**ADR or design decision needed:** ADR-0006 through ADR-0010 must be formally drafted and accepted before their respective checkpoints begin, per the Required ADRs table above. The decisions themselves are recorded in this phase; only the formal ADR documents remain outstanding.

### Phase Gate

- Ready to implement checkpoint A: yes, once ADR-0006 and ADR-0007 are drafted and accepted as formal documents (the underlying decisions are already recorded above).
- Ready to implement the complete v0.5.0 release: no — no checkpoint is implemented or validated yet, and ADR-0008/0009/0010 remain to be drafted ahead of their respective checkpoints.
- Ready for release integration: no.

---

## Phase 2 — Implement (Checkpoint A)

Result: partial  
Phase: 2 — Implement  
Version: `v0.5.0`
Checkpoint: `v0.5.0-A` — Curve Kernel (`R050-001`, partial)

**Implementation:**
- Formalized and accepted ADR-0006 (primitive semantics/continuity) and ADR-0007 (station parameterization/inversion) before implementation. Updated the decision backlog to reflect those accepted checkpoint-A decisions; the final long-term geometry-library choice remains deferred under ADR-0004.
- Added typed polyline-segment, cubic-Bezier, and fixed-radius minor-arc primitives behind the Atlas-owned `CurveKernel` interface. No third-party geometry types or Qt/OpenGL dependencies enter the API.
- Added construction-time checks for empty/duplicate primitive IDs, stable control-point identity, exact C0 shared joints, non-finite coordinates, degenerate primitives, zero Bezier tangent points, impossible arc radii, closed curves, and unsupported query domains.
- Added analytic line/arc length and evaluation; deterministic adaptive-Simpson cubic-Bezier length with a depth cap of 24; bracketed Newton/bisection station inversion with 64-iteration cap and deterministic midpoint fallback; unit tangent/left-normal frames; station-boundary ownership; deterministic interval sampling with a sample-count bound.
- Added algorithm identifier `atlas-curve-kernel-1`; no curve lookup tables or sampled geometry are persisted.
- Added version-tagged golden records for line, cubic Bezier, and circular arc outputs (`schemaGeneration: 1`, `geometryEngineVersion: atlas-curve-kernel-1`) and 14 purpose-commented curve-kernel tests.

**Scope Ledger:**
- Checkpoint A / `R050-001`: implemented and focused-tested for primitive definitions, evaluation, lengths, frames, station conversion/inversion, structural validation, deterministic sampling, and versioned goldens. No standalone mini-gate claim is made for requirements beyond these assertions.
- Checkpoints B–G: planned and untouched. No RoadSpline/RoadSegment authoritative records, StationAnchor, editing commands, segmentation, envelope generation, persistence migration, or road fixtures were added.
- v0.4.x regression and compatibility suite: preserved and passing.

**Requirements addressed:**
- Checkpoint-A portions of `ROAD-CORE-001`, `STAT-CORE-001`, `GEOM-CORE-001`, and `GEOM-CORE-005` through the open primitive model, plan-length station contract, tessellation-independent evaluation, deterministic algorithm version, and tests.
- `ROAD-CORE-002`, Map ownership, RoadSegment coverage, anchor remapping, and derived envelope requirements are not implemented by this checkpoint.

**Files/components changed:**
- Added `src/atlas/geometry/curve.hpp` and `src/atlas/geometry/curve.cpp`.
- Added `tests/atlas/geometry/curve_tests.cpp` and `tests/fixtures/geometry/curve-kernel-v1.json`.
- Updated `CMakeLists.txt`, ADR-0007's explicit integration recursion cap, and `.DESIGN/decision-backlog.md` for accepted ADR-0006/0007 status.

**Focused Checks:**
- `atlas_geometry_tests`: 26 out of 26 passed.
- Full headless CTest: 115 out of 115 passed, no failures or skips.
- Windows/Qt configure and build passed.
- Architecture governance passed.
- `git diff --check` passed.
- Editor diagnostics for curve source, tests, ADRs, and backlog reported no errors.

**Persistence/compatibility impact:**
- None to authoritative packages. The checkpoint adds only a derived geometry kernel and test fixture; package schema generation remains `1`, and no road records or station lookup tables are persisted.

**Issues:**
- Deviations from Phase 1: none for checkpoint A. Internal sampling additionally rejects requests exceeding an explicit sample-count limit to keep work bounded.
- Remaining checkpoint-A work: the curve-kernel golden fixture and direct station monotonicity/round-trip evidence now pass; no checkpoint-A item remains partial within its declared boundary.
- Unimplemented release work: checkpoints B–G and the remaining `R050-001` source-record work remain unimplemented.
- ADR or design decision needed before subsequent checkpoints: ADR-0008 before C, ADR-0010 before B's record model (with migration in G), and ADR-0009 before F. The final geometry-library selection remains deferred and must be revisited before broader road-geometry adoption if the evidence requires it.

**Phase Gate:**
- Ready for validation: yes for checkpoint A.
- Ready for release integration: no for v0.5.0.
- Reason: the curve-kernel checkpoint is implemented and focused-tested, but the v0.5.0 release requires checkpoints B–G, AC-001/AC-003 evidence, migration/fixture coverage, and all remaining road release gates.

---

## Phase 3 — Validate & Fix (Checkpoint A)

Result: completed  
Phase: 3 — Validate & Fix  
Version: `v0.5.0-A`

**Validation:**
- Initial validation attempt was blocked by an uninitialized MSVC shell (`<array>` could not be found); this was an environment failure, not a source failure. The check was rerun in one explicit `VsDevCmd.bat` developer shell.
- Headless configure and build passed.
- Headless CTest passed: 115 out of 115 tests, with no failures or skips.
- Windows/Qt configure and build passed.
- Architecture governance check passed.
- `git diff --check` passed.
- The versioned golden fixture exists and contains `schemaGeneration: 1` and `geometryEngineVersion: atlas-curve-kernel-1`.

**Scope ledger result:**
- Checkpoint A / `R050-001` curve-kernel scope: validated.
- Checkpoints B–G: intentionally not in this checkpoint and remain planned.
- v0.4.x regression and compatibility suite: remains green within the 115-test headless run.

**Conformance:**
- Polyline, cubic Bezier, and fixed-radius minor-arc primitives have executable evaluation and length evidence.
- Tangent and left-normal frame behavior is tested.
- Station monotonicity, station/parameter round trips, endpoint tolerance, shared-boundary ownership, bounded inversion, deterministic sampling, and explicit out-of-domain errors are tested.
- Invalid primitive IDs, shared control-point mismatches, zero-length/closed curves, impossible radii, zero Bezier tangents, non-finite arithmetic, and unresolved arc sweeps are tested.
- Versioned golden geometry is validated against the declared kernel identifier.
- Source/derived separation is preserved: the kernel adds no persistence records or lookup-table source state.

**Tests/checks executed:**
- Focused `atlas_geometry_tests`: 26 out of 26 passed.
- Full headless suite: 115 out of 115 passed.
- Windows/Qt build: passed.
- Architecture governance: passed.
- `git diff --check`: passed.

**Defects:**
- Defects found: none in the implementation. The first build failure was the known MSVC developer-shell environment issue.
- Fixes made: no production fix required; validation was rerun with the documented developer-shell initialization.
- Regression coverage added: no additional tests were needed after the focused 26-test run already covered the mini-gate and edge cases.
- Remaining limitations: no RoadSpline source records, RoadSegment, StationAnchor, commands, envelope engine, persistence migration, or later checkpoint behavior is claimed here.
- Stop-ship defects: none for checkpoint A.

**Phase Gate:**
- Ready for release integration: no for the public v0.5.0 release.
- Ready to proceed to checkpoint B: yes.
- Reason: Checkpoint A's mini-gate is evidenced: deterministic/versioned evaluation, station inversion within tolerance, and construction-time rejection of invalid primitives all pass. Checkpoint B may begin after ADR-0010 is accepted for the road record architecture.

---

## Phase 2 — Implement (Checkpoint B)

Result: completed  
Phase: 2 — Implement  
Version: `v0.5.0`
Checkpoint: `v0.5.0-B` — Road Source Model (`R050-001`, source-model portion)

**Implementation:**
- Accepted ADR-0010 and updated the decision backlog: RoadSpline and RoadSegment are first-class authoritative record families, with persistence schema generation 2 reserved for checkpoint G migration and package integration.
- Added first-class `RoadSpline` and `RoadSegment` domain records with stable IDs, explicit `mapId` ownership, primitive source payloads, canonical direction, segment references, anchor boundary IDs, placeholder cross-section state, lineage, metadata, and reserved network references.
- Added deterministic project normalization for road arrays, sorted by stable record ID; derived station length and lookup data are not persisted.
- Added validated normalized-source parsing, duplicate ID rejection, RoadSegment parent validation, and same-Map ownership enforcement.
- Kept package writer/reader and schema generation unchanged; persistence migration and road fixtures remain checkpoint-G work.

**Scope Ledger:**
- Checkpoint A / `R050-001`: remains implemented and validated.
- Checkpoint B / `R050-001` source-model portion: implemented and focused-tested.
- Checkpoints C–G: planned and untouched; no StationAnchor, road commands, segmentation, envelope engine, migration, or road package fixtures were added.
- v0.4.x regression and compatibility suite: preserved and passing.

**Requirements addressed:**
- `ARCH-CORE-002`: RoadSpline is a distinct authoritative road-centerline record.
- `ARCH-CORE-003`: the source model represents the RoadSpline-to-RoadSegment relationship required for full-domain creation; command creation remains checkpoint D.
- `ARCH-CORE-004`: RoadSegment references its parent RoadSpline and does not carry independent centerline geometry.
- `MAP-CORE-001`: road records require one explicit owning Map and reject cross-map references.
- Stable identity and deterministic normalization are implemented without using collection position as identity.

**Files/components changed:**
- Updated `src/atlas/domain/project.hpp` and `src/atlas/domain/project.cpp`.
- Updated `tests/atlas/domain/project_tests.cpp` with five purpose-commented checkpoint-B tests.
- Added `.DESIGN/adr/ADR-0010-road-persistence-record-architecture.md`.
- Updated `.DESIGN/decision-backlog.md`.

**Focused Checks:**
- Domain tests: 19 out of 19 passed.
- Full headless suite: 120 out of 120 passed, no failures or skips.
- Windows/Qt build passed.
- Architecture governance passed.
- `git diff --check` passed.
- Editor diagnostics reported no errors for the changed source, tests, ADR, or backlog.

**Persistence/compatibility impact:**
- No package read/write or migration behavior changed in checkpoint B.
- The implemented domain normalization exposes first-class road arrays for source-model testing, but schema generation remains `1` until checkpoint G implements the accepted generation-1-to-2 migration and package adapter.
- Existing v0.4 packages remain unaffected by the domain containers when no road records are present.

**Issues:**
- Deviations from Phase 1: none; persistence integration was intentionally kept out of checkpoint B as planned.
- Remaining implementation work: checkpoint C requires ADR-0008 before StationAnchor work; checkpoint F requires ADR-0009; checkpoint G requires migration and road fixtures under ADR-0010.
- ADR or design decision needed: none for checkpoint B. ADR-0008 and ADR-0009 remain required before their checkpoints.

**Phase Gate:**
- Ready for validation: yes for checkpoint B.
- Ready to proceed to checkpoint C: no until ADR-0008 is formally accepted.
- Ready for release integration: no for public v0.5.0.

---

## Phase 3 — Validate & Fix (Checkpoint D final)

Result: completed  
Phase: 3 — Validate & Fix  
Version: `v0.5.0-D`

**Validation:**
- Headless configure and build passed in an explicitly initialized Visual Studio x64 developer shell.
- Headless CTest passed: 138 out of 138 tests, with no failures or skips.
- Windows/Qt configure and build passed.
- Architecture governance check passed.
- `git diff --check` passed.
- Editor diagnostics reported no errors for the repaired RoadSpline command, domain, tests, or development log.

**Scope ledger result:**
- Checkpoint D / `R050-003`: validated after the Phase 2 repair cycle.
- Checkpoints A–C: remain validated by prior evidence and the full regression suite.
- Checkpoints E–G: intentionally not applicable to D and remain planned.
- v0.4.x compatibility and regression suite: remains green within the 138-test headless run.

**Conformance:**
- RoadSpline create/edit/extend/shorten/reverse/delete commands use the revision and undo/redo boundary.
- Reverse remaps RoadSpline StationAnchor stations inside the same authoritative transaction and reverse-twice source normalization passes.
- Shortening past an anchor requires explicit Cancel, Move dependents, or Delete dependents resolution.
- Dependent RoadSegments prevent silent RoadSpline deletion.
- Stable RoadSpline identity and expected-revision guards remain enforced.

**Tests/checks executed:**
- Focused application tests: 45 out of 45 passed.
- Full headless suite: 138 out of 138 passed.
- Windows/Qt build: passed.
- Architecture governance: passed.
- `git diff --check`: passed.

**Defects:**
- Defects found: the baseline Phase 3 audit confirmed missing StationAnchor transaction integration and shortening resolution.
- Fixes made: added same-transaction anchor remapping, explicit shortening resolutions, source-length validation, and dependent deletion protection.
- Regression coverage added: reverse anchor remapping and all three shortening resolutions.
- Remaining failures/limitations: segmentation, lineage, envelope generation, persistence migration, and road fixtures remain later checkpoints.
- Stop-ship defects: none for checkpoint D.

**Phase Gate:**
- Ready for release integration: no for public v0.5.0.
- Ready to proceed to checkpoint E: yes.
- Reason: the complete checkpoint-D command contract, StationAnchor transaction behavior, shortening resolutions, full regression suite, supported builds, governance, and diff checks pass.
- Reason: first-class road source records, ownership, identity, deterministic normalization, and parent validation pass; later checkpoints and the public release remain incomplete.

---

## Phase 3 — Validate & Fix (Checkpoint B)

Result: completed  
Phase: 3 — Validate & Fix  
Version: `v0.5.0-B`

**Validation:**
- Headless configure and build passed in one explicitly initialized Visual Studio x64 developer shell.
- Headless CTest passed: 120 out of 120 tests, with no failures or skips.
- Windows/Qt configure and build passed.
- Architecture governance check passed.
- `git diff --check` passed.
- Editor diagnostics reported no errors for the road source model, tests, ADR-0010, backlog, or release log.

**Scope ledger result:**
- Checkpoint B / `R050-001` source-model portion: validated.
- Checkpoint A / `R050-001` curve-kernel portion: remains validated by prior checkpoint evidence and the full regression suite.
- Checkpoints C–G: intentionally not applicable to B and remain planned.
- v0.4.x regression and compatibility suite: remains green in the 120-test headless run.

**Conformance:**
- RoadSpline and RoadSegment are first-class project records rather than generic MapObject payloads.
- RoadSpline and RoadSegment IDs are validated for non-empty uniqueness.
- RoadSpline and RoadSegment Map ownership and RoadSegment parent references are validated.
- RoadSpline segment references and RoadSegment boundary/cross-section stub data normalize deterministically.
- Road arrays sort by stable ID for normalized output independent of insertion order.
- Normalized source round-trip reconstructs road record families without persisting derived station length or lookup data.
- Existing v0.4 source and package behavior remains green with empty road arrays.

**Tests/checks executed:**
- Focused domain tests: 19 out of 19 passed.
- Full headless suite: 120 out of 120 passed.
- Windows/Qt build: passed.
- Architecture governance: passed.
- `git diff --check`: passed.

**Defects:**
- Defects found: none in the final checkpoint-B implementation.
- Fixes made: repaired a malformed parser block found during focused compilation; no architectural behavior changed.
- Regression coverage added: first-class road identity/ownership, segment parent validation, deterministic road normalization, duplicate IDs, invalid ownership, and normalized round-trip tests.
- Remaining failures/limitations: package writer/reader schema generation, generation-1-to-2 migration, road fixtures, and unknown road-field persistence remain checkpoint G work by ADR-0010. StationAnchor and remap semantics remain checkpoint C work.
- Stop-ship defects: none for checkpoint B.

**Phase Gate:**
- Ready for release integration: no for public v0.5.0.
- Ready to proceed to checkpoint C: no until ADR-0008 is formally accepted.
- Reason: Checkpoint B's first-class source model, ownership, stable identity, deterministic normalization, and parent validation are green. The next checkpoint requires the StationAnchor remap-signature and primitive-lineage decision before implementation.

---

## Phase 2 — Implement (Checkpoint C)

Result: completed  
Phase: 2 — Implement  
Version: `v0.5.0`
Checkpoint: `v0.5.0-C` — StationAnchor (`R050-002`)

**Implementation:**
- Accepted ADR-0008 and updated the decision backlog for the StationAnchor remap-signature and primitive-lineage policy.
- Added `StationAnchor` with authoritative `resolvedStation`, five explicit affinity modes, and a remap signature containing normalized station, primitive ID, local parameter, and world position evidence.
- Added transaction-local `PrimitiveRemapEntry` ranges and lineage-first geometry-locked remapping.
- Added deterministic start-locked, end-locked, normalized, geometry-locked, and world-locked remapping behavior.
- Added bounded world-position projection fallback, explicit search-distance handling, ambiguity reporting, and out-of-domain diagnostics.
- Kept shortening commit resolution, command transactions, and persistence outside checkpoint C as planned.

**Scope Ledger:**
- Checkpoints A and B: remain implemented and validated.
- Checkpoint C / `R050-002`: implemented and focused-tested.
- Checkpoints D–G: planned and untouched; no road editing commands, segmentation, envelope engine, migration, or road persistence wiring were added.
- v0.4.x regression and compatibility suite: preserved and passing.

**Requirements addressed:**
- `STAT-CORE-002`: affinity model supports the geometry-locked default required for segment boundaries and attachments.
- `STAT-CORE-003`: unresolved ambiguous remaps return an explicit unresolved result and diagnostic.
- `STAT-CORE-004`: out-of-domain remaps remain unresolved for later Move/Delete dependent/Cancel command resolution.
- `STAT-CORE-005`: remap output is a value result suitable for atomic inclusion in a later command transaction; command integration remains checkpoint D.
- `AC-003` checkpoint-C remap portion: geometry-locked primitive lineage remapping is deterministic and tested.

**Files/components changed:**
- Added `src/atlas/domain/station_anchor.hpp` and `src/atlas/domain/station_anchor.cpp`.
- Added `tests/atlas/domain/station_anchor_tests.cpp` with seven purpose-commented tests.
- Added `.DESIGN/adr/ADR-0008-station-anchor-remap-and-primitive-lineage.md`.
- Updated `CMakeLists.txt`, `.DESIGN/decision-backlog.md`, and this development log.

**Focused Checks:**
- Domain tests: 26 out of 26 passed.
- Full headless suite: 127 out of 127 passed, no failures or skips.
- Windows/Qt build passed.
- Architecture governance passed.
- `git diff --check` passed.
- Editor diagnostics reported no errors for the StationAnchor source, tests, ADR, or build configuration.

**Persistence/compatibility impact:**
- No package writer/reader or schema behavior changed. StationAnchor persistence remains part of the road record migration and fixture work in checkpoint G.
- Existing v0.4 packages and road-source normalized data remain compatible with the current source-only model.

**Issues:**
- Defects found: one initial ambiguity fixture did not actually contain multiple equal projection candidates; it was corrected to use overlapping primitive spans. One target link was added so the domain remapper could use `atlas_geometry`.
- Remaining implementation work: checkpoint D must integrate remapping into commands and implement shortening resolution; checkpoint E must produce structural lineage; checkpoint G must persist anchors and migration data.
- ADR or design decision needed: none for checkpoint C. ADR-0009 remains required before checkpoint F; ADR-0010 governs checkpoint-G persistence.

**Phase Gate:**
- Ready for validation: yes for checkpoint C.
- Ready to proceed to checkpoint D: yes; ADR-0008 is accepted and all checkpoint-C tests pass.
- Ready for release integration: no for public v0.5.0.
- Reason: StationAnchor data, affinity behavior, lineage-first remapping, bounded fallback, ambiguity diagnostics, and determinism are implemented; command integration and later road checkpoints remain incomplete.

---

## Phase 2 — Implement (Checkpoint D)

Result: partial  
Phase: 2 — Implement  
Version: `v0.5.0`
Checkpoint: `v0.5.0-D` — Road Editing Commands (`R050-003`, source-command portion)

**Implementation:**
- Added command-backed RoadSpline creation, full-source editing, endpoint extension, endpoint shortening, reversal, and deletion through the existing `CommandProcessor`.
- Added immutable project replacement/removal boundaries for RoadSpline records and dependent-RoadSegment protection on deletion.
- Added deterministic reversal of line-like primitive source records, control-point endpoint swaps, control-point sequence reversal, segment-ID order reversal, and direction toggling.
- Preserved expected-revision guards, atomic revision snapshots, undo/redo, stale-command rejection, and stable RoadSpline identity.
- Added 7 purpose-commented command tests covering create, edit, extend, shorten, reverse-twice, reverse undo, dependent deletion protection, and stale commands.

**Scope Ledger:**
- Checkpoints A–C: remain implemented and validated.
- Checkpoint D / `R050-003`: implemented for the source-command portion; full StationAnchor-in-transaction remapping and geometric shortening resolution remain partial.
- Checkpoints E–G: planned and untouched; no segmentation, envelope engine, migration, or road persistence wiring was added.
- v0.4.x regression and compatibility suite: preserved and passing.

**Requirements addressed:**
- RoadSpline mutations use the v0.3 command processor and expected-revision boundary.
- Create/edit/extend/shorten/reverse/delete source command paths have focused transaction evidence.
- Reverse-twice source normalization and stable identity pass for the implemented line-like source representation.
- Deletion cannot bypass existing RoadSegment dependents.

**Files/components changed:**
- Updated `src/atlas/domain/project.hpp` and `src/atlas/domain/project.cpp` with immutable RoadSpline replacement/removal helpers.
- Updated `src/atlas/application/command.hpp` and `src/atlas/application/command.cpp` with RoadSpline command classes and source transforms.
- Updated `tests/atlas/application/command_tests.cpp` with checkpoint-D command tests.

**Focused Checks:**
- Application tests: 41 out of 41 passed.
- Full headless suite: 134 out of 134 passed, no failures or skips.
- Windows/Qt configure and build passed.
- Architecture governance passed.
- `git diff --check` passed.

**Persistence/compatibility impact:**
- No package writer/reader or migration behavior changed. Road command source mutations remain in-memory until checkpoint G persistence integration.
- Existing schema generation and v0.4 compatibility behavior are unchanged.

**Issues:**
- Defects found: none after the corrected RoadSpline primitive fixture.
- Remaining implementation work: integrate StationAnchor remap results into the same RoadSpline edit transactions; distinguish valid shortening resolutions (Move/Delete dependent/Cancel); validate geometric endpoint monotonicity against the curve kernel; and complete checkpoint E segmentation.
- ADR or design decision needed: none for the implemented source-command portion. The remaining D work is governed by accepted ADR-0008.

**Phase Gate:**
- Ready for validation: yes for the implemented source-command portion.
- Ready to proceed to checkpoint E: no; full D requires StationAnchor transaction integration and shortening resolution before segmentation.
- Ready for release integration: no for public v0.5.0.
- Reason: command paths and revision behavior are green, but the complete R050-003 contract is not yet implemented.

---

## Phase 2 — Implement (Checkpoint D repair)

Result: completed  
Phase: 2 — Implement  
Version: `v0.5.0`
Checkpoint: `v0.5.0-D` — R050-003 completion repair

**Implementation:**
- Added authoritative `stationAnchors` to RoadSpline source normalization and parsing.
- Integrated StationAnchor remapping into RoadSpline reversal and endpoint-shortening source snapshots.
- Added `ShortenResolution::cancel`, `moveDependents`, and `deleteDependents` with atomic command behavior.
- Shortening now rejects anchor loss unless an explicit resolution is supplied; Move clamps dependent anchors to the new endpoint and Delete removes them.
- Reversal now remaps anchor stations against the source length in the same transaction.
- Added bounded line-like source-length validation for endpoint edits.
- Updated existing shortening coverage to select an explicit resolution under the repaired contract.

**Scope Ledger:**
- Checkpoints A–C remain implemented and validated.
- Checkpoint D / `R050-003`: implemented for RoadSpline command mutation, same-transaction anchor changes, reverse anchor remapping, and shortening resolution.
- Checkpoints E–G remain planned and untouched.

**Tests added/updated:**
- Same-transaction reverse anchor remapping.
- Shortening Cancel resolution.
- Shortening Move dependents resolution.
- Shortening Delete dependents resolution.
- Existing endpoint edit test updated to select Move explicitly.

**Focused Checks:**
- Application tests: 45 out of 45 passed.
- Full headless suite: 138 out of 138 passed, no failures or skips.
- Windows/Qt build passed.
- Architecture governance passed.
- `git diff --check` passed.

**Issues:**
- Defects found and fixed: the initial D implementation did not carry StationAnchors in RoadSpline source or enforce shortening resolution; both gaps are now covered.
- Remaining implementation work: checkpoint E segmentation, checkpoint F envelope behavior, and checkpoint G persistence/migration remain.
- ADR or design decision needed: none for D; ADR-0008 governs the completed anchor behavior.

**Phase Gate:**
- Ready for validation: yes.
- Ready to proceed to checkpoint E: yes after the Phase 3 rerun confirms the repaired full R050-003 scope.
- Ready for release integration: no for public v0.5.0.

---

## Phase 3 — Validate & Fix (Checkpoint C)

Result: completed  
Phase: 3 — Validate & Fix  
Version: `v0.5.0-C`

**Validation:**
- Headless configure and build passed in an explicitly initialized Visual Studio x64 developer shell.
- Headless CTest passed: 127 out of 127 tests, with no failures or skips.
- Windows/Qt build passed on a separate completion check; the chained validation process did not reach its later commands, so the build status was independently confirmed with exit code 0.
- Architecture governance check passed.
- `git diff --check` passed.
- Editor diagnostics reported no errors for the StationAnchor implementation, tests, ADR-0008, build configuration, or release log.

**Scope ledger result:**
- Checkpoint C / `R050-002`: validated.
- Checkpoints A and B: remain validated by prior checkpoint evidence and the full regression suite.
- Checkpoints D–G: intentionally not applicable to C and remain planned.
- v0.4.x compatibility and regression suite: remains green within the 127-test headless run.

**Conformance:**
- All five affinity modes are exercised: start-locked, end-locked, normalized, geometry-locked, and world-locked.
- Geometry-locked remapping consults primitive lineage ranges before fallback projection.
- World-locked fallback is bounded by search distance and reports unresolved results for missing candidates.
- Equal projection candidates are reported as ambiguous rather than silently tie-broken.
- Out-of-domain remaps remain unresolved with diagnostics for later command-level Move/Delete dependent/Cancel resolution.
- Identical remap inputs produce identical resolved anchor state.
- `AC-003` checkpoint-C evidence passes for deterministic geometry-locked remapping and ambiguity preview.

**Tests/checks executed:**
- Focused domain suite: 26 out of 26 passed.
- Full headless suite: 127 out of 127 passed.
- Windows/Qt build: passed.
- Architecture governance: passed.
- `git diff --check`: passed.

**Defects:**
- Defects found during validation: none in the final implementation. The earlier ambiguity-fixture defect was corrected during Phase 2 and is covered by the final passing suite.
- Fixes made: none during Phase 3.
- Regression coverage added: no additional coverage required after the corrected equal-candidate ambiguity test.
- Remaining limitations: command transaction integration, shortening resolution, and persisted StationAnchor records remain checkpoint D/G work.
- Stop-ship defects: none for checkpoint C.

**Phase Gate:**
- Ready for release integration: no for public v0.5.0.
- Ready to proceed to checkpoint D: yes.
- Reason: ADR-0008 is accepted and the StationAnchor data model, lineage-first remapping, all affinities, bounded fallback, ambiguity handling, determinism, full regression suite, and supported builds are green.

---

## Phase 2 - Implement (Checkpoint E)

Result: partial
Phase: 2 - Implement
Version: `v0.5.0`
Checkpoint: `v0.5.0-E` - Segmentation (`R050-004`)

**Implementation:**
- Added contiguous, ordered RoadSegment coverage validation and automatic full-domain segment creation for new RoadSplines.
- Added atomic split, internal boundary-move, and adjacent-merge commands through the existing revision/undo boundary.
- Split creates a StationAnchor, stable revision-derived child IDs, parent/predecessor/successor lineage, copies all currently represented segment property fields, and preserves exact normalized source on undo.
- Merge retains the upstream segment ID, archives complete input records in lineage, and rejects differences in cross-section state, style overrides, schema properties, spatial references, boundary attachments, or metadata.
- Added deterministic repeated-split coverage checks and exact merge undo/redo checks.

**Scope ledger status:**
- Checkpoints A-D remain implemented as recorded in prior entries.
- Checkpoint E / `R050-004` is partially implemented. Coverage, split, boundary move, merge rejection/acceptance, lineage, and transaction behavior are present.
- Checkpoints F-G remain planned. Public v0.5.0 remains incomplete and has not been promoted.

**Roadmap packets addressed:**
- `R050-004` segmentation, in part.

**CORE requirements addressed:**
- `SEGM-CORE-001` and `SEGM-CORE-002`: ordered complete interval coverage and initial full-domain segment creation.
- `SEGM-CORE-004`: upstream identity retention and archival of both merge inputs; downstream reference remapping remains incomplete.
- `SEGM-CORE-005`: boundary moves update the shared StationAnchor while preserving interval coverage.
- Split lineage and transaction/undo behavior are covered; full split requirements listed below remain partial.

**Source files/components changed:**
- `src/atlas/domain/project.hpp` and `src/atlas/domain/project.cpp`: RoadSegment source fields and atomic topology replacement.
- `src/atlas/application/command.hpp` and `src/atlas/application/command.cpp`: coverage validation and split, boundary-move, and merge commands; split preserves currently represented property fields.
- `tests/atlas/application/command_tests.cpp`: segmentation, lineage, property-copy, coverage, merge, and undo/redo tests.
- `tests/atlas/domain/project_tests.cpp`: RoadSegment property-field normalized round-trip test.

**Tests added/updated:**
- Automatic full-domain segment creation; split coverage/lineage/property copying and exact undo; endpoint-tolerance rejection; valid and zero-length boundary moves; equivalent merge identity and archived lineage; exact merge undo/redo; non-equivalent merge rejection for each represented equivalence field; repeated split coverage; source-field round-trip.
- Added tests include concise purpose comments.

**Documentation/comments added/updated:**
- Appended this checkpoint report. No public release/version documentation was promoted.

**Persistence/compatibility impact:**
- No package reader/writer or schema-generation migration was changed; persistence integration remains checkpoint G.
- RoadSegment fields participate in normalized project source. Application package persistence and migration compatibility are not yet verified.

**Focused Checks:**
- Domain tests: 27/27 passed.
- Application command tests: 55/55 passed.
- Full headless CTest: 149/149 passed.
- Windows/Qt build and UI tests: passed; UI tests 4/4.
- Architecture governance and `git diff --check`: passed. The diff check emitted only a line-ending warning for the unrelated existing `.DESIGN/agent-workflow/PHASE_4_RELEASE_INTEGRATION.md` file.
- Editor diagnostics for changed implementation and test files: no errors.

**Issues:**
- Deviations from Phase 1: split copies all currently represented fields, but source uses opaque JSON for cross-section state and attachment/reference collections; required schema-specific split rules cannot be applied safely without a defined element/reference shape.
- Unimplemented or partial ledger items: stable downstream CrossSectionElement IDs with `continuesFrom`; station-scoped attachment/reference movement without changing object IDs; schema-defined property copy behavior; affected-range derived invalidation; deterministic downstream-reference remapping on merge; field-wise conflict resolution beyond safe rejection. These leave `SEGM-CORE-003` and portions of `SEGM-CORE-004` and the split acceptance criteria incomplete.
- Remaining implementation work: define/use typed or schema-backed cross-section elements and attachment/reference ownership, then implement split/merge remapping and focused tests before checkpoint E can pass its mini-gate. Do not proceed to checkpoint F until this is resolved.
- ADR or design decision needed: no new ADR was specified for E, but the missing cross-section and attachment/reference data contract must be established from the canonical schema/design before implementation; no semantics were invented here.

**Phase Gate:**
- Ready for validation: no.
- Reason: current coverage, commands, represented-field copying, lineage, and transaction tests pass, but mandatory cross-section identity and station-scoped reference behavior are not representable in the current source model. Checkpoint E is not complete and public v0.5.0 remains partial.

---

## Phase 3 - Validate & Fix (Checkpoint E, initial gate)

Result: partial
Phase: 3 - Validate & Fix
Version: `v0.5.0`
Checkpoint: `v0.5.0-E` - Segmentation (`R050-004`)

**Validation:**
- Headless configure/build passed in the initialized Visual Studio x64 environment.
- Full headless CTest passed: 149/149.
- Windows/Qt configure/build passed; UI tests passed: 4/4.
- Architecture governance and `git diff --check` passed. Diff check reported only the existing unrelated Phase 4 document line-ending warning.
- Scope ledger result: checkpoint E is not validated. Checkpoints F-G and public release integration remain not applicable to this checkpoint and unvalidated for v0.5.0.
- Passed: existing interval coverage, split/merge transaction, represented-field copy, lineage, undo/redo, and merge-rejection tests; full prior regression suite.
- Failed: no executable test failed. The checkpoint gate fails on missing implementation and required evidence listed below.
- Not run/not present: fuzzed edit-sequence coverage property test; CrossSectionElement ID/`continuesFrom` split checks; station-scoped attachment ownership/remapping checks; range-scoped split/merge invalidation checks; explicit field-level merge-resolution checks; road persistence/migration and later checkpoint F tests are outside E.
- Determinism/compatibility: deterministic repeated split and exact normalized undo/redo checks pass; the required fuzzed coverage campaign is absent. Existing headless/Qt regression suites pass; no E-specific package migration is applicable yet.

**Conformance:**
- CORE requirements verified in part: `SEGM-CORE-001`, `SEGM-CORE-002`, and `SEGM-CORE-005` have direct implementation/test evidence. `SEGM-CORE-003` stable cross-section state is not validated against element identity semantics. `SEGM-CORE-004` upstream identity/archive behavior passes, but compatible downstream reference remapping remains absent.
- Roadmap packet `R050-004` and ship items: partially verified; split, boundary move, merge, coverage, and basic lineage are present. Element lineage, station-scoped attachment/reference remapping, and affected-range invalidation are missing.
- Acceptance criteria: split-with-undo and safe rejection of non-equivalent merges pass; fuzzed coverage, schema-defined element copy/identity, station attachment movement, and an explicit field-by-field resolution path are not evidenced.
- Release gates: checkpoint-E mini-gate `SEGM-CORE-001`-`005` is not met. Public v0.5.0 release gates remain incomplete by design.

**Defects:**
- Defects found: no runtime regression in executed suites. Validation identified incomplete checkpoint requirements: typed/schema-backed cross-section split identity, station-scoped attachment ownership, targeted derived invalidation, fuzzed coverage evidence, and field-level merge resolution.
- Fixes made: none during this initial Phase 3 pass; repair is recorded in the following Phase 2 checkpoint-E entry.
- Regression coverage added: none during this initial pass.
- Remaining failures/limitations: all checkpoint-E gaps listed above; the current merge command only rejects conflicts and provides no explicit resolution path.
- Stop-ship defects: checkpoint-scoped stop condition is active because split/merge cannot yet demonstrate atomic preservation/remapping of stable element IDs and references.

**Phase Gate:**
- Ready for release integration: no.
- Ready to proceed to checkpoint F: no.
- Reason: builds and existing tests pass, but mandatory checkpoint-E source semantics and validation evidence are incomplete. Phase 2 repair is required before the checkpoint can pass.

---

## Phase 2 - Implement (Checkpoint E repair)

Result: completed
Phase: 2 - Implement
Version: `v0.5.0`
Checkpoint: `v0.5.0-E` - Segmentation (`R050-004`)

**Implementation:**
- Defined station attachments as segment-owned records with stable `id` and `stationAnchorId`; split and boundary move reassign them using half-open interval ownership while preserving attachment IDs.
- Split preserves each CrossSectionElement ID upstream, creates a deterministic downstream ID linked by `continuesFrom`, and remaps station-attachment element references atomically.
- Merge recognizes compatible split lineage, keeps upstream element identity, remaps downstream attachment references, combines attachment records deterministically, archives both full source records, and exposes conflicting fields for independent upstream/downstream choices.
- Split, boundary move, and merge previews emit RoadSpline station-range invalidations. Command history carries those invalidations through commit, undo, and redo with the restored revision.
- Replaced split-only coverage repetition with additional seeded mixed split/move/merge sequences and retained the focused split coverage/undo tests.

**Scope ledger status:**
- Checkpoints A-D remain implemented as previously recorded.
- Checkpoint E / `R050-004`: implementation and focused tests now cover the planned segmentation behaviors and mini-gate requirements. Phase 3 remains required to validate the checkpoint.
- Checkpoints F-G and public v0.5.0 release completion remain not implemented.

**Roadmap packets addressed:**
- `R050-004` segmentation.

**CORE requirements addressed:**
- `SEGM-CORE-001` and `SEGM-CORE-002`: complete ordered coverage and initial full-domain segment creation.
- `SEGM-CORE-003`: stable per-segment cross-section state and deterministic split continuation identity.
- `SEGM-CORE-004`: upstream segment identity, archived inputs, explicit conflict choices, and compatible element-reference remapping.
- `SEGM-CORE-005`: boundary ownership changes without moving attachment stations; attachments follow their StationAnchor owner interval.

**Source files/components changed:**
- `src/atlas/domain/project.hpp` and `src/atlas/domain/project.cpp`: station attachment source field and normalized round-trip.
- `src/atlas/application/command.hpp` and `src/atlas/application/command.cpp`: split/move/merge property, identity, attachment, conflict, invalidation, and history behavior.
- `tests/atlas/application/command_tests.cpp`: split/merge identity and reference behavior, conflict choices, invalidation history, atomic rejection, and seeded coverage.
- `tests/atlas/domain/project_tests.cpp`: station attachment normalized-source round trip.

**Tests added/updated:**
- Split preserves upstream CrossSectionElement IDs and creates downstream `continuesFrom` IDs; attachment partitioning before/at/after a split and element reference remapping; merge continuity and exact undo; boundary movement attachment reassignment; missing-anchor atomic rejection; explicit field-level merge resolution; preview and committed range invalidations; seeded mixed split/move/merge coverage sequences across five seeds (200 operations total).
- Updated domain round-trip coverage for station attachments. Each added or modified test has a purpose comment.

**Documentation/comments added/updated:**
- Documented the segment-owned StationAnchor attachment record contract next to the source field. Appended this repair report; no public release/version documentation was promoted.

**Persistence/compatibility impact:**
- Normalized project source now includes `stationAttachments`; application package reader/writer and schema-generation migration remain unchanged and are deferred to checkpoint G.
- No application version or persistence schema generation was promoted.

**Focused Checks:**
- Domain tests: 27/27 passed.
- Application command tests: 59/59 passed.
- Editor diagnostics for changed source and test files: no errors.
- `git diff --check` passed with only the existing unrelated Phase 4 document line-ending warning.

**Issues:**
- Deviations from Phase 1: none identified in the implemented checkpoint-E requirements.
- Unimplemented or partial ledger items: none within checkpoint E. Checkpoints F-G remain planned and public release gates are not claimed.
- Remaining implementation work: run Phase 3 validation for checkpoint E. Road envelope generation and persistence/migration remain later checkpoint work.
- ADR or design decision needed: none; attachment and split/merge behavior follows the accepted specification and ADR-0010 source-record architecture.

**Phase Gate:**
- Ready for validation: yes.
- Reason: checkpoint-E split, boundary move, merge, lineage, ownership, reference remapping, scoped invalidation, seeded coverage, and transaction evidence are implemented with focused tests. No unresolved checkpoint-E design ambiguity remains.

---

## Phase 3 - Validate & Fix (Checkpoint E, final gate)

Result: completed
Phase: 3 - Validate & Fix
Version: `v0.5.0`
Checkpoint: `v0.5.0-E` - Segmentation (`R050-004`)

**Validation:**
- Build result: headless and official Qt presets configured and built in initialized Visual Studio x64 shells; Qt deployment completed.
- Scope ledger result: checkpoint E / `R050-004` validated. Checkpoints A-D remain covered by their prior phase records and the full regression run. Checkpoints F-G and the public v0.5.0 release ledger remain incomplete and are not promoted by this result.
- Test/check suites executed: full headless CTest, Qt UI tests, architecture governance, editor diagnostics, and `git diff --check`.
- Passed: headless CTest 153/153; Qt UI tests 4/4; architecture governance; changed-file diagnostics with no errors; diff check.
- Failed: none.
- Not run: checkpoint-F envelope fixtures/failure campaigns; checkpoint-G package persistence, generation migration, migration failure/backup tests, and full public-release integration gates. These are later checkpoints, not E acceptance criteria. CMake Tools had no active target/test discovery and the repository tasks were hidden from the task runner; the documented initialized-shell commands were used instead.
- Determinism/compatibility result: seeded mixed split/move/merge coverage passed across five seeds and 200 operations; split undo and merge undo/redo restore normalized source; the complete existing headless regression and Qt UI suites pass. Persistence migration is deferred to G.

**Conformance:**
- CORE requirements verified: `SEGM-CORE-001` complete gap-free ordered coverage; `SEGM-CORE-002` automatic full-domain segment; `SEGM-CORE-003` stable cross-section state and split element identity lineage; `SEGM-CORE-004` upstream identity, archived inputs, field conflict choices, and compatible attachment element-reference remapping; `SEGM-CORE-005` interval-only boundary movement with StationAnchor attachment reassignment.
- Roadmap packets and ship items verified: checkpoint E / `R050-004` split, boundary move, equivalence-checked merge, lineage, station-scoped attachment/reference remapping, half-open coverage, and affected-range invalidation.
- Acceptance criteria verified: purpose-commented split/merge/lineage and conflict-resolution tests; exact normalized split undo; deterministic seeded mixed-operation coverage property; atomic rejection for endpoint and missing attachment references; committed range invalidations retained through undo/redo.
- Release gates verified: checkpoint-E mini-gate `SEGM-CORE-001`-`005` passes. Public v0.5.0 release gates remain incomplete pending F-G.

**Defects:**
- Defects found: the initial Phase 3 run identified missing CrossSectionElement continuation, station attachment ownership/remapping, affected-range invalidation, mixed-operation coverage evidence, and explicit field-level merge resolution.
- Fixes made: completed in the intervening Phase 2 repair entry; the final Phase 3 rerun found no remaining checkpoint-E defect.
- Regression coverage added: downstream `continuesFrom` IDs; attachment split/move/merge ownership and reference remaps; field-by-field conflict selection; preview/commit/undo/redo invalidation ranges; atomic invalid-reference rejection; seeded mixed split/move/merge coverage.
- Remaining failures/limitations: no checkpoint-E failures. Envelope generation and persistence/migration remain checkpoint F/G work.
- Stop-ship defects: none for checkpoint E. Public v0.5.0 is not release-ready.

**Phase Gate:**
- Ready for release integration: no.
- Ready to proceed to checkpoint F: yes.
- Reason: checkpoint E now satisfies its coverage, identity, ownership, transaction, reference-remap, invalidation, conflict-resolution, and property-test gates. Public v0.5.0 still requires checkpoints F-G and their release evidence.

---

## Phase 2 - Implement (Checkpoint F)

Result: completed
Phase: 2 - Implement
Version: `v0.5.0`
Checkpoint: `v0.5.0-F` - Envelope Geometry (`R050-005`)

**Implementation:**
- Added signed curvature and primitive-parameter frame evaluation to the analytic CurveKernel for polyline, cubic Bezier, and fixed-radius arc primitives.
- Added a deterministic road-envelope geometry module with stable left/right offsets, round/bevel/miter joins, project miter-limit handling, bounded sampling and previews, offset cusp/tight-radius/orientation/self-intersection diagnostics, and algorithm/tolerance versioned output hashes.
- Added the persisted 8.0 m centered uniform-placeholder width as a configurable road-creation preset; every created RoadSegment stores its selected total width and round join style in authoritative source.
- Added a namespaced project geometry policy storing the miter-limit ratio (default 4.0); legacy source without the policy receives that default, and existing projects retain their normalized policy independently of future application defaults.
- Wired changed-road derivation into command previews. Model B reports envelope failures as nonblocking Error diagnostics with bounded invalid previews; structurally invalid source, width, and join inputs remain commit-blocking.
- Added full-domain or affected-segment station-range envelope invalidations. Project policy changes rebuild affected road envelopes; commit, undo, and redo retain invalidation history.
- Different valid segment widths or join styles that the single uniform envelope result cannot represent produce a nonblocking invalid derived preview rather than rejecting source.

**Scope ledger status:**
- Checkpoints A-E remain recorded in prior entries.
- Checkpoint F / `R050-005`: implementation and focused evidence added for the analytic offset envelope, failure diagnostics, Model B commit behavior, joins, deterministic hashes, and range invalidation. Phase 3 remains required to validate the checkpoint.
- Checkpoint G and public v0.5.0 release completion remain not implemented.

**Roadmap packets addressed:**
- `R050-005` envelope geometry.

**CORE requirements addressed:**
- `GEOM-CORE-001`: centerline frames and offsets derive from analytic/tolerance-bounded CurveKernel evaluation, not render tessellation.
- `GEOM-CORE-002`: cusp, tight-radius, self-intersection, and orientation-inversion diagnostics; miter overflow is separately diagnosed under `GEOM-CORE-004`.
- `GEOM-CORE-003`: failed derived offsets return Error diagnostics and bounded invalid previews; structurally valid source commits under ADR-0009 Model B.
- `GEOM-CORE-004`: round default and explicit round/bevel/miter joins; miter joins use the persisted project ratio.
- `GEOM-CORE-005`: output hash includes curve/envelope/tolerance versions, resolved options, work bounds, and bounded result coordinates.
- `AC-001` envelope half: new RoadSpline creation produces a default-width envelope preview; failure does not silently mark derived geometry valid.

**Source files/components changed:**
- `src/atlas/geometry/curve.hpp` and `src/atlas/geometry/curve.cpp`: primitive-parameter frames and signed curvature.
- `src/atlas/geometry/envelope.hpp` and `src/atlas/geometry/envelope.cpp`: envelope options/results, offset/join generation, diagnostics, bounds, and deterministic versioned hashing.
- `src/atlas/application/command.hpp` and `src/atlas/application/command.cpp`: preview envelopes, blocking/nonblocking diagnostic distinction, Model B commit behavior, persisted-width reads, and range invalidations.
- `src/atlas/domain/project.hpp` and `src/atlas/domain/project.cpp`: recognized normalized project geometry policy.
- `CMakeLists.txt`: envelope source in `atlas_geometry`.
- `tests/atlas/geometry/curve_tests.cpp`, `tests/atlas/application/command_tests.cpp`, and `tests/atlas/domain/project_tests.cpp`: geometry, transaction, policy, and source validation coverage.
- `tests/fixtures/geometry/road-envelope-v1.json`: version-tagged basic envelope golden.
- `.DESIGN/adr/ADR-0009-road-source-validity-vs-derived-envelope-validity.md`, `.DESIGN/adr/ADR-0011-road-uniform-placeholder-width.md`, `.DESIGN/adr/ADR-0012-project-miter-limit-policy.md`, and `.DESIGN/decision-backlog.md`: accepted checkpoint-F policy records.

**Tests added/updated:**
- Analytic signed curvature and parameter-frame tests across supported primitives.
- Envelope golden and determinism tests; round/bevel/miter join tests; cusp, tight-radius, self-intersection, orientation-inversion, miter-limit, and resource-bound failure cases.
- Command tests for persisted default/custom width, valid default envelope, Model B commit with bounded invalid preview, blocking invalid source width, nonblocking segment-width discontinuity, project miter-policy rebuild, and ranged invalidation.
- Domain tests for project geometry-policy defaults, validation, legacy defaulting, and normalized round trips.
- Every added or modified test includes a concise purpose comment.

**Documentation/comments added/updated:**
- Formalized accepted Model B in ADR-0009; recorded user-selected width in ADR-0011 and persisted miter policy in ADR-0012; updated the decision backlog.
- Added focused source comments for geometry-policy and station-attachment ownership; no public release/version status was promoted.

**Persistence/compatibility impact:**
- Project normalized source includes the namespaced `atlas.geometryPolicy` extension and existing RoadSegment `totalWidthMeters`/`joinStyle` values. Older normalized project source defaults the missing policy to ratio 4.0 when loaded.
- No package reader/writer, package schema generation, migration, or application version was changed; transactional persistence migration and canonical road package fixtures remain checkpoint G.
- Derived envelopes are not authoritative persisted source. Results carry algorithm/tolerance versions and hashes.

**Focused Checks:**
- Geometry suite: 32/32 passed.
- Domain suite: 28/28 passed.
- Application suite: 64/64 passed.
- Full headless CTest: 165/165 passed.
- Supported official Qt configure/build/deployment passed; UI tests: 4/4 passed.
- Architecture governance passed; changed-file diagnostics reported no errors; `git diff --check` passed with only the unrelated existing Phase 4 document line-ending warning.

**Issues:**
- Deviations from Phase 1: none identified in the implemented envelope/failure-policy scope.
- Unimplemented or partial ledger items: no envelope-dependent export or downstream operation exists in the current application to exercise the “blocked until valid” consumer behavior. Package persistence/migration, retained `minimal-road`/`split-road` package fixtures, and later public-release evidence remain checkpoint G.
- Remaining implementation work: run Phase 3 validation for checkpoint F. Wire valid-envelope preconditions into any future export or other envelope-dependent command when such a consumer is introduced.
- ADR or design decision needed: none. ADR-0009 through ADR-0012 are accepted; the final geometry-library selection remains deferred as previously recorded and was not changed by this checkpoint.

**Phase Gate:**
- Ready for validation: yes.
- Reason: checkpoint-F source/derived validity, bounded offset generation, joins, deterministic output, project policy, failure diagnostics, and range invalidation have focused evidence; supported builds and full current regressions pass. No Phase 3 validation or public release promotion is claimed.

---

## Phase 3 - Validate & Fix (Checkpoint F, initial gate)

Result: partial
Phase: 3 - Validate & Fix
Version: `v0.5.0`
Checkpoint: `v0.5.0-F` - Envelope Geometry (`R050-005`)

**Validation:**
- Build result: headless and supported official-Qt presets configured and built in initialized Visual Studio x64 shells; Qt deployment completed.
- Scope ledger result: checkpoint F is not fully validated. The current envelope derives points from analytic curve evaluations but has no explicit spatial deviation tolerance for its sampled polygon.
- Test/check suites executed: full headless CTest, Qt UI tests, architecture governance, editor diagnostics, and `git diff --check`.
- Passed: headless CTest 165/165; Qt UI tests 4/4; architecture governance; changed-file diagnostics with no errors; diff check.
- Failed: no executable test failed. The checkpoint gate fails for missing bounded-approximation evidence and curved-envelope golden coverage.
- Not run: envelope-dependent export/consumer blocking because no such consumer exists in the current application; checkpoint-G persistence/migration, retained package fixtures, and migration failure campaigns are outside F.
- Determinism/compatibility result: repeated straight-envelope outputs hash identically, versioned straight golden and diagnostic failure cases pass, and prior headless/Qt regressions pass. No curved-envelope positional-error bound is currently specified or verified.

**Conformance:**
- CORE requirements verified in part: `GEOM-CORE-002` diagnostic categories, `GEOM-CORE-003` bounded invalid preview and Model B source commit, `GEOM-CORE-004` joins/miter policy, and `GEOM-CORE-005` versioned deterministic hashes have direct evidence. `GEOM-CORE-001` remains partial because output sampling has no positional deviation tolerance. Curved envelope geometry is not covered by a golden fixture.
- Roadmap packets and ship items: checkpoint F / `R050-005` is partially verified. Envelope generation, joins, failure diagnostics, bounded output, Model B command behavior, and range invalidation are implemented; tolerance-bounded curved approximation lacks an executable contract.
- Acceptance criteria verified: source/derived failure policy, each current diagnostic category, deterministic straight golden, and range-scoped invalidation pass. Curved golden/error-bound acceptance remains unverified.
- Release gates verified: checkpoint-F mini-gate is not met until curved output has an explicit bounded-deviation contract and golden evidence. Public v0.5.0 release gates remain incomplete pending G.

**Defects:**
- Defects found: no runtime regression in executed suites. Validation found the envelope polygon uses fixed maximum station spacing without a declared or measured positional deviation bound for curved geometry; the only golden envelope case is straight.
- Fixes made: none during this initial Phase 3 pass; repair is recorded in the following Phase 2 checkpoint-F entry.
- Regression coverage added: none during this initial pass.
- Remaining failures/limitations: curved-envelope approximation is not demonstrably tolerance-bounded, and no curved version-tagged golden fixture exists.
- Stop-ship defects: checkpoint-F stop condition is active for `GEOM-CORE-001` until curved output error is bounded and covered.

**Phase Gate:**
- Ready for release integration: no.
- Ready to proceed to checkpoint G: no.
- Reason: all current builds and tests pass, but curved-envelope positional accuracy and golden evidence required by checkpoint F are incomplete. Phase 2 repair is required before the checkpoint can pass.

---

## Phase 2 - Implement (Checkpoint F repair)

Result: completed
Phase: 2 - Implement
Version: `v0.5.0`
Checkpoint: `v0.5.0-F` - Envelope Geometry (`R050-005`)

**Implementation:**
- Replaced fixed station-step-only envelope sampling with deterministic adaptive subdivision using both a maximum station span and `maximumChordErrorMeters` for offset-side chord deviation.
- Adaptive checks probe quarter, midpoint, and three-quarter positions on both offset sides; curvature failure diagnostics inspect those probes as well as emitted samples.
- Added subdivision-depth, evaluation-count, sample-count, and preview-vertex bounds. When the chord tolerance cannot be met within those bounds, the result is invalid with a resource diagnostic and bounded endpoint preview.
- Round joins choose tessellation from the same chord-error tolerance, and the result records maximum measured curve/join chord deviation.
- Bumped envelope algorithm and tolerance-policy identifiers to `atlas-road-envelope-2` and `atlas-road-envelope-tolerance-2`; deterministic hashes include the chord tolerance and updated version dimensions.
- Added curved circular-arc golden coverage and a cubic-Bezier deviation regression.

**Scope ledger status:**
- Checkpoints A-E remain recorded in prior entries.
- Checkpoint F / `R050-005`: the Phase 3 bounded-approximation finding is implemented and focused-tested. The corrected scope is ready for a new Phase 3 run.
- Checkpoint G and public v0.5.0 release completion remain not implemented.

**Roadmap packets addressed:**
- `R050-005` envelope geometry, including the missing curved-output tolerance contract.

**CORE requirements addressed:**
- `GEOM-CORE-001`: curved offset output is adaptively subdivided to a declared positional chord-error tolerance from analytic CurveKernel evaluations.
- `GEOM-CORE-002`-`004`: existing diagnostic and join behavior is retained; round-join tessellation also respects the error tolerance.
- `GEOM-CORE-005`: algorithm/tolerance versions and the new chord tolerance participate in deterministic results.

**Source files/components changed:**
- `src/atlas/geometry/envelope.hpp` and `src/atlas/geometry/envelope.cpp`: chord-error option/result, adaptive subdivision, probe diagnostics, bounded work, and tolerance-controlled round joins.
- `tests/atlas/geometry/curve_tests.cpp`: curved arc golden and cubic deviation/bounds regression; updated algorithm/tolerance assertions.
- `tests/fixtures/geometry/road-envelope-v1.json`: versioned curved arc expected hash, polygon vertex count, and 5 mm tolerance case.

**Tests added/updated:**
- Version-tagged arc golden pins the adaptive polygon vertex count and deterministic hash while requiring measured deviation at or below 5 mm.
- Cubic-Bezier envelope test requires measured deviation at or below 1 mm, bounded output, and repeated hash equality.
- Round-join deviation assertion and invalid chord-tolerance option test.
- Every added or modified test has a concise purpose comment.

**Documentation/comments added/updated:**
- This repair entry documents the positional tolerance contract and algorithm/tolerance version bump. No public release/version status was promoted.

**Persistence/compatibility impact:**
- No authoritative source or package schema changed in this repair. The chord tolerance is a derived-generation option included in output hashes; derived envelope output remains unpersisted source.
- Curved golden fixture records the curve-kernel, envelope-algorithm, tolerance-policy, and schema-generation versions.

**Focused Checks:**
- Geometry suite: 33/33 passed.
- Domain suite: 28/28 passed.
- Application suite: 64/64 passed.
- `git diff --check` remains required in the final Phase 3 rerun.

**Issues:**
- Deviations from Phase 1: none; this repair supplies the missing explicit positional error contract.
- Unimplemented or partial ledger items: no current export or other envelope-dependent operation exists to exercise downstream blocking until the derived envelope is valid. Package persistence/migration, retained package fixtures, and public release evidence remain checkpoint G.
- Remaining implementation work: rerun Phase 3, including full headless and Qt regressions, architecture governance, diagnostics, and diff checks. Future envelope-dependent consumers must enforce valid-envelope status.
- ADR or design decision needed: none; adaptive chord tolerance is a derived-output accuracy policy and does not alter authoritative meaning or the accepted Model B policy.

**Phase Gate:**
- Ready for validation: yes.
- Reason: the initial Phase 3 finding is addressed with bounded adaptive sampling, curved golden coverage, and a cubic tolerance test; focused suites pass. Full Phase 3 validation remains pending.

---

## Phase 2 - Implement (Checkpoint G)

Result: partial
Phase: 2 - Implement
Version: `v0.5.0`
Checkpoint: `v0.5.0-G` - Conformance and Compatibility (`R050-006`)

**Implementation:**
- Promoted package persistence to schema generation 2 while keeping schema 1 readable and ordinary saves schema-preserving. Schema-1 serialization rejects road-bearing projects instead of dropping authoritative records.
- Added transactional schema 1-to-2 migration through the existing staging, validation, checkpoint, backup, rename, and rollback path. Migration returns machine-readable changes/errors; the CLI prints the report. Dry-run validates the source and leaves it unchanged.
- Added manifest/project generation agreement checks. Schema-2 package data includes top-level RoadSpline and RoadSegment arrays; project and road-record unknown fields survive normalization and package migration.
- Added schema-2 `minimal-road` and `split-road` source fixtures and package save/load tests. Added seeded road package round trips across 24 generated source variations.
- Added an end-to-end integration test for RoadSpline creation, geometry edit with ADR-0008 StationAnchor remapping, segment split/merge, schema-2 save/reopen, and reverse-twice normalized-source equality. RoadSpline edit now remaps retained anchors on surviving primitives and blocks ambiguous/unresolved remaps.
- Added a retained named-host performance baseline for curve construction, station queries, and first/repeated envelope generation.

**Scope ledger status:**
- Checkpoints A-F remain recorded in prior entries.
- Checkpoint G / `R050-006`: persistence architecture, schema 1-to-2 migration, backups/reports/rollback, road fixtures, unknown-data preservation, and integration coverage are implemented and tested.
- G's cold/warm geometry-cache performance evidence remains partial because no production envelope cache exists; the baseline records cold first invocation and repeated process-local timings instead. No dedicated fuzz harness was added; deterministic seeded road round trips and existing malformed-input/resource/security tests provide bounded coverage, not a full fuzz campaign.
- Public v0.5.0 release completion and version promotion remain not performed.

**Roadmap packets addressed:**
- `R050-006` conformance and compatibility, in part.

**CORE requirements addressed:**
- `SAVE-CORE-005`: schema 1-to-2 migration uses one staged package replacement, retains a recoverable generation-1 checkpoint, returns a report, and restores the previous readable package after injected failure.
- `ARCH-CORE-002`, `ARCH-CORE-003`, and `ARCH-CORE-004`: first-class RoadSpline/RoadSegment source persists with identity and ownership intact through schema-2 package round trips.
- Project/road unknown-field preservation: unknown top-level project and road record fields survive normalization and package serialization.
- Station semantic persistence: package fixtures store StationAnchor IDs/stations and remap signatures; RoadSegments retain anchor references, not collection indices as identity.

**Source files/components changed:**
- `src/atlas/persistence/package.hpp` and `src/atlas/persistence/package.cpp`: schema-aware saves/loads, schema matching, explicit migration, report, and rollback/checkpoint behavior.
- `src/atlas/domain/project.hpp` and `src/atlas/domain/project.cpp`: schema-generation serialization and unknown RoadSpline/RoadSegment field preservation.
- `src/atlas/application/command.hpp` and `src/atlas/application/command.cpp`: edit-time StationAnchor remapping and fail-closed diagnostics for unresolved anchors.
- `tests/atlas/persistence/package_tests.cpp`: migration, fixture round-trip, unknown-data, manifest, rollback, and seeded property tests.
- `tests/atlas/domain/project_tests.cpp`: schema generation and unknown road-field coverage.
- `tests/atlas/integration/workflow_tests.cpp`: full road persistence/edit workflow.
- `tests/atlas/geometry/performance_tests.cpp` and `tests/fixtures/performance/road-envelope-v2-baseline.json`: named workload measurements.
- `tests/fixtures/compatibility/minimal-road.json` and `tests/fixtures/compatibility/split-road.json`: retained canonical road source fixtures.
- `README.build.md`: migration CLI example now targets current schema generation 2.

**Tests added/updated:**
- Schema 1 ordinary-save preservation; schema 1-to-2 migration dry-run/success/checkpoint; induced post-backup failure restoration; malformed-source migration report; project/manifest generation mismatch; schema-1 road-array rejection; schema-2 road fixture round trips; 24-seed package normalization round trips; unknown road fields; full create/edit/remap/split/merge/save/reopen/reverse-twice integration.
- Performance baseline workload: AMD Ryzen 7 7800X3D 8-Core Processor, Windows, MSVC 14.51.36231 x64; 100 m cubic Bezier, 8 m width, 0.5 m station span, 0.01 m chord tolerance. Recorded: curve construction 0.126 ms; cold envelope 40.8714 ms; 500 station queries median/p95 5.166/6.0053 ms; repeated envelope median/p95 41.057/42.2716 ms. The benchmark records that no geometry cache exists.
- Every added or modified test includes a concise purpose comment.

**Documentation/comments added/updated:**
- Updated the CLI migration example and retained the schema/migration decisions in ADR-0010 and the release log. No application version, roadmap status, or public release status was promoted.

**Persistence/compatibility impact:**
- New packages use manifest/project schema generation 2 and include first-class road arrays. Existing generation-1 packages load with empty road arrays and remain generation 1 on ordinary load/save; only explicit migration promotes them.
- Migration preserves unknown project extensions, unknown project fields, and accepted road record extensions; successful upgrades retain the old package in checkpoint-0.
- The CMake/application version remains unchanged. Schema generation 2 is independent of application version.

**Focused Checks:**
- Domain tests: 30/30 passed.
- Persistence tests: 36/36 passed.
- Application tests: 65/65 passed.
- Geometry tests: 33/33 passed, including the named performance measurement case.
- Integration tests: 2/2 passed.
- Full headless CTest: 179/179 passed.
- Supported official Qt configure/build/deployment passed; UI tests: 4/4 passed.
- Architecture governance passed; changed-file diagnostics reported no errors; `git diff --check` passed with only the unrelated existing Phase 4 document line-ending warning.

**Issues:**
- Deviations from Phase 1: no dedicated production envelope cache exists, so the named benchmark records first-run and repeated process-local execution rather than cold/warm cache hits. No standalone fuzzing harness was added; malformed-input, resource-bound, security, and seeded property tests are present but do not constitute a full fuzz campaign.
- Unimplemented or partial ledger items: cache-hit performance baseline and a dedicated fuzz/security campaign remain partial. Full public-release requirement traceability and release integration remain Phase 3/Phase 4 gates.
- Remaining implementation work: decide/implement a production envelope cache before cache-hit performance measurement is applicable; add a dedicated fuzz campaign and complete the final requirement-to-test traceability review during validation. Do not claim public v0.5.0 release readiness.
- ADR or design decision needed: none for implemented persistence behavior. A geometry-cache design is not invented in this phase because no current envelope consumer/cache owner exists.

**Phase Gate:**
- Ready for validation: no.
- Reason: schema-2 persistence, migration, fixtures, compatibility, and integration are implemented with passing regressions, but the approved G test plan's cache-performance and fuzz-campaign evidence remains incomplete. Public v0.5.0 remains unpromoted.

---

## Phase 3 - Validate & Fix (Checkpoint F, final gate)

Result: completed
Phase: 3 - Validate & Fix
Version: `v0.5.0`
Checkpoint: `v0.5.0-F` - Envelope Geometry (`R050-005`)

**Validation:**
- Build result: headless and supported official-Qt presets configured and built in initialized Visual Studio x64 shells; Qt deployment completed.
- Scope ledger result: checkpoint F / `R050-005` validated. Checkpoint G and public v0.5.0 release integration remain incomplete and are not promoted by this checkpoint result.
- Test/check suites executed: full headless CTest, Qt UI tests, architecture governance, editor diagnostics, and `git diff --check`.
- Passed: headless CTest 166/166; Qt UI tests 4/4; architecture governance; changed-file diagnostics with no errors; diff check.
- Failed: none in the final run.
- Not run: envelope-dependent export/consumer blocking because no such consumer exists in the current application; checkpoint-G package persistence, generation migration, failure/backup campaign, retained road package fixtures, and public release integration gates are later work. CMake Tools reported no configured targets/tests and the workspace tasks were hidden from its task runner; documented initialized-shell commands were used.
- Determinism/compatibility result: curved circular-arc golden pins output count/hash and checks a 5 mm measured deviation; cubic-Bezier output checks 1 mm deviation and repeated hashes. Full headless and Qt regression suites pass. Persistence migration remains G.

**Conformance:**
- CORE requirements verified: `GEOM-CORE-001` adaptive offset subdivision obeys a declared maximum measured chord deviation; `GEOM-CORE-002` cusp, tight-radius, self-intersection, and orientation diagnostics; `GEOM-CORE-003` bounded invalid previews and nonblocking Model B failures; `GEOM-CORE-004` round/bevel/miter joins with persisted miter policy and overflow diagnostics; `GEOM-CORE-005` deterministic versioned output hashes.
- Roadmap packets and ship items verified: checkpoint F / `R050-005` envelope generation, uniform persisted width, explicit joins, required diagnostics, bounded invalid preview, Model B command behavior, deterministic output metadata, and range-scoped invalidation.
- Acceptance criteria verified: version-tagged straight and curved golden cases; failure coverage for cusp, tight radius, self-intersection, orientation inversion, miter overflow, and work bounds; deterministic output; source-invalid versus derived-invalid command behavior; range invalidation.
- Release gates verified: checkpoint-F mini-gate passes. `AC-001` envelope half passes with the prior D source-model half retained. Public v0.5.0 release gates remain incomplete pending G.

**Defects:**
- Defects found: initial Phase 3 identified that curved envelope output used fixed station-step sampling without positional deviation tolerance or curved golden evidence.
- Fixes made: replaced fixed-step-only tessellation with adaptive subdivision over both offset sides using station-span and chord-deviation tolerances; bounded recursion/evaluation/sample work; added round-join sagitta control, updated version identifiers and hash inputs, and added curved arc golden plus cubic tolerance evidence.
- Regression coverage added: curved arc golden with 5 mm tolerance and fixed output hash/count; cubic-Bezier 1 mm deviation and hash repeatability; invalid chord tolerance and round-join measured-deviation assertions.
- Remaining failures/limitations: no checkpoint-F failures. No export or other downstream envelope consumer exists to exercise consumer-side blocking; package compatibility/migration remains G.
- Stop-ship defects: none for checkpoint F. Public v0.5.0 is not release-ready.

**Phase Gate:**
- Ready for release integration: no.
- Ready to proceed to checkpoint G: yes.
- Reason: the initial curved-output tolerance gap is repaired and all checkpoint-F requirements have executable evidence. Public v0.5.0 still requires checkpoint G persistence, migration, fixtures, and compatibility gates.

---

## Phase 2 - Implement (Checkpoint G repair)

Result: completed
Phase: 2 - Implement
Version: `v0.5.0`
Checkpoint: `v0.5.0-G` - Conformance and Compatibility (`R050-006`)

**Implementation:**
- Added a bounded `RoadEnvelopeCache` keyed by authoritative road/segment source, project geometry policy, algorithm/tolerance versions, and all generation options. Command previews reuse matching derived output; source or policy changes miss the cache.
- Updated the road performance baseline to measure actual cold envelope generation and warm cache hits, alongside curve construction and station-query median/p95 timings.
- Added a 40-case seeded malformed-road ownership campaign alongside the 24-case seeded valid package round-trip campaign and existing malformed-input/resource/security tests.
- Added the full road integration workflow: create, add/edit geometry, remap a Geometry-locked anchor, split, merge, save/reopen schema 2, reverse twice, and assert exact normalized-source equivalence.
- Preserved the transactional generation-1-to-2 migration, checkpoint/report/rollback behavior, explicit schema-preserving ordinary saves, schema-1 road exclusion, unknown road fields, and canonical `minimal-road`/`split-road` fixtures.
- Updated the build guide's migration example to target schema generation 2.

**Scope ledger status:**
- Checkpoints A-F remain recorded in prior entries.
- Checkpoint G / `R050-006`: schema-2 persistence, 1→2 migration, backup/report/failure rollback, compatibility fixtures, unknown-field preservation, seeded input campaigns, integration workflow, and actual cache performance measurements are implemented and tested.
- Public v0.5.0 release integration/version promotion remains intentionally unperformed.

**Roadmap packets addressed:**
- `R050-006` conformance and compatibility.

**CORE requirements addressed:**
- `SAVE-CORE-005`: one staged schema 1→2 replacement, recoverable checkpoint-0, machine-readable report, and restoration of the last readable package on injected post-backup failure.
- `ARCH-CORE-002`, `ARCH-CORE-003`, `ARCH-CORE-004`: road families persist as first-class schema-2 records with stable identity and ownership; schema 1 rejects road serialization rather than dropping records.
- Road source compatibility: unknown project, RoadSpline, and RoadSegment fields round-trip; manifest and project schema generations must agree.
- Semantic references: station IDs/signatures and RoadSegment boundary IDs persist; no raw collection position is used as identity.

**Requirement-to-test traceability:**
- `R050-001` / curve source: geometry `CurveKernel` unit/property/golden cases and domain `Project.R050_001_*` record tests.
- `R050-002` / anchors: `StationAnchor.*` affinity, remap, ambiguity, and determinism tests.
- `R050-003` / road commands: `CommandProcessor` create/edit/extend/shorten/reverse/delete, revision, anchor remap, undo, and stale-command cases.
- `R050-004` / segmentation: split/move/merge/lineage, exact undo/redo, conflict resolution, and seeded coverage properties in `CommandProcessor` tests.
- `R050-005` / envelopes: `RoadEnvelope.*`, `RoadEnvelopeGolden.*`, Model B transaction tests, and the retained road-envelope golden/performance fixtures.
- `R050-006` / persistence: `Package.R050_006_*`, `Project.R050_006_*`, and `RoadWorkflow.CreateEditRemapSplitMergeSaveReopenReverseTwice`.
- `AC-001` source/envelope halves: default full-domain road/width tests, valid default envelope, and invalid-derived-envelope nonblocking commit tests.
- `AC-003`, reverse-twice, and prior compatibility: StationAnchor suite, road command/integration tests, retained `empty-project.atlas` replay, and full CTest.

**Source files/components changed:**
- `src/atlas/persistence/package.hpp` and `src/atlas/persistence/package.cpp`: schema-aware package writer/loader, explicit schema migration, validation, report, checkpoint and rollback.
- `src/atlas/domain/project.hpp` and `src/atlas/domain/project.cpp`: schema-generation serialization and unknown road-record field preservation.
- `src/atlas/application/command.hpp` and `src/atlas/application/command.cpp`: edit-time StationAnchor remapping, fail-closed unresolved anchors, and cached envelope previews.
- `src/atlas/geometry/envelope.hpp` and `src/atlas/geometry/envelope.cpp`: bounded source/options-keyed derived envelope cache.
- `tests/atlas/persistence/package_tests.cpp`, `tests/atlas/domain/project_tests.cpp`, `tests/atlas/application/command_tests.cpp`, `tests/atlas/integration/workflow_tests.cpp`, and `tests/atlas/geometry/performance_tests.cpp`: G compatibility, malformed-input, cache, and integration coverage.
- `tests/fixtures/compatibility/minimal-road.json`, `tests/fixtures/compatibility/split-road.json`, and `tests/fixtures/performance/road-envelope-v2-baseline.json`: retained road and performance fixtures.
- `README.build.md`: schema-2 migration CLI example.

**Tests added/updated:**
- Migration dry-run/success/checkpoint, failure rollback, malformed-source report, generation mismatch, schema-1 ordinary-save preservation, and schema-1 road-array rejection.
- `minimal-road`/`split-road` schema-2 package round trips including unknown fields; 24-seed valid package round trips and 40-seed malformed ownership rejection.
- Cross-module road create/edit/remap/split/merge/save/reopen/reverse-twice integration.
- Bounded envelope-cache hit/miss/eviction tests, command preview cache reuse, and a named cold/warm benchmark with analytic construction, station-query median/p95, envelope-generation median, and cache-hit median/p95.
- Every new or modified test has a purpose comment.

**Documentation/comments added/updated:**
- Updated `README.build.md` migration example. Phase 1 and prior phase records remain unchanged; no public version metadata was promoted.

**Persistence/compatibility impact:**
- New packages use schema generation 2 with top-level `roadSplines` and `roadSegments`. Older schema-1 packages remain readable and ordinary saves retain generation 1; explicit migration alone upgrades them.
- Migration preserves unknown data and retains the last readable schema-1 package in checkpoint-0. Failed migration returns a report and restores the original package.
- The application/CMake version remains unchanged; schema generation is an independent compatibility dimension.

**Focused Checks:**
- Full headless CTest: 182/182 passed.
- Supported official Qt configure/build/deployment passed; UI tests: 4/4 passed.
- Architecture governance passed; changed-file diagnostics reported no errors; `git diff --check` passed with only the unrelated existing Phase 4 document line-ending warning.
- Named performance baseline: AMD Ryzen 7 7800X3D, Windows, MSVC 14.51.36231 x64; cold envelope 40.9207 ms; warm cache lookup median/p95 0.014/0.0172 ms; 500 station queries median/p95 4.9989/5.7705 ms; curve construction 0.099 ms.

**Issues:**
- Deviations from Phase 1: no standalone libFuzzer target was introduced; deterministic seeded valid/malformed campaigns and the existing malformed-input, path, resource-limit, and degenerate-geometry tests provide bounded security/property evidence.
- Unimplemented or partial ledger items: no known checkpoint-G acceptance item remains unimplemented. Performance evidence is tied to the named host/dataset; it is a baseline, not a portable timing threshold.
- Remaining implementation work: Phase 3 must validate the full v0.5.0 ledger and regression/fixture replay. Phase 4 version propagation and release integration remain out of scope for this phase.
- ADR or design decision needed: none. The cache is derived, bounded, keyed by canonical source/options, and carries the accepted algorithm/tolerance versions.

**Phase Gate:**
- Ready for validation: yes.
- Reason: schema-2 road persistence, migration/rollback, canonical fixtures, unknown-data compatibility, requirement traceability, end-to-end integration, bounded cache measurements, and complete current regressions have focused evidence. No Phase 3 or Phase 4 work is claimed.

---

## Phase 3 - Validate & Fix (Checkpoint G, final gate)

Result: completed
Phase: 3 - Validate & Fix
Version: `v0.5.0`
Checkpoint: `v0.5.0-G` - Conformance and Compatibility (`R050-006`)

**Validation:**
- Build result: headless and supported official-Qt presets configured and built in initialized Visual Studio x64 shells; Qt deployment completed.
- Scope ledger result: checkpoints A-G validated against their phase records and the final full regression/compatibility run. Public version propagation remains Phase 4 work and was not performed.
- Test/check suites executed: full headless CTest, Qt UI suite, architecture governance, editor diagnostics, diff check, migration/package/fixture tests, road workflow integration, seeded malformed-input campaign, and cold/warm cache performance test.
- Passed: full headless CTest 182/182; Qt UI tests 4/4; migration and compatibility suites; architecture governance; changed-file diagnostics with no errors; diff check.
- Failed: none.
- Not run: Phase 4 version propagation/signing/release integration, by instruction. No separate external libFuzzer process was run; seeded valid/malformed campaigns and the specified deterministic security/resource failure cases ran as part of the suite.
- Determinism/compatibility result: `minimal-road` and `split-road` schema-2 fixtures round-trip without normalized-source drift; schema-1 packages remain readable and preserve generation on ordinary saves; explicit 1→2 migration writes a recoverable generation-1 checkpoint and restores the source after injected failure. Cache output hashes and source/options keys are deterministic. The named cache baseline records cold generation, warm hits, station queries, and envelope median/p95.

**Conformance:**
- CORE requirements verified: `SAVE-CORE-005` transactional migration/backup/report/rollback; `ARCH-CORE-002`/`003`/`004` first-class road identity and ownership; `GEOM-CORE-001`-`005` bounded analytic envelopes, diagnostics, joins, and deterministic versioned output; `SEGM-CORE-001`-`005` segmentation; `STAT-CORE-001`-`005` anchor behavior and transaction rules; `MAP-CORE-001` one-map ownership.
- Roadmap packets and ship items verified: `R050-001` through `R050-006`, with the requirement-to-test mapping recorded in the Phase 2 checkpoint-G repair report and preceding checkpoint reports.
- Acceptance criteria verified: `AC-001` source/envelope halves; `AC-003` StationAnchor remapping; reverse-twice normalized equivalence; split/merge coverage and lineage; invalid-offset bounded-preview Model B behavior; schema-2 migration, unknown-field preservation, canonical fixtures, and full prior-release regression replay.
- Release gates verified: all checkpoint and compatibility gates are evidenced by the full test suite, retained fixtures, integration workflow, and performance baseline. Phase 4 release packaging/version propagation remains separate.

**Defects:**
- Defects found: the initial G reconciliation identified missing schema-2 persistence/migration, unknown road-field preservation, canonical fixtures, end-to-end road workflow, cache-hit timing, and malformed ownership campaign evidence.
- Fixes made: implemented in the Phase 2 G repair entry; the final Phase 3 rerun found no remaining in-scope G defect.
- Regression coverage added: transactional migration success/failure and source preservation; schema compatibility/manifest matching; canonical and seeded road package round trips; seeded malformed ownership rejection; command-level anchor remapping; cold/warm cache key and performance tests; complete road workflow integration.
- Remaining failures/limitations: none for checkpoint G. Performance values are host-specific evidence, not portable timing thresholds.
- Stop-ship defects: none for v0.5.0.

**Phase Gate:**
- Ready for release integration: yes.
- Reason: all in-scope packets, CORE requirements, acceptance criteria, migration/compatibility behaviors, retained fixtures, requirement-to-test traceability, performance baseline, and prior-release regressions have executable evidence. Phase 4 was not started.

---

## Phase 3 - Validate & Fix (Checkpoint G, final gate)

Result: completed
Phase: 3 - Validate & Fix
Version: `v0.5.0`
Checkpoint: `v0.5.0-G` - Conformance and Compatibility (`R050-006`)

**Validation:**
- Build result: headless and supported official-Qt presets configured and built in initialized Visual Studio x64 shells; Qt deployment completed.
- Scope ledger result: checkpoints A-G validated against their phase records and the final full regression/compatibility run. Public version propagation remains Phase 4 work and was not performed.
- Test/check suites executed: full headless CTest, Qt UI suite, architecture governance, editor diagnostics, diff check, migration/package/fixture tests, road workflow integration, and named cold/warm cache performance test.
- Passed: full headless CTest 182/182; Qt UI tests 4/4; migration and compatibility suites; architecture governance; changed-file diagnostics with no errors; diff check.
- Failed: none.
- Not run: Phase 4 version propagation/signing/release integration, by instruction. A separate libFuzzer executable was not run; the approved malformed-input campaign was deterministic and seeded (40 malformed road ownership cases plus 24 valid road package variations), alongside degenerate-curve, invalid-radius, package-size/path, resource-limit, and injected migration-failure tests.
- Determinism/compatibility result: `minimal-road` and `split-road` schema-2 fixtures round-trip without normalized-source drift; schema-1 packages remain readable and preserve generation on ordinary saves; explicit 1→2 migration writes a recoverable generation-1 checkpoint and restores the source after injected failure. The cache benchmark records deterministic hashes, cold generation, warm cache hits, station-query median/p95, and envelope median/p95 on the named dataset.

**Conformance:**
- CORE requirements verified: `SAVE-CORE-005` transactional migration/backup/report/rollback; `ARCH-CORE-002`/`003`/`004` first-class road identity and ownership; `GEOM-CORE-001`-`005` bounded analytic envelopes, diagnostics, joins, and deterministic versioned output; `SEGM-CORE-001`-`005` segmentation; `STAT-CORE-001`-`005` anchor behavior and transaction rules; `MAP-CORE-001` one-map ownership.
- Roadmap packets and ship items verified: `R050-001` through `R050-006`, with evidence mapped in the Phase 2 checkpoint-G traceability list and preceding checkpoint reports.
- Acceptance criteria verified: `AC-001` source/envelope halves; `AC-003` StationAnchor remapping; reverse-twice normalized equivalence; split/merge coverage and lineage; invalid-offset bounded-preview Model B behavior; schema-2 migration, compatibility fixtures, and full prior-release regression replay.
- Release gates verified: all checkpoint and compatibility gates are evidenced by the full test suite and retained fixtures. Phase 4 release packaging/version propagation remains to be performed separately.

**Defects:**
- Defects found: the Phase 2 G review identified absent schema-2 persistence/migration, unknown road-field preservation, canonical road fixtures, complete road workflow integration, and cache/fuzz/performance evidence; the final rerun found no remaining checkpoint-G defect.
- Fixes made: schema-aware package load/save and 1→2 migration; rollback/report tests; road unknown-field preservation; canonical fixtures; edit-time anchor remapping and end-to-end workflow; bounded envelope cache with command-preview reuse; seeded valid/malformed road package campaigns; and recorded hardware-specific performance baseline.
- Regression coverage added: migration dry-run/success/failure, no silent schema promotion, schema mismatch/road exclusion, fixture and unknown-field round trips, 24 seeded valid packages, 40 malformed ownership mutations, road workflow integration, cache hit/miss/bounds, and cold/warm performance measurements.
- Remaining failures/limitations: none for checkpoint G. Performance numbers are a named-host baseline, not a portable timing threshold; no standalone libFuzzer binary was introduced, but the bounded deterministic failure campaign covers the specified malformed-input/security cases.
- Stop-ship defects: none for v0.5.0 checkpoint G.

**Phase Gate:**
- Ready for release integration: yes.
- Reason: all in-scope checkpoints, CORE requirements, acceptance criteria, migration/compatibility behaviors, retained fixtures, requirement-to-test traceability, and prior-release regressions have executable evidence. Phase 4 was not started.

---

## Phase 1 Addendum - Checkpoint H Plan (v0.5.0)

Result: completed
Phase: 1 - Analyze & Plan
Version: `v0.5.0`
Checkpoint: `v0.5.0-H` - Reversal, Package Integrity, and Bounded Failure Hardening (`R050-006` sub-slice)

**Repository State:**
- Checkpoints A-G have implementation and phase evidence; the latest G Phase 3 entry reports ready for release integration.
- Current road source contains cross-section elements, station attachments, schema-2 persistence, road envelope generation/cache, and command-level edits. The UI remains the generic v0.4 shell.
- Existing tests verify reverse-twice source equivalence and segment ordering, but do not assert one-way cross-section side/directional remapping.
- Package loading validates each manifest hash entry that is present, but does not require the complete schema-defined authoritative file inventory.
- Envelope recursion limits curve samples, but round-join tessellation and pairwise polygon self-intersection checks lack a shared worst-case work budget.
- The migration dry-run test currently requests schema 2→1 and only checks `dryRun`; it does not exercise the supported 1→2 path.

**Release Scope:**
- Objective: harden the four identified v0.5.0 correctness/security/test gaps as internal checkpoint H, without adding new domain families or changing schema generation.
- Scope ledger:
  - H1 / Reverse semantics: planned; one reverse remaps Left/Right groups and defined directional values while preserving element IDs/references, with a reverse-twice normalized-source invariant.
  - H2 / Complete manifest inventory: planned; schema-specific required authoritative file hashes are enforced and omission/tampering fails closed.
  - H3 / Envelope work bounds: planned; round-join vertex generation and polygon self-intersection work share explicit bounded-work behavior and return a bounded invalid preview when limits are reached.
  - H4 / Migration dry-run evidence: planned; correct the test to exercise schema 1→2 dry-run success and immutability; retain a separate unsupported-downgrade assertion.
  - GUI RoadSpline authoring: explicitly deferred to v0.5.1 by user direction; excluded from H.
- In scope: `R050-006` conformance hardening; `ROAD-CORE-003`; cross-section reversal behavior defined by the existing specification; `FILE-CORE-001`; `GEOM-CORE-002`/`003` resource-bounded failures; `SAVE-CORE-005` migration evidence.
- Explicit non-goals: Qt road tools and road UI tests (reserved for v0.5.1); lane-native authoring, RoadTransition, network/junction behavior, elevation, export, and schema-generation changes.
- Roadmap packets: `R050-006`, H sub-slice only.
- CORE requirements: `ROAD-CORE-003`; `XSEC-CORE-001`/`003`/`004` as applicable to reversal; `FILE-CORE-001`; `GEOM-CORE-002`/`003`; `SAVE-CORE-005`.
- Acceptance criteria: one-way reversal remaps implemented sides/directions and preserves stable IDs/references; reverse twice restores normalized source; required manifest hashes cannot be omitted; adversarial joins/polygons terminate within bounded work with invalid bounded previews; supported migration dry-run reports success without modifying source or checkpoints.
- Release gates: H-specific tests and full prior regression pass. Public v0.5.0 release integration remains blocked until GUI deferral is reconciled with the Roadmap's v0.5.0 user-visible outcome.

**Plan:**
- Reversal: inspect and transform the canonical `crossSectionState.elements` representation in the compound reverse command. Swap Left/Right, reverse Center ordering where specified, remap defined `travelDirection` values, retain element IDs and compatible references, preserve opaque extension data, and verify both single-reverse semantics and reverse-twice equality.
- Package integrity: derive the required hash inventory for supported schema generations and root-map package layout; reject missing core entries and missing/unreadable hashed files while preserving permitted extension files and excluding disposable cache data. Add tests for each omitted required entry and for source tampering when its inventory entry is absent.
- Envelope bounds: add an aggregate work budget covering round joins and polygon intersection validation, with deterministic resource diagnostics. Test tiny positive chord error at a sharp corner, high-turn/many-edge input, and limit exhaustion; ensure no invalid partial polygon is presented as valid.
- Migration dry-run: create a valid schema-1 package, snapshot manifest/project/all authoritative bytes and checkpoints, dry-run target 2, assert `succeeded`, `changed`, and `dryRun`, and prove snapshots are unchanged. Test unsupported 2→1 separately.
- Validation: run focused reversal, manifest integrity, resource-limit, and migration tests; then full headless CTest, supported Qt build/UI regression (without adding GUI scope), architecture governance, compatibility fixture replay, editor diagnostics, and `git diff --check`.
- Persistence/compatibility impact: no schema-generation change. The package inventory rule must remain backward-compatible with schema 1 and complete for schema 2; migration semantics remain unchanged.
- Documentation/versioning impact: append H implementation/validation evidence to this log. Do not promote application metadata or public release status. Record the v0.5.1 GUI deferral; reconcile `ROADMAP.md` before claiming v0.5.0 release integration.

**Risks / Decisions:**
- Stop conditions: stop if custom cross-section/directional fields have no canonical reverse mapping; stop if manifest completeness cannot distinguish required authoritative files from supported extensions; stop if an envelope work bound would make valid source appear valid while returning incomplete geometry.
- ADR or design decision needed: none anticipated for existing canonical fields. If an unknown/custom directional field requires semantic interpretation, preserve it or request an ADR rather than guessing.
- Version-scope conflict: the user explicitly reserves road GUI work for v0.5.1, while the current v0.5.0 Roadmap user-visible outcome includes road authoring. H excludes GUI as requested; the Roadmap/version-completion statement must be reconciled before public release integration.

**Phase Gate:**
- Ready to implement: yes, for checkpoint H's four targeted items.
- Ready for release integration: no.
- Reason: H is a bounded, testable `R050-006` hardening slice but is not implemented yet, and the GUI deferral conflicts with the published v0.5.0 outcome.

---

## Phase 2 — Implement

Result: completed
Phase: 2 — Implement
Version: v0.5.0

Implementation:
- Scope implemented: H1 reverse semantics; H2 required authoritative hash inventory and rooted-path rejection; H3 aggregate envelope work limits; H4 supported migration dry-run evidence.
- Scope ledger status: H1-H4 implemented with focused tests. GUI authoring remains explicitly deferred to v0.5.1.
- Roadmap packets addressed: `R050-006` H sub-slice.
- CORE requirements addressed: `ROAD-CORE-003`; `XSEC-CORE-001`/`003`/`004`; `FILE-CORE-001`; `GEOM-CORE-002`/`003`; `SAVE-CORE-005`.
- Source files/components changed: `src/atlas/application/command.cpp`; `src/atlas/persistence/package.cpp`; `src/atlas/geometry/envelope.cpp`.
- Tests added/updated: `tests/atlas/application/command_tests.cpp`; `tests/atlas/persistence/package_tests.cpp`; `tests/atlas/geometry/curve_tests.cpp`. Coverage verifies one-way and double reversal, complete and relative manifest paths, bounded join/intersection work, and schema 1-to-2 dry-run immutability with downgrade rejection.
- Documentation/comments added/updated: H plan retained; this implementation report appended. Added concise test intent comments.
- Persistence/compatibility impact: no schema-generation change. Required schema-defined core hashes are enforced, rooted manifest hash paths are rejected, and migration behavior is unchanged.

Focused Checks:
- Checks/tests run: H-focused reversal, manifest, envelope, and migration tests; full headless build and CTest; supported Qt build/deployment and UI tests; architecture governance; `git diff --check`.
- Result: all focused tests passed; headless CTest passed 187/187; Qt UI tests passed 4/4; architecture governance passed; diff check passed.

Issues:
- Deviations from Phase 1: added rejection of rooted manifest hash paths after identifying that extra hash entries could resolve outside the package root.
- Unimplemented or partial ledger items: none within H. GUI authoring remains outside H as directed.
- Remaining implementation work: none for H. Reconcile the v0.5.1 GUI deferral with the v0.5.0 Roadmap outcome before public release integration.
- ADR or design decision needed: none for H. Editor diagnostics report three `contains` errors in `command.cpp` for standard containers despite successful C++23 preset builds; the IDE configuration discrepancy remains to be resolved.

Phase Gate:
- Ready for validation: yes
- Reason: H acceptance criteria are implemented and the focused and regression gates pass. This is implementation completion only; it does not establish release readiness, and the Roadmap/UI scope conflict remains.

---

## Phase 3 — Validate & Fix

Result: partial
Phase: 3 — Validate & Fix
Version: v0.5.0

Validation:
- Build result: headless configure/build passed after the H3 stress-test addition; the Qt configure/build and deployment passed. The final H4 test-only change was rebuilt in the headless persistence target and its focused test passed.
- Scope ledger result: H1-H4 acceptance criteria validated by focused tests. Public v0.5.0 release scope remains blocked by the explicitly deferred GUI item.
- Test/check suites executed: full headless CTest (187/187) and full Qt-build CTest (191/191) before the final H4 test-only expansion; final H4 dry-run focused test; reversal, manifest, envelope, migration, fixture, and performance focused tests; architecture governance; `git diff --check`.
- Passed: headless 187/187; Qt build-directory CTest 191/191 including UI; H1 reversal 1/1; H2 inventory/path and persistence/migration cases; H3 resource exhaustion and road-envelope performance; H4 expanded snapshot test 1/1; compatibility/migration selection 14/14; architecture governance and diff check.
- Failed: no test failures observed. CMake Tools `RunCtest` could not start (`Build failed`); its test discovery returned no available tests and its diagnostics panel was empty.
- Not run: complete headless and Qt suites after the final H4 test-only expansion, because CMake Tools had no discovered tests in the active workspace configuration. The changed H4 test itself was rebuilt and passed.
- Determinism/compatibility result: retained empty-project, minimal-road, and split-road fixture replays; seeded road package round trips; schema migration/rollback; and deterministic/golden tests passed in the recorded full-suite runs. The final H4 test compares every file under the package and checkpoint tree before and after dry-run.

Conformance:
- CORE requirements verified: `ROAD-CORE-003`; `XSEC-CORE-001`/`003`/`004`; `FILE-CORE-001`; `GEOM-CORE-002`/`003`; `SAVE-CORE-005`, with H-specific tests and full-suite evidence as noted above.
- Roadmap packets and ship items verified: `R050-006` checkpoint-H sub-slice validated. The v0.5.0 Roadmap user-visible road-authoring outcome remains blocked by the user's GUI deferral to v0.5.1.
- Acceptance criteria verified: H1 one-way and double reversal; H2 required core hashes and rooted-path rejection; H3 round-join and many-edge intersection work exhaustion returns bounded invalid output; H4 schema 1-to-2 dry-run leaves package and checkpoints byte-identical.
- Release gates verified: H-specific acceptance tests pass. Public release integration is not verified or ready because of the GUI scope conflict and the missing post-H4 full-suite rerun.

Defects:
- Defects found: H3 lacked explicit many-edge stress evidence; H4 dry-run coverage compared only project and manifest files and did not exercise existing checkpoints.
- Fixes made: expanded H3 coverage with a deterministic 64-edge path; expanded H4 to create two checkpoints and snapshot/compare every file recursively.
- Regression coverage added: many-edge bounded-work assertion and complete package/checkpoint dry-run immutability assertion.
- Remaining failures/limitations: the final full suites were not rerun after the H4 test-only expansion. Editor diagnostics still report three `std::set/map::contains` errors in `command.cpp`, though the configured C++23 preset builds passed and the diagnostics are not reproduced by those builds. The Roadmap's v0.5.0 GUI outcome still conflicts with the v0.5.1 deferral.
- Stop-ship defects: no runtime stop-ship defect was found in the executed tests. Public v0.5.0 integration remains blocked by the unresolved GUI scope conflict.

Phase Gate:
- Ready for release integration: no
- Reason: H1-H4 behavior is validated, but final full-suite evidence after the last test change is unavailable through the active CMake Tools configuration, and the v0.5.0 Roadmap GUI outcome remains unreconciled with the explicit v0.5.1 deferral.

---

## Phase 1 Addendum — Checkpoint J Plan (v0.5.0)

Result: completed
Phase: 1 — Analyze & Plan
Version: v0.5.0
Checkpoint: v0.5.0-J — Accessible Road Authoring Workspace

Repository State:
- Current version/state: README and ROADMAP still identify v0.4.0 as current. Checkpoints A-G have prior implementation and validation records; H Phase 3 is recorded as partial. The desktop executable remains a generic shell with an empty startup project and no road tools or package open/save actions.
- Relevant existing implementation: `src/atlas/ui/main.cpp` constructs a `QMainWindow`, generic canvas, Hierarchy and Inspector docks, status bar, and a minimal command palette. `CanvasWidget` only draws/selects generic point objects and navigates the camera. Road source types, curve geometry, `CommandProcessor`, create/edit/extend/shorten/reverse/delete/split/boundary-move/merge commands, previews, diagnostics, undo/redo, envelope output, and schema-2 persistence already exist in their respective modules. The UI target does not yet link `atlas_persistence`.

Release Scope:
- Objective: add checkpoint J inside v0.5.0, not v0.5.1, so users can create, edit, measure, split, merge, reverse, save, and reopen roads through the supported Qt desktop interface. This user direction supersedes the H addendum's GUI deferral prospectively; prior H records remain historical evidence.
- Scope ledger: `R050-001` validated for the curve/station backend; `R050-002` validated for anchor/remap backend; `R050-003` planned for complete keyboard-operable desktop create/edit/extend/shorten/reverse/delete flows (command backend exists); `R050-004` planned for desktop split/boundary-move/merge flows (command backend exists); `R050-005` planned for canvas envelope preview and diagnostics (engine exists); `R050-006` planned for UI integration, accessibility, save/load, and conformance evidence. Existing backend status does not count as end-to-end UI validation.
- Ship-item ledger: curve primitives, canonical direction, stationing, anchor data/remap, automatic initial segment, segment coverage/lineage, 8.0 m placeholder, offset diagnostics, and reverse semantics have backend implementation evidence in earlier checkpoints. Their user-facing authoring, inspection, resolution, and persistence flows are `planned` for J; none is marked end-to-end validated before implementation.
- In scope: extend the existing Qt Widgets workspace; expose the existing RoadSpline command path; create/edit/extend/shorten/reverse/delete roads; split, move boundaries, and merge segments; measure plan distance/station; preview affected anchors, diagnostics, and derived envelopes; explicit Apply/Cancel and undo/redo; accessible hierarchy/inspector/tool actions; package open/save and explicit schema upgrade behavior needed to preserve authored roads.
- Explicit non-goals: lane-native cross-section editing or lineage (`v0.6.0`); road connectivity, junctions, transitions, elevation, 3D, export, collaboration, and procedural features; changing domain ownership, stable-ID rules, world units, curve or station semantics, persisted road schema, or derived-envelope policy; replacing the application shell or redesigning unrelated generic-object tools; public application-version promotion.
- Roadmap packets: `R050-001`-`R050-006`; J directly completes the missing supported-interface acceptance for `R050-003`/`004` and adds visible derived output/accessibility/integration evidence for `R050-005`/`006`. Earlier packet backend evidence remains governed by its existing records.
- CORE requirements: road/backend contracts `ARCH-CORE-002`/`003`/`004`, `GEOM-CORE-001`-`005`, `MAP-CORE-001`, `ROAD-CORE-001`-`003`, `SEGM-CORE-001`-`005`, and `STAT-CORE-001`-`005` remain invariants; J specifically exercises `STAT-CORE-003`/`004`, `UNDO-CORE-001`/`002`, `VAL-CORE-002`, `UX-CORE-001`-`004`, and `A11Y-CORE-001`-`005`.
- Acceptance criteria: `AC-001` create exactly one full-domain segment with valid anchors and an envelope; `AC-003` control-point edits remap geometry-locked anchors and preview ambiguity; split/undo exact-source equivalence and the v0.5.0 reverse-twice gate; keyboard completion and visible/accessibly named focus for every shipped J workflow.
- Release gates: all v0.5.0 gates from the Roadmap remain in force: AC-001, AC-003, reversal equivalence, split/merge coverage and lineage, station/edit properties, invalid-offset bounded preview and source policy, retained fixture save/reopen without drift, and prior-release regression. J adds UI-level execution, cancel/no-mutation, undo/redo, accessibility, and save/load evidence. Release integration remains `planned` until Phase 3 validates every row.
- Compatibility fixtures: `tests/fixtures/compatibility/empty-project.atlas`, `minimal-road.json`, and `split-road.json` remain required. Prior records contain backend replay evidence; J must also open/resave/reopen supported road packages through the desktop path without normalized-source drift.

Plan:
- UI layout: keep a 1280×800 default `QMainWindow`. Row 1 is the menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, plus the existing command-palette entry). Row 2 is a 40 px primary toolbar with labeled `Select`, `Road`, `Edit`, `Split`, and `Measure` actions plus icon-and-accessible-name `Undo`/`Redo`. A 36 px context toolbar appears below it for the active tool: creation uses segmented `Polyline`/`Bezier`/`Fixed-radius arc` modes, grid/snap toggle, exact radius input when applicable, and the 8.0 m uniform-width creation default.
- UI layout continued: below the toolbars, dock a 240 px `Hierarchy` on the left (`Project > Map > RoadSplines > RoadSegments`, each segment labeled by station interval), the resizable authoring canvas in the center, and a 300 px `Inspector` on the right. The Inspector shows selected type/owner/Map, road direction and plan length, editable control-point/station values, segment interval and persisted placeholder width, and context-appropriate road commands; it is not a lane editor. A bottom `Changes & Diagnostics` dock, 180 px when shown and otherwise collapsed, lists the candidate change, affected anchors/references, envelope validity, and diagnostics with `Apply`/`Cancel` and required resolution choices. The status bar shows world coordinates in meters, selected road station, active Map/level/layer, zoom, tool, and warning state. Focus order is toolbar, hierarchy, canvas, Inspector, then preview/diagnostics.
- UI layout continued: road source is drawn as a selectable centerline with stable-ID control handles; its derived envelope is a subordinate fill. Invalid derived output is visibly labeled and bounded, never styled as valid. Direction and warning states use labels/shapes as well as color. All controls expose accessible names, roles, checked/enabled state, visible focus, and scale without clipping.
- Interaction plan: `R` activates Road, `V` Select, `E` Edit, `S` Split, and `M` Measure when a text editor does not own focus; `Enter` finishes a draft or accepts a ready preview; `Escape` cancels the draft/preview without source mutation and returns to Select; `Ctrl+Z`/`Ctrl+Y` undo/redo; `Ctrl+Shift+P` opens the command palette. Every action is also available through menus/toolbars and keyboard focus, not shortcuts alone.
- Interaction plan continued: Polyline creation adds snapped vertices; Bezier creation edits endpoint/control handles; fixed-radius arc creation takes start/end plus an exact radius field. A persistent draft preview shows centerline, 8.0 m envelope, snapping target, and diagnostics. Commit invokes `CreateRoadSplineCommand` once; it must yield one full-domain segment and generated endpoint anchors. Edit uses control handles and exact fields through `EditRoadSplineCommand`; endpoint actions use extend/shorten commands. Anchor ambiguity and shortening dependents are listed in the preview with only valid Move/Delete dependent/Cancel choices. Split accepts a snapped cursor station or exact meter value; merge requires adjacent segments and previews conflicting fields with explicit upstream/downstream resolution before commit. Reverse, delete, and boundary move use the same preview/commit boundary. Measure reports plan distance and along-road station without changing source.
- Interaction plan continued: `MainWindow` owns the current `CommandProcessor`, selection context, dirty state, package path, actions, and docks. `CanvasWidget` owns only camera, tool/draft presentation, hit testing, and derived drawing; it submits requested operations to the window/controller and never mutates authoritative records. All changes use `preview`, display candidate diagnostics/derived envelope, then `commit`; `cancel` leaves normalized source unchanged. Stable IDs are assigned once per draft and remain stable through preview/commit/undo.
- Persistence workflow: File provides New, Open, Save, and Save As through `atlas_persistence::Package`. New projects use schema 2. Loading a schema-1 project must not silently rewrite it. If road records need saving to a schema-1 package, the UI first explains the schema-2 requirement and requires explicit migration consent; it invokes the existing transactional `Package::migrate(path, 2)` with its checkpoint/report behavior before saving the current project. Failure leaves the last readable on-disk package intact and the editor dirty. No schema or record changes are planned.
- Ledger ownership and evidence plan: `main_window.hpp/.cpp` owns shell/actions/file workflows; `main.cpp` becomes bootstrap only; `canvas_widget.hpp/.cpp` owns road tool presentation, hit testing, derived overlays, and keyboard navigation; `CMakeLists.txt` links the UI target to persistence; `tests/atlas/ui/road_authoring_workflow_tests.cpp` owns cross-widget flows and `tests/atlas/ui/canvas_widget_tests.cpp` retains canvas/accessibility coverage. Existing application, geometry, domain, persistence, and integration tests remain the authority for their modules.
- Tests required: keyboard-only and mouse-assisted create/edit/extend/shorten/reverse/delete/split/boundary-move/merge/measure flows; create AC-001 invariants; anchor ambiguity and Move/Delete/Cancel behavior; conflicting merge choices; cancel leaves normalized source/revision/history unchanged; commit then undo restores normalized source, IDs, references, and selection; redo replays; road envelope valid/invalid preview states; save/open/reopen schema-2 road round trip; schema-1 explicit-upgrade/no-silent-rewrite and migration-failure recovery; compatibility fixture replay; Qt accessibility names/state/focus order, non-color direction/errors, 100%/200% scaling and visible focus. Run supported headless and Qt builds/full CTest, focused UI integration, architecture checks, visual smoke review, and `git diff --check`.
- Documentation/versioning impact: Phase 2 adds this checkpoint plan and implementation evidence to the existing log; Phase 4 alone may update public status/version/changelog. Do not change `ROADMAP.md` to remove the already-required GUI outcome or promote app metadata during J.
- Persistence/compatibility impact: no new source fields or schema generation. Continue canonical meters, one-Map ownership, stable IDs, unknown-field preservation, derived/source separation, schema-1 ordinary-save preservation, explicit 1-to-2 migration, checkpoints, and transactional failure behavior.

Risks / Decisions:
- Stop conditions: stop if an operation cannot be represented by the existing command/preview boundary; if UI-created primitives would require guessed or new persisted semantics; if station ambiguity lacks a safe resolution; if valid source versus invalid derived-envelope behavior is obscured; if save/migration can replace the last readable package; or if stable IDs/selection cannot survive undo/redo.
- ADR or design decision needed: none anticipated. ADR-0001 and ADR-0003 establish the Qt Widgets shell/canvas boundary; ADR-0006 through ADR-0012 define curve, station, anchor, source/derived, persistence, width, and miter behavior. Any need to change those contracts stops J pending an ADR.

Phase Gate:
- Ready to implement: yes, for checkpoint J.
- Reason: the user has explicitly assigned the missing road authoring UI to v0.5.0J; the canonical UI layout, command boundary, persistence compatibility behavior, tests, and non-goals are defined. Ready for release integration: no; J is unimplemented, H's final full-suite rerun remains outstanding, and the full v0.5.0 ledger must pass Phase 3 before Phase 4.

---

## Phase 2 — Implement (Checkpoint J)

Result: completed
Phase: 2 — Implement
Version: v0.5.0
Checkpoint: v0.5.0-J — Accessible Road Authoring Workspace

Implementation:
- Scope implemented: replaced the empty generic desktop shell with a road-authoring workspace; created polyline, cubic Bezier, and fixed-radius arc RoadSplines; edited control points and endpoints; measured stations; split, moved boundaries, merged, reversed, and deleted roads through preview/commit/cancel and undo/redo; added package open/save and explicit schema migration; exposed hierarchy, Inspector, derived envelope diagnostics, and keyboard commands.
- Scope ledger status: J user-facing flows for `R050-003`/`004` implemented; canvas-derived envelope display and failure diagnostics for `R050-005` implemented; UI/accessibility/integration scope for `R050-006` implemented with focused tests. `R050-001`/`002` retain their earlier backend evidence. This does not change the separate H Phase 3 partial status or validate the full v0.5.0 release ledger.
- Roadmap packets addressed: J slice of `R050-003`, `R050-004`, `R050-005`, and `R050-006`.
- CORE requirements addressed: existing road contracts remain unchanged; UI exercises `ROAD-CORE-003`, `STAT-CORE-003`/`004`, `SEGM-CORE-001`-`005`, `GEOM-CORE-002`/`003`, `UNDO-CORE-001`/`002`, `VAL-CORE-002`, `UX-CORE-001`-`004`, and `A11Y-CORE-001`/`002`/`004`/`005`.
- Source files/components changed: `CMakeLists.txt`; `src/atlas/ui/main.cpp`; new `src/atlas/ui/main_window.hpp/.cpp`; `src/atlas/ui/canvas_widget.hpp/.cpp`; `src/atlas/domain/project.hpp/.cpp`; `src/atlas/application/command.hpp/.cpp`.
- Tests added/updated: `tests/atlas/ui/canvas_widget_tests.cpp`; new `tests/atlas/ui/road_authoring_workflow_tests.cpp`; `tests/atlas/application/command_tests.cpp`. UI coverage verifies keyboard creation in all primitive modes, snapping toggle, cancel/no-mutation, valid and invalid envelope text, edit/anchor remap, extension/shortening resolutions, station measurement, split/boundary move/merge/reverse/delete, undo/redo, save/open/migration, shortcut focus, accessible names, and baseline/doubled-font Inspector reachability.
- Documentation/comments added/updated: checkpoint J plan retained; this report appended. Added test intent comments. No roadmap or public version promotion was made.
- Persistence/compatibility impact: no schema or record-format change. New packages save as schema 2; schema-1 packages remain schema 1 on ordinary saves. Adding road records to a schema-1 package requires explicit transactional migration. RoadSpline deletion defaults to fail-closed behavior and only removes owned RoadSegments when the user explicitly confirms the compound operation; undo restores both.

Focused Checks:
- Checks/tests run: supported headless configure/build and all six headless GoogleTest executables; supported Qt configure/build and deployment; complete Qt UI suite; focused J workflow tests; architecture governance; `git diff --check`.
- Result: all 188 headless tests passed; all 22 Qt UI tests passed; Qt deployment completed; architecture governance and diff checks passed.

Issues:
- Deviations from Phase 1: added an explicit `RoadDeleteResolution::deleteOwnedSegments` path because the domain correctly rejects deleting a RoadSpline while owned RoadSegments remain; the UI confirms and commits this as one undoable operation. The default command remains fail-closed.
- Unimplemented or partial ledger items: no J command workflow remains unimplemented. Manual visual review of theme contrast, visible focus order, and actual device-pixel-ratio rendering was not performed; the UI suite checks accessible labels/shortcuts and doubled-font Inspector reachability. Overall `R050-006` and the v0.5.0 release ledger remain subject to Phase 3, including the separate H evidence/status.
- Remaining implementation work: none for the J functional slice. Phase 3 must perform visual contrast/focus review and reconcile the complete v0.5.0 ledger before release integration.
- ADR or design decision needed: none. Editor diagnostics still report `std::set/map::contains` and missing Qt include-path errors in `command.cpp`/`canvas_widget.cpp`; the supported C++23 headless and Qt builds succeed. The shorten-dialog UI test emits a host-window geometry warning but passes.

Phase Gate:
- Ready for validation: yes
- Reason: J's desktop road-authoring workflows are routed through the existing command/transaction and persistence contracts, focused accessibility/workflow tests pass, and supported builds plus all module/UI tests pass. This is implementation completion only; Phase 3 visual review and full-version ledger validation remain, and no release readiness is claimed.

---

## Phase 3 — Validate & Fix (Checkpoint J)

Result: completed
Phase: 3 — Validate & Fix
Version: v0.5.0
Checkpoint: v0.5.0-J — Accessible Road Authoring Workspace

Validation:
- Build result: supported `headless-vcpkg` and `windows-qt-sdk` configure/builds passed in the initialized Visual Studio x64 environment; `windeployqt` completed for `atlas.exe`.
- Scope ledger result: `R050-001` through `R050-006` validated from prior checkpoint evidence plus this J end-to-end run. H's previously recorded partial post-change suite evidence is satisfied by the final combined runs; the earlier H report remains unchanged as historical evidence.
- Test/check suites executed: headless CTest (188 tests); Qt build-directory CTest (218 tests); H/J focused transaction, geometry, migration, compatibility, and UI cases; retained-fixture desktop open/save/reopen replay; visual screenshots at 100% and 200% text scale plus `QT_SCALE_FACTOR=2`; contrast and platform-accessibility-interface tests; architecture governance; `git diff --check`.
- Passed: headless CTest 188/188; Qt CTest 218/218; focused J workflow and H regression cases; retained empty-project, minimal-road, and split-road fixture replay; architecture governance and diff check. Visual review confirms visible fitted road/envelope, dashed non-color focus frame, text diagnostic for invalid envelopes, and Inspector reachability at increased scale.
- Failed: no final build, test, fixture, architecture, or diff failures. A Tab-selection regression was caught during the final suite rerun and fixed by restoring the no-pointer generic selection-cycle fallback; both generic and road-overlap cycle tests then passed.
- Not run: external screen-reader sessions and physical high-density monitor hardware tests; Qt accessibility interfaces were queried directly, and runtime/font scaling was exercised at 2×. No separate libFuzzer process was run; checkpoint-G seeded valid/malformed package campaigns and bounded geometry/security cases remain the recorded fuzz/security evidence.
- Determinism/compatibility result: all versioned geometry and deterministic tests pass. Desktop package replay preserves normalized source for the actual retained fixture `fixtures/compatibility/empty-project.atlas` and both `tests/fixtures/compatibility/{minimal-road.json,split-road.json}`. Schema-1 ordinary saves remain schema 1, explicit road-data migration upgrades transactionally, and unknown fields/road records survive existing save/load tests.

Conformance:
- CORE requirements verified: `ARCH-CORE-002`/`003`/`004`; `GEOM-CORE-001`-`005`; `MAP-CORE-001`; `ROAD-CORE-001`-`003`; `SEGM-CORE-001`-`005`; `STAT-CORE-001`-`005`; `XSEC-CORE-001`/`003`/`004`; `FILE-CORE-001`; `SAVE-CORE-005`; `UNDO-CORE-001`/`002`; `VAL-CORE-002`; `UX-CORE-001`-`005`; `A11Y-CORE-001`-`005`.
- Roadmap packets and ship items verified: `R050-001`-`R050-006`; J supplies the previously absent supported UI for create/edit/extend/shorten/reverse/delete/measure/split/boundary-move/merge while preserving the v0.6 lane-native and v0.7 network non-goals.
- Acceptance criteria verified: AC-001 creates one valid full-domain segment with endpoint anchors and derived envelope; AC-002 split/undo restores exact normalized source; AC-003 control-point editing remaps anchors and previews diagnostics; AC-004 reverse behavior preserves stable IDs and is undoable; keyboard, focus, names, text/overlay contrast, and scaling checks pass for J workflows.
- Release gates verified: full packet/compatibility tests pass; split/merge coverage and lineage, reverse equivalence, station properties, bounded invalid-envelope preview, migration/recovery, prior-release regression, UI save/load, and retained fixture replay have executable evidence. Phase 4 version promotion/release packaging was not performed.

Defects:
- Defects found: initial road commit did not fit the canvas to the created road; Inspector content could overflow at 2× text size; selected-road context omitted owner/Map/level/layer; the canvas had no explicit visible focus outline, live coordinate status, or road-overlap Tab cycling; deleting a RoadSpline with owned RoadSegments failed closed without a user resolution; generic Tab cycling regressed while adding road-local hit cycling; Phase 1 named the empty package fixture under the wrong directory.
- Fixes made: fit newly committed roads to the canvas; constrain/wrap Inspector content and use short control-point labels; expose owner context; add dashed canvas focus indication, live coordinate/road-station status, Ctrl+Tab focus traversal, and deterministic overlap cycling with generic fallback; add explicit confirmed atomic road-and-owned-segment deletion while retaining fail-closed default behavior; correct Tab fallback; verify the actual fixture location without rewriting Phase 1.
- Regression coverage added: visible-road framebuffer screenshots, 100%/200%/DPR-2 layout checks, contrast thresholds, Qt accessibility-interface names, UX-CORE-001 selection context, overlap cycling, live coordinate status, Ctrl+Tab traversal, real interior-anchor Move/Delete/Cancel shortening, atomic delete/undo, and desktop replay of retained fixtures.
- Remaining failures/limitations: editor diagnostics still report `std::set/map::contains` and missing `QOpenGLWidget` include-path errors, but the supported C++23 headless and Qt builds succeed and changed UI files report no editor errors. The shorten-dialog UI test emits a host-window geometry warning but passes. Full-window `QWidget::grab()` omits the OpenGL child surface, so the canvas was reviewed separately through `grabFramebuffer()`.
- Stop-ship defects: none found in the completed v0.5.0 ledger validation. External assistive-technology and physical monitor tests remain outside this run; the Qt accessibility API, focus, contrast, and scale checks passed.

Phase Gate:
- Ready for release integration: yes
- Reason: every v0.5.0 work packet, acceptance criterion, compatibility fixture, earlier-release regression, and J accessibility/desktop workflow gate has executable evidence; final headless and Qt CTest trees pass. Phase 4 version promotion and release packaging remain separate and were not performed.

---

## Phase 4 — Release Integration

Result: completed
Phase: 4 — Release Integration
Version: v0.5.0

Release Integration:
- Scope shipped: Atlas v0.5.0 RoadSpline stationing and derived geometry, including accessible Qt desktop creation/editing, station measurement, segment split/boundary move/merge, reverse, delete, preview/commit/cancel, undo/redo, diagnostics, and schema-aware package workflows.
- Final scope-ledger reconciliation: `R050-001`-`R050-006` are validated by prior checkpoint evidence plus the final J UI/compatibility coverage. H's Phase 3 report is preserved as historical evidence; its identified post-change validation gap is covered by the final 188/188 headless and 218/218 Qt CTest runs. No v0.5.0 in-scope row remains partial or blocked.
- Application-version locations updated: CMake project version; vcpkg manifest version string; package generator's Atlas version; README current version/status and capability table; build guide; Roadmap current status and v0.5.0 release state; canonical Markdown spec frontmatter, baseline, current-status text, and adopted release train; AGENTS current project context; CHANGELOG v0.5.0 release notes; this development log.
- Independent compatibility versions changed: none. Package schema remains generation 2; geometry-engine, envelope-algorithm/tolerance, exporter, and plugin-API versions remain unchanged.
- Windows executable signing: certificate subject `Hydrogen Studios, LLC`; thumbprint `3B04F019D7D392F3303F762C725C2E553CF6F3C0`; expires `2028-09-24 22:45:55` local certificate time. SignTool signing succeeded; `signtool verify /pa /v` succeeded with zero warnings/errors; `Get-AuthenticodeSignature` returned `Valid` / `Signature verified.` The certificate is self-signed for development and is not a publicly trusted publisher signature.
- Changelog/release notes updated: v0.5.0 entry added with Added, Changed, Fixed, Testing / Validation, Compatibility / Migration, and Known Limitations sections.
- Documentation/status updated: README, build guide, Roadmap, canonical Markdown spec, AGENTS, and release changelog now identify v0.5.0 and distinguish shipped road authoring from planned lane-native/network capabilities. Per the user's direction, the generated DOCX was left unchanged; the Markdown specification is the canonical representation used for this release. Roadmap `canonical_specification_sha256` matches the Markdown SHA-256 `EB0BB93618DCA38A39B3EB5A33ED4CD36B082EE615937B73B035031AB8DA4D01`.
- Compatibility/migration impact: no persisted schema change. New packages use schema generation 2; schema-1 ordinary saves preserve schema 1; road data requires explicit transactional migration to schema 2. Retained empty-project, minimal-road, and split-road fixtures replay through desktop open/save/reopen without normalized-source drift.
- Known limitations: the local self-signed development certificate does not establish public trust and the executable signature is not timestamped, so long-term signature validity is bounded by that certificate. External screen-reader sessions and physical high-density monitor hardware tests were not run; Qt accessibility interfaces, keyboard/focus flows, text and overlay contrast checks, 2× font sizing, Qt scale factor 2, and framebuffer visual review passed. VS Code IntelliSense continues to report `std::set/map::contains` and Qt include-path diagnostics despite successful supported C++23 builds; no corresponding compiler errors were observed.

Final Verification:
- Final commands/checks run: `cmake --preset headless-vcpkg`; `cmake --build --preset headless-vcpkg`; `ctest --preset headless-vcpkg --output-on-failure`; `cmake --preset windows-qt-sdk`; `cmake --build --preset windows-qt-sdk`; `windeployqt`; `ctest --test-dir build/windows-qt-sdk --output-on-failure`; focused road UI/compatibility tests; architecture governance; version/stale-reference search; manifest JSON parse; canonical spec hash check; `git diff --check`; SignTool and PowerShell Authenticode verification.
- Result: headless CTest 188/188 passed; Qt CTest 218/218 passed; official Qt build/deployment passed; architecture governance and diff checks passed; version and compatibility metadata are consistent.
- Signed executable path and signature verification result: `build/windows-qt-sdk/atlas.exe`; SignTool verification succeeded, PowerShell status is `Valid`, signer subject and thumbprint match the certificate above.
- Release gates confirmed: `R050-001`-`R050-006`; AC-001, AC-003, reverse equivalence, split/merge coverage and lineage, invalid-envelope bounded preview, save/load/migration compatibility, desktop keyboard/accessibility workflows, all retained compatibility fixtures, and earlier-release regressions.
- Release evidence status: Complete. The signed desktop binary, final build outputs, CTest logs, retained fixtures, accessibility/contrast tests, and this chronological development log are available in the repository/workspace. Visual captures were reviewed from temporary output; the report records the findings, but the PNGs are not retained in the repository.
- Stop-ship defects: none found.
- Unvalidated or partial roadmap items: no v0.5.0 in-scope packet remains unvalidated. v0.6.0 lane-native editing and later capabilities remain planned and are not claimed as shipped. The DOCX remains at its previously generated content by explicit user direction; the canonical Markdown spec carries the active release state.

Release Status:
- Release complete: yes
- ADR or design decision needed: none.
- Reason: the v0.5.0 implementation and release ledger are validated, compatibility and migration contracts remain intact, public version/documentation metadata agree, and the final Windows desktop executable is signed and verified. Phase 4 is complete; no additional version promotion or integration work is implied.
