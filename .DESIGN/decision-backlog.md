# Atlas Decision Backlog

This file tracks unresolved implementation decisions that must remain explicit and reviewable while the project is still in the v0.1.0 foundation phase.

## Open decisions

- Final GPU canvas backend beyond the v0.4 prototype; see accepted ADR-0003.
- Final computational geometry library selection and licensing; see accepted ADR-0004 and follow-up geometry-engine decision before broad road-geometry adoption.
- Final computational geometry tolerance policy for road evaluation; see accepted ADR-0004 and ADR-0007.
- Reference calibration tolerance and validation semantics; see accepted ADR-0005.
- StationAnchor remap-signature and primitive-lineage policy; ADR-0008 is required before checkpoint C of v0.5.0.
- First-class road record persistence and schema generation 1-to-2 migration; ADR-0010 is required before v0.5.0 persistence implementation.
- Exact compiler, Qt, CMake, and vcpkg baseline versions for the supported developer environment.
- Canonical JSON normalization library behavior and formatting policy.
- Future physical package container representation beyond the v0.2 directory package.
- Plugin isolation and scripting API boundary before roadmap features enter their release windows.
- CI matrix and packaging strategy for Windows-first validation and later cross-platform coverage.
## Decision status

- Accepted in ADR-0001: C++23, Qt 6 Widgets, CMake, vcpkg manifest mode, GoogleTest, UTF-8 deterministic JSON source records, and a Windows-first implementation baseline.
- Accepted in ADR-0002: the v0.2 directory package, storage abstraction boundary, standalone v0.1 JSON test-only status, and unknown-data preservation contract.
- Accepted for v0.4 implementation: QOpenGLWidget behind an Atlas-owned renderer abstraction (ADR-0003), an Atlas-owned geometry/tolerance interface with internal and Boost.Geometry evaluation (ADR-0004), and rejection of degenerate reference calibration below a named tolerance (ADR-0005).
- Accepted for v0.5.0 checkpoint A: heterogeneous open RoadSpline primitive definitions and C0 continuity only (ADR-0006); plan-station parameterization, adaptive integration, bounded inversion, and endpoint semantics (ADR-0007).
- Accepted for v0.5.0 checkpoint B: first-class RoadSpline and RoadSegment record families with a schema-generation-2 migration boundary (ADR-0010).
- Accepted for v0.5.0 checkpoint C: StationAnchor remap signature and primitive lineage (ADR-0008).
- Accepted for v0.5.0 checkpoint F: valid-source versus invalid-derived-envelope commit policy (ADR-0009).
- Accepted for v0.5.0 checkpoint F: centered 8.0 m uniform-placeholder width stored in RoadSegment source (ADR-0011).
- Accepted for v0.5.0 checkpoint F: persisted project-level miter limit ratio, default 4.0 (ADR-0012).
- Deferred: final renderer backend after prototype evidence, final geometry library stack and package-lock policy, future container representation, and plugin boundary details.

## Review rule

Any change to one of these items must be reflected in the relevant ADR and must not silently redefine canonical source or persistence behavior.
