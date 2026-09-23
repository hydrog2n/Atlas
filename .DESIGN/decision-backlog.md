# Atlas Decision Backlog

This file tracks unresolved implementation decisions that must remain explicit and reviewable while the project is still in the v0.1.0 foundation phase.

## Open decisions

- GPU canvas backend and rendering abstraction.
- Computational geometry library selection, licensing, and tolerance policy.
- Exact compiler, Qt, CMake, and vcpkg baseline versions for the supported developer environment.
- Canonical JSON normalization library behavior and formatting policy.
- Future physical package container representation beyond the v0.2 directory package.
- Plugin isolation and scripting API boundary before roadmap features enter their release windows.
- CI matrix and packaging strategy for Windows-first validation and later cross-platform coverage.

## Decision status

- Accepted in ADR-0001: C++23, Qt 6 Widgets, CMake, vcpkg manifest mode, GoogleTest, UTF-8 deterministic JSON source records, and a Windows-first implementation baseline.
- Accepted in ADR-0002: the v0.2 directory package, storage abstraction boundary, standalone v0.1 JSON test-only status, and unknown-data preservation contract.
- Deferred: future container representation, GPU canvas backend, geometry library stack, final package-lock policy, and plugin boundary details.

## Review rule

Any change to one of these items must be reflected in the relevant ADR and must not silently redefine canonical source or persistence behavior.
