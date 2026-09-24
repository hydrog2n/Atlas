# ADR-0004: Geometry and Tolerance Evaluation Boundary

- Status: accepted
- Date: 2026-09-23
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `COORD-CORE-001`, `COORD-CORE-002`, `COORD-CORE-003`, `COORD-CORE-004`, `GEOM-CORE-001`, `R040-002`

## Context

v0.4.0 requires deterministic snapping and hit testing for grid, endpoints, midpoints, tangents, perpendiculars, angles, and distances. ADR-0001 leaves computational geometry libraries, tolerance policy, and deterministic-version policy open. A library choice must not leak third-party types into authoritative or application interfaces.

## Decision

Define an Atlas-owned geometry and tolerance interface before implementing snapping. Prototype both a minimal internal implementation and Boost.Geometry behind that interface. Benchmark representative v0.4 cases: large world coordinates, near-tolerance candidates, degenerate inputs, overlapping candidates, and dense candidate sets.

The evaluation must compare correctness, deterministic ordering, performance, licensing, dependency/build impact, and suitability for later road geometry. Boost.Geometry types must not appear in domain, application, persistence, or public Atlas geometry interfaces.

Do not make the final long-term geometry-library selection in this ADR. Record the evidence-based selection, algorithm set, tolerance policy, and dependency/license decision in a follow-up ADR before v0.5 road geometry or broader library adoption.

## Consequences

The v0.4 implementation can proceed behind a stable Atlas contract while preserving the option to change the underlying library. Benchmark work becomes release evidence and an input to the follow-up ADR. The project temporarily carries two evaluation paths and must maintain deterministic behavior across them.

## Validation

- Candidate acquisition and constraint results are deterministic for fixed source and tolerance inputs.
- Large-coordinate, degenerate, near-tolerance, and dense-candidate cases have retained tests.
- Candidate priority and tie-breaking are explicit and stable across runs.
- Benchmark results include hardware, dataset, algorithm, tolerance, median, 95th percentile, and build configuration.
- License and transitive dependency review is recorded before adopting Boost.Geometry.

## Persistence and Migration Impact

No immediate schema change. Tolerance policy and algorithm/version identifiers must be explicit if they affect persisted derived hashes or future package compatibility. Any such persisted meaning requires the follow-up ADR and migration/compatibility analysis.
