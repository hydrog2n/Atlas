# Atlas Decision Backlog

This file tracks unresolved implementation decisions that must remain explicit and reviewable while the project is still in the v0.1.0 foundation phase.

## Open decisions

- Final GPU canvas backend and rendering abstraction beyond the v0.4 prototype; see proposed ADR-0003.
- Final computational geometry library selection, licensing, and tolerance policy; see proposed ADR-0004.
- Reference calibration tolerance and validation semantics; see proposed ADR-0005.
- Exact compiler, Qt, CMake, and vcpkg baseline versions for the supported developer environment.
- Canonical JSON normalization library behavior and formatting policy.
- Future physical package container representation beyond the v0.2 directory package.
- Plugin isolation and scripting API boundary before roadmap features enter their release windows.
- CI matrix and packaging strategy for Windows-first validation and later cross-platform coverage.

## Decision status

- Accepted in ADR-0001: C++23, Qt 6 Widgets, CMake, vcpkg manifest mode, GoogleTest, UTF-8 deterministic JSON source records, and a Windows-first implementation baseline.
- Accepted in ADR-0002: the v0.2 directory package, storage abstraction boundary, standalone v0.1 JSON test-only status, and unknown-data preservation contract.
- Accepted for v0.4 implementation: QOpenGLWidget behind an Atlas-owned renderer abstraction (ADR-0003), an Atlas-owned geometry/tolerance interface with internal and Boost.Geometry evaluation (ADR-0004), and rejection of degenerate reference calibration below a named tolerance (ADR-0005).
- Deferred: final renderer backend after prototype evidence, final geometry library stack and package-lock policy, future container representation, and plugin boundary details.

## Review rule

Any change to one of these items must be reflected in the relevant ADR and must not silently redefine canonical source or persistence behavior.
