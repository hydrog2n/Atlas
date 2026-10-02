# ADR-0006: Road Curve Primitive Definitions and Continuity

- Status: accepted
- Date: 2026-09-26
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `ROAD-CORE-001`, `STAT-CORE-001`, `GEOM-CORE-001`, `GEOM-CORE-005`, `R050-001`

## Context

v0.5.0 introduces a RoadSpline as a continuous directional reference path composed of stable curve primitives. The term RoadSpline is a product/domain name and does not identify one particular mathematical spline representation. Geometry and station semantics must be independent of view tessellation and must be deterministic for identical source and declared algorithm inputs.

## Decision

A v0.5.0 RoadSpline is an open, piecewise parametric curve made from these primitive types:

- **Polyline primitive**: one straight line segment between two stable control-point IDs and their 2D world positions. A multi-vertex path is an ordered sequence of line primitives, not one polyline primitive.
- **Cubic Bezier primitive**: four stable control-point IDs and positions: start, control1, control2, end. Quadratic and arbitrary-degree Beziers are outside v0.5.0.
- **Fixed-radius primitive**: a circular minor arc defined by start/end control points, radius, and `arcSide` (`left` or `right` of the directed start-to-end chord). Radius must be at least half the chord length. The selected arc bulges to `arcSide`; the circle center is on the opposite side. Major arcs and center-based arc input are outside v0.5.0.

Adjacent primitives have C0 positional continuity only. Their shared endpoint MUST use the same control-point ID and the exact same canonical coordinate values. Tangent discontinuities are allowed and represent explicit corners. Atlas does not solve or require C1/C2 continuity in v0.5.0.

A primitive whose length is at or below `coordinateEpsilon`, whose required coordinates are non-finite, whose fixed-radius definition is impossible, or whose cubic Bezier derivative reaches zero within its parameter domain is structurally invalid and MUST be rejected at commit. Such values may be shown transiently during an interactive preview, but cannot become committed source. Closed curves and cyclic station domains are not supported.

`RoadSpline` is a piecewise parametric curve over heterogeneous primitives. It MUST NOT be assumed to mean a single B-spline, NURBS, or other globally uniform spline representation.

Curve evaluation and measurements use the analytic primitive representation or the tolerance-bounded geometry kernel. Render tessellation is derived presentation data and MUST NOT determine station, length, or semantic measurements.

## Determinism and Versioning

The curve-kernel algorithm set has an explicit version identifier. Any change to primitive interpretation, numeric algorithm, quadrature, frame convention, or deterministic boundary behavior that can change derived output requires a geometry-engine version change and reviewed golden updates. Primitive IDs and control-point IDs are source identity; array positions and generated tessellation indices are not identity.

## Alternatives Considered

- One global cubic B-spline/NURBS representation: rejected for v0.5.0 because the roadmap explicitly calls for heterogeneous polyline, Bezier, and fixed-radius primitives and analytic source retention.
- C1 continuity enforcement: deferred; it adds geometric constraint-solving behavior not required by the v0.5.0 roadmap and would prevent explicit corners.
- Major fixed-radius arcs: deferred to avoid introducing an additional sweep-selection contract before the initial road/station model is validated.

## Consequences

Positive: primitive semantics are explicit, stable IDs are primitive-level, and the source model remains inspectable and independent of render tessellation. The geometry kernel can add later primitive types without changing existing primitive meaning.

Costs and risks: exact shared-joint identity must be maintained by source construction and edits. Cubic Bezier stationary points require deterministic structural validation. Future continuity constraints and closed curves need separate contracts.

## Validation

- Each primitive has deterministic position, derivative/tangent, length, and parameter-domain behavior.
- Invalid coordinates, degenerate lengths, impossible arcs, zero tangent points, and closed RoadSplines are rejected with typed errors.
- Adjacent primitives with mismatched shared endpoint IDs or coordinates are rejected.
- Golden outputs record the curve-kernel/geometry-engine version.

## Persistence and Migration Impact

This ADR defines geometry semantics only; it does not define or change the RoadSpline persistence record layout or schema generation. Stable primitive and control-point IDs must be persisted when the road record family is introduced under ADR-0010.
