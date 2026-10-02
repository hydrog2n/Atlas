# Atlas v0.5.0 Development Log Critique

## Review Scope

This critique reviews `LOG_0.5.0.md` against the Atlas v0.5.0 roadmap and the architectural expectations established by the Atlas Master Design Specification Revision 3.0.

## Overall Assessment

The v0.5.0 planning is architecturally strong, but the current document is not yet much of an implementation log. Most of its substantive content is Phase 1 planning.

That distinction matters because the document calls itself a **Development Log**, while it currently functions more like a release-specific implementation plan.

The planning correctly identifies the major architectural hazards in v0.5.0 and, importantly, stops implementation where unresolved decisions could otherwise create long-term architectural debt.

---

## What Is Strong

The scope discipline is very good.

The log correctly identifies v0.5.0 as the point where Atlas establishes the authoritative road centerline and stationing system **before** lane topology and network connectivity.

Particularly strong architectural decisions include:

- Stable primitive and control-point IDs from the beginning.
- Two-dimensional plan arc-length stationing rather than station values derived from rendering or tessellation.
- `StationAnchor` carrying multiple pieces of remapping information rather than only a station number.
- Explicit ambiguity handling rather than silently guessing where an anchor should move.
- Half-open `RoadSegment` interval coverage.
- Derived envelopes remaining disposable and reproducible.
- Road reversal being treated as a compound semantic operation rather than simply reversing a list of points.
- Range-scoped invalidation rather than rebuilding an entire road or map.
- A substantial test matrix covering golden geometry, property tests, persistence, transactions, fuzzing, determinism, and failure cases.

The **Phase Gate** is also a particularly good decision. The log correctly refuses to begin the complete implementation until the geometry-engine ADR and road persistence boundary are resolved.

That is exactly the kind of unresolved boundary where an implementation agent could otherwise produce a large amount of technically competent code that later becomes architectural debt.

---

# 1. v0.5.0 Is Still a Very Large Implementation Slice

The largest concern is the amount of foundational geometry behavior being introduced simultaneously.

The release introduces, among other things:

1. Three curve primitive families.
2. Arc-length evaluation.
3. Moving frames.
4. Station conversion.
5. Anchor remapping.
6. Multiple anchor affinity modes.
7. Road editing commands.
8. Reversal semantics.
9. Segment coverage.
10. Segment splitting.
11. Segment merging.
12. Lineage.
13. Cross-section placeholder behavior.
14. Offset and envelope generation.
15. Offset failure diagnostics.
16. Persistence behavior.
17. Golden geometry testing.
18. Fuzz and property testing.
19. Compatibility behavior.

This is not necessarily incorrect. The roadmap intentionally defines v0.5.0 as a complete road-geometry vertical slice.

However, implementation should be subdivided much more aggressively internally.

## Recommended Internal Checkpoints

These do not need to become public versions.

### 0.5-A — Curve Kernel

Implement:

- Polyline primitives.
- Bezier primitives.
- Circular/fixed-radius primitives.
- Curve evaluation.
- Length evaluation.
- Station-to-parameter conversion.
- Tangent and normal evaluation.
- Deterministic sampling.

### 0.5-B — Road Source Model

Implement:

- `RoadSpline`.
- Stable primitive IDs.
- Stable control-point IDs.
- Primitive ownership.
- Canonical direction.
- Full-domain initial `RoadSegment`.

### 0.5-C — StationAnchor

Implement:

- Anchor representation.
- Affinity modes.
- Remapping signatures.
- Deterministic remapping.
- Ambiguity detection and preview.

### 0.5-D — Road Editing

Implement:

- Create.
- Control-point edit.
- Extend.
- Shorten.
- Reverse.
- Delete.
- Transaction behavior.

### 0.5-E — Segmentation

Implement:

- Complete interval coverage.
- Split.
- Boundary movement.
- Merge.
- Segment lineage.
- Reference remapping.

### 0.5-F — Envelope Geometry

Implement:

- Uniform cross-section placeholder.
- Offset generation.
- Join behavior.
- Envelope generation.
- Failure diagnostics.
- Bounded invalid previews.

### 0.5-G — Conformance

Complete:

- Persistence.
- Golden geometry.
- Property testing.
- Fuzz testing.
- Compatibility testing.
- Determinism testing.
- Regression testing.

The existing `R050-001` through `R050-006` packets already approximate this subdivision, but each packet should have a clear mini-gate before dependent packets begin.

---

# 2. Curve Terminology Needs to Be More Precise

The phrase:

> Open RoadSpline using polyline, Bezier, and fixed-radius primitives

is still mathematically underspecified.

Before implementation, Atlas should explicitly define what each primitive means.

Questions that should be answered include:

- Is a polyline primitive one line segment or a polyline containing multiple vertices?
- Is Bezier support quadratic, cubic, arbitrary-degree, or some subset?
- Is a fixed-radius primitive specifically a circular arc?
- How is a circular arc represented?
- Is it defined by center/radius/angles?
- Is it defined by three points?
- Is it constructed from tangent points and a radius?
- What continuity is expected between primitives?
- Is only C0 positional continuity required?
- Is C1 tangent continuity required in some situations?
- Is C2 continuity ever relevant?
- Can zero-length primitives temporarily exist during an interactive preview?
- What happens to the moving frame at a discontinuous tangent?
- What is the exact behavior at a cusp?
- What is the canonical representation of a full or nearly full circular arc?

There is also a terminology issue worth documenting explicitly.

`RoadSpline` is a good product/domain name, but mathematically the object appears to be a **piecewise parametric curve composed of potentially heterogeneous primitives**. It is not necessarily a mathematical spline.

That should be stated explicitly so future contributors do not assume that `RoadSpline` implies a single cubic B-spline, NURBS representation, or similar mathematical structure.

---

# 3. Station-to-Parameter Inversion Should Be a First-Class Contract

The current planning discusses:

- analytic length,
- frames,
- deterministic sampling,
- station evaluation.

However, **station-to-curve-parameter inversion** deserves to be elevated into an explicit geometry-engine contract.

For a curve:

\[
s(t) = \int_0^t \|C'(u)\|\,du
\]

Atlas frequently needs to compute the inverse:

\[
t = s^{-1}(s)
\]

This operation will underpin a large amount of later functionality.

The implementation contract should explicitly define:

- convergence tolerance,
- iteration limits,
- bracketing behavior,
- monotonicity assumptions,
- degenerate-curve behavior,
- endpoint snapping behavior,
- deterministic fallback behavior,
- failure diagnostics,
- whether derived arc-length lookup tables are permitted,
- how lookup tables are versioned or invalidated,
- whether analytic inversion is used when available.

A conceptual geometry interface should expose operations such as:

```text
evaluateAtStation(s)
stationOfPrimitiveParameter(primitiveId, t)
primitiveParameterAtStation(s)
```

Application code should not need to know how numerical inversion is implemented.

---

# 4. StationAnchor Is Redundant in a Useful Way

The proposed `StationAnchor` contains information such as:

- last station,
- normalized fallback,
- primitive affinity,
- local parameter,
- last world position,
- edit affinity.

At first glance this appears redundant.

However, that redundancy is useful because these values are not necessarily competing authoritative locations. Together they form a **remapping signature**.

The distinction should be reflected directly in the data model.

A clearer conceptual representation would be:

```text
StationAnchor
    id
    resolvedStation
    affinity
    remapSignature
        normalizedStation
        primitiveId
        primitiveT
        worldPosition
```

Here:

- `resolvedStation` represents the current semantic location.
- `affinity` represents the user's intended edit behavior.
- `remapSignature` contains historical/geometric evidence used to recover the semantic location after edits.

This makes it harder for later code to incorrectly treat all stored location-related values as equally authoritative.

---

# 5. Geometry-Locked Anchors Need Primitive Remapping Rules

A significant missing case is what happens when the curve primitive referenced by a geometry-locked anchor is itself transformed structurally.

For example:

1. An anchor is geometry-locked to primitive `P`.
2. `P` is split into `P1` and `P2`.
3. `P` no longer exists.

Without another rule, the system may be forced to fall back to geometric nearest-point matching even though the edit itself knows exactly how the old primitive maps to the new primitives.

Atlas should therefore introduce either:

- primitive lineage, or
- a transaction-local primitive remap table.

For example:

```text
old primitive P
    [0.00, 0.37] -> P1 [0.00, 1.00]
    [0.37, 1.00] -> P2 [0.00, 1.00]
```

An anchor previously located at:

```text
P, t = 0.61
```

can then deterministically remap to approximately:

```text
P2, t = 0.381
```

without requiring a nearest-point search.

This should be designed before implementation because primitive structural edits will become increasingly important once later systems depend on `StationAnchor`.

---

# 6. Atlas Needs Formal Definitions of Equivalence

The log uses terms such as:

- equivalence-checked merge,
- equivalent normalized geometry,
- reverse-twice equivalence.

These are good requirements, but **equivalence should be formally defined**.

At minimum, Atlas should distinguish three forms.

## Exact Identity

The same stable IDs and exactly equal canonical values.

## Normalized Source Equivalence

Differences in irrelevant serialization ordering or formatting are ignored, but canonical source meaning and identity remain equivalent.

## Geometric Equivalence

Geometry represents the same physical result within named tolerance policies even if the source representation differs.

This distinction matters especially for the reverse-twice requirement.

The desired invariant should likely be stronger than:

```text
reverse(reverse(R)) ~= geometrically equivalent R
```

It should instead approach:

```text
reverse(reverse(R)) == normalized-source-equivalent R
```

with stable IDs preserved or restored according to the explicit reversal contract.

Otherwise an implementation could satisfy a geometric test while unnecessarily replacing primitive IDs or altering source representation.

---

# 7. Offset Failure Policy Needs a Product-Level Decision

The log states that invalid offsets should block commit of required derived output while preserving a bounded preview and the user's source-edit choice.

This is sensible, but it exposes an important architectural question.

Atlas deliberately separates:

- authoritative source, and
- disposable derived geometry.

Consider a mathematically valid `RoadSpline` with a very tight curve.

The centerline itself may be valid, while a 12-meter road envelope cannot be generated without a cusp, inversion, or self-intersection.

Should the source edit itself be rejected?

There are two defensible models.

## Model A — Envelope Validity Is Required for Source Validity

If the required road envelope cannot be derived, the RoadSpline edit cannot commit.

Advantages:

- Committed roads always have valid required derived representations.
- Downstream tools can assume the envelope exists.

Disadvantages:

- The editor may prevent users from temporarily creating imperfect geometry while working toward a valid design.
- Derived geometry effectively constrains authoritative source validity.

## Model B — Valid Source May Have Invalid Derived Geometry

The RoadSpline edit can commit if the source representation itself remains structurally valid.

Envelope generation may then fail and produce an Error diagnostic.

Dependent operations such as export may remain blocked until the geometry is repaired.

Advantages:

- Better iterative authoring behavior.
- Maintains a stronger conceptual distinction between source and derived data.
- Users can author their way out of temporarily invalid geometry.

Disadvantages:

- More systems must tolerate source objects with unavailable derived representations.

A useful distinction would be:

```text
INVALID SOURCE
    cannot commit

VALID SOURCE / INVALID DERIVATION
    may commit
    Error diagnostic
    dependent operations may be blocked
```

Examples of source-level failures that should remain uncommittable include:

- NaN coordinates,
- corrupted ownership,
- invalid references,
- impossible primitive definitions,
- structurally invalid station domains.

Examples of potentially derivation-level failures include:

- requested offset exceeds local curvature capability,
- envelope self-intersection,
- miter-limit failure,
- local offset inversion.

This question should be explicitly resolved before the envelope engine becomes authoritative behavior.

---

# 8. The Road Persistence Boundary Must Be Resolved Before Implementation

The log correctly identifies the unresolved decision:

> confirm whether road records remain optional schema-version-1 MapObject extensions or introduce a dedicated authoritative road record family before persistence implementation.

This is one of the most important unresolved questions in the current plan.

`RoadSpline`, `RoadSegment`, and eventually road-specific source structures are central Atlas domain objects.

Unless the existing persistence architecture provides a particularly strong typed-extension system, these should probably become explicit first-class authoritative record families.

Atlas should not distort its domain model merely to avoid incrementing a schema version.

Schema versions exist specifically so that persisted models can evolve deliberately.

The preferred conceptual direction is therefore:

```text
Map
    MapObjects
    RoadSplines
    RoadSegments
    NetworkObjects
```

or an equivalent first-class typed record architecture, rather than treating roads as loosely typed generic-object payloads solely for compatibility convenience.

The exact layout should still follow the existing ownership architecture and persistence ADRs.

---

# 9. Clarify Schema-Version Wording

The development log states that the package schema remains version `1`.

Meanwhile, the repository README identifies the public application as still being in the foundational release train and explicitly states that project-package persistence is not part of the current release.

These facts may be completely compatible if schema generation `1` already exists internally.

However, the wording could be clearer.

Instead of:

> current package schema remains version `1`

prefer wording such as:

> Current implemented persistence schema generation: `1`.

This reinforces Atlas's intentional separation between:

- application version,
- project schema version,
- geometry-engine version,
- exporter version,
- plugin API version,
- specification revision.

---

# 10. LOG_0.5.0.md Should Become More Chronological

Once implementation begins, the development log should stop functioning primarily as another roadmap.

The three document types should have clearly separated responsibilities:

## Specification

Defines **what must be true**.

## Roadmap

Defines **what should be implemented and in what sequence**.

## Development Log

Records **what actually happened**.

The current Phase 1 planning section should remain because it provides useful historical context.

After that, however, the file should append chronological implementation records.

For example:

```text
## Phase 2 — Architecture Decisions

### 2026-09-24 — Geometry engine ADR accepted

Decision:
Files:
Tests:
Evidence:
Open risks:

### 2026-09-25 — R050-001 started

Commit:
Files touched:
Tests introduced:
Unexpected findings:
Open questions:

### 2026-09-26 — R050-001 completed

Result:
Requirements:
Evidence:
Benchmarks:
Known limitations:
Gate status:
```

This would make the file significantly more useful months or years later when trying to understand why a particular implementation decision was made.

It would also reduce duplication between `ROADMAP.md` and `LOG_0.5.0.md`.

---

# Recommended Decisions Before R050-001 Begins

Before implementation of the road geometry system begins, resolve the following.

## 1. Exact Curve Primitive Definitions

Specify:

- line/polyline representation,
- supported Bezier degree,
- circular/fixed-radius arc representation,
- continuity rules,
- degenerate-preview behavior,
- tangent/frame behavior at joins and cusps.

## 2. Formal Station/Parameter Contract

Specify:

- station-to-parameter inversion,
- parameter-to-station conversion,
- numerical tolerances,
- deterministic convergence,
- lookup-table policy,
- endpoint behavior,
- failure behavior.

## 3. Primitive Lineage and Remapping

Define how stable primitive references survive:

- split,
- merge,
- replacement,
- reverse,
- deletion,
- reconstruction.

Prefer deterministic edit-produced remap information over nearest-point guessing whenever possible.

## 4. Source Validity Versus Derived Validity

Explicitly decide whether a structurally valid `RoadSpline` may commit when its required envelope cannot currently be derived.

Distinguish:

```text
invalid authoritative source
```

from:

```text
valid authoritative source with invalid derived output
```

## 5. Road Persistence Record Boundary

Decide whether road source records are:

- first-class authoritative persisted record families, or
- typed extensions of generic MapObjects.

Do not choose the latter solely to preserve schema version `1`.

---

# Final Assessment

The Phase 1 analysis is strong.

More importantly, it correctly identifies the places where implementation should stop rather than guess:

- geometry-engine policy,
- road persistence ownership and record boundaries.

The current architecture is not suffering from a fundamental conceptual problem. The primary risk is that implementation could begin while several small-looking geometry semantics remain implicit.

Those semantics include:

- what exactly a curve primitive is,
- how station inversion works,
- how primitive identity survives structural edits,
- what equivalence means,
- whether derived-geometry failure invalidates authoritative source.

These questions are inexpensive to answer now.

They become substantially more expensive after:

- v0.6.0 lane cross-sections depend on RoadSpline geometry,
- v0.7.0 network connectivity depends on stable lane and road references,
- v1.1.0 RoadTransitions depend on stable station intervals,
- v1.2.0 elevation profiles depend on plan station,
- later procedural systems bind objects to road station intervals.

The recommended course is therefore **not to redesign v0.5.0**, but to tighten these contracts before implementation begins and to enforce packet-level implementation gates within the release.

Once those decisions are made, the v0.5.0 architecture is in strong shape for implementation.
