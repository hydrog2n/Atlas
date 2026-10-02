# ADR-0007: Station Parameterization and Inversion Contract

- Status: accepted
- Date: 2026-09-26
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `STAT-CORE-001`, `GEOM-CORE-001`, `GEOM-CORE-005`, `R050-001`

## Context

v0.5.0 requires plan arc-length stationing in meters and geometry evaluation independent of view tessellation. Editing, segment coverage, anchor remapping, and derived geometry need stable conversions between station and primitive parameter. Numerical inversion must be deterministic, bounded, and hidden behind the geometry-engine interface.

## Decision

The Atlas-owned curve-kernel interface exposes:

```text
evaluateAtStation(s) -> { position, tangent, normal }
stationOfPrimitiveParameter(primitiveId, t) -> s
primitiveParameterAtStation(s) -> { primitiveId, t }
```

Application and domain code MUST NOT implement or persist raw curve-parameter inversion themselves.

Station is 2D plan arc length in meters from the canonical RoadSpline Start. Primitive station length is analytic for line and circular-arc primitives and tolerance-bounded numerical quadrature for cubic Bezier primitives. Three-dimensional travel length, elevation, and view tessellation do not participate.

Cubic Bezier arc length uses deterministic adaptive Simpson integration with absolute error tolerance `stationEpsilon` (1e-4 m) and a maximum recursion depth of 24. Failure to meet tolerance within the cap returns an explicit evaluation error; it MUST NOT silently return an unconverged length.

Station-to-parameter inversion uses a bracketed Newton/bisection hybrid, capped at 64 iterations. Each Newton proposal must remain within the current bracket; otherwise the next step is the deterministic midpoint. Convergence is reached when absolute station residual is at most `stationEpsilon`. If the iteration cap is reached without convergence, return an explicit error. Bisection is the deterministic fallback for zero/near-zero speed and invalid Newton proposals.

Primitive parameter domains are `[0, 1)` for all primitives except the final RoadSpline primitive, whose terminal endpoint is included. At a shared station boundary, resolution selects the earlier primitive at its upper endpoint. A station within `stationEpsilon` of a primitive boundary snaps to that exact boundary. Stations outside `[0, totalLength]` by more than `stationEpsilon` are errors; values within the tolerance of the RoadSpline's outer endpoints snap to the exact endpoint.

Arc-length lookup tables MAY be used only as derived acceleration data. They are never authoritative or persisted. They must be regenerated whenever source geometry, tolerance policy, or curve-kernel version changes. Identical declared source and algorithm inputs must produce equivalent station results and frames.

## Consequences

Positive: station semantics are explicit, independent of rendering, and testable at a narrow geometry boundary. Callers do not depend on a particular numerical method.

Costs and risks: adaptive integration and inversion require explicit error propagation and bounded work. The fixed tolerance/depth/iteration values are part of derived-output determinism and must be versioned if changed.

## Alternatives Considered

- View tessellation distance as station: rejected by `STAT-CORE-001` and `GEOM-CORE-001` because presentation detail would change semantic measurements.
- Unbounded Newton iteration: rejected because pathological geometry could hang or produce nondeterministic failure.
- Persist raw curve parameters as semantic locations: rejected because curve edits can change their meaning; `StationAnchor` is the later semantic-reference mechanism.

## Validation

- Parameter-to-station and station-to-parameter round trips meet `stationEpsilon` for every primitive family and representative parameters, including values near endpoints.
- Station evaluation is monotonic for validated primitives and agrees with known analytic line/arc lengths.
- Bezier integration is deterministic, bounded, and returns an explicit error for non-convergent cases.
- Inversion remains within its bracket, respects the 64-iteration cap, uses deterministic fallback, and resolves shared boundaries to the earlier primitive.
- Out-of-domain requests fail explicitly; near-boundary requests snap exactly as specified.
- Golden tests record the geometry-engine version and declared tolerance policy.

## Persistence and Migration Impact

This ADR does not add persisted station caches or change a package schema. Lookup tables and sampled frames remain derived. Persisted semantic references use station/StationAnchor records when introduced under the v0.5 road source model, never raw numerical parameters.
