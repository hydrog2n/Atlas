# ADR-0003: Canvas Renderer Prototype and Boundary

- Status: accepted
- Date: 2026-09-23
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `ARCH-CORE-001`, `ARCH-CORE-007`, `ARCH-CORE-008`, `COORD-CORE-001`, `A11Y-CORE-001`, `R040-001`

## Context

Atlas v0.4.0 needs an interactive real-scale canvas. ADR-0001 establishes Qt 6 Widgets and requires GPU-backed derived rendering, but intentionally leaves the graphics backend open. The canvas must render large double-precision world coordinates without moving pixel or tessellation values into authoritative source records.

## Decision

Use `QOpenGLWidget` for the v0.4.0 canvas prototype behind an Atlas-owned renderer abstraction. The abstraction owns camera transforms, derived render layers, tessellation inputs, grid/ruler overlays, picking inputs, and presentation resources. Domain and application modules must not depend on Qt, OpenGL types, graphics resources, pixel coordinates, or renderer implementations.

World coordinates remain authoritative double-precision values. Camera-relative coordinates, viewport pixels, tessellation, grid lines, selection overlays, and render caches are derived. World-to-camera and inverse camera transforms must be explicit and tested. Zoom, pan, display-unit changes, and high-DPI scaling must never mutate source geometry.

The abstraction must permit a future Qt RHI or other backend without changing domain records, application commands, persistence meaning, stable IDs, or selection semantics.

## Alternatives Considered

- Qt RHI/custom backend: more future-facing, but not selected for the first prototype because the repository needs a focused backend proof first.
- QPainter/software rasterization: useful for a correctness prototype, but does not satisfy the intended GPU-backed canvas baseline.
- Qt Quick/QML: inconsistent with ADR-0001's initial Qt Widgets decision.

## Consequences

Positive: establishes the v0.4 rendering path, preserves source/derived ownership, and provides a concrete basis for camera, picking, accessibility overlays, and performance evidence.

Costs: introduces OpenGL context/resource lifecycle complexity and requires high-DPI, device-loss, and deterministic presentation tests. The backend remains a prototype until performance and portability evidence are reviewed.

## Validation

- World/camera/inverse transform round trips at ordinary and very large coordinates.
- Zoom, pan, units, and high-DPI changes preserve normalized source bytes.
- Grid, rulers, origin, selection, and snapping overlays render without changing source state.
- Picking and stable object IDs remain deterministic across viewport changes.
- Representative 10,000-object viewport performance is measured with hardware, dataset, cache state, median, and 95th percentile.

## Persistence and Migration Impact

None. Camera state, pixels, tessellation, render caches, and viewport settings are derived or presentation data and are not added to authoritative package records by this decision.
