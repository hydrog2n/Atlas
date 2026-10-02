# ADR-0011: Road Uniform Placeholder Width

- Status: accepted
- Date: 2026-10-01
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `R050-005`, `GEOM-CORE-001`-`GEOM-CORE-005`, `AC-001`

## Context

Checkpoint F requires a uniform placeholder cross-section from which the initial RoadSpline envelope can be derived. The existing placeholder carried no physical width, so envelope generation could not produce a meaningful footprint. The application default may change over time, but authoritative road meaning must not silently change with it.

## Decision

- The v0.5.0 default uniform placeholder has a total width of 8.0 m, centered on the RoadSpline: 4.0 m on each side of the canonical centerline.
- This value is a configurable creation default/preset, not a CORE invariant and not an implicit lane or topology definition.
- The actual selected total width in meters is stored in each RoadSegment's authoritative `crossSectionState` as `totalWidthMeters`. Envelope generation reads that persisted value and never substitutes the current application default for a missing value.
- Split copies the width to both children. A later change to the application default affects only newly created roads; existing RoadSegments retain their stored width.

## Alternatives Considered

- Infer width from a lane count or lane defaults: rejected because v0.5.0 does not define implicit lanes or lane-native cross-section editing.
- Read the current application default whenever deriving an envelope: rejected because changing a default would alter existing roads' physical meaning without an edit.
- Leave the placeholder widthless and defer envelope generation: rejected for v0.5.0 because checkpoint F requires the initial cross-section to support a derived envelope.

## Consequences

- New RoadSpline creation must materialize the selected width in the initial RoadSegment source record.
- Source validation rejects non-finite or non-positive widths. Missing width is not repaired by consulting a mutable application default.
- Envelope generation treats the width as total physical width and offsets half of it to each side.
- RoadSegment normalized-source round trips and split/undo tests must preserve the explicit width.

## Validation

Checkpoint F tests the default and configurable creation widths, verifies that the actual width is present in normalized source, checks centered envelope output, and confirms identical persisted inputs produce identical derived hashes.
