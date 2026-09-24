# ADR-0005: Reference Calibration Validation Semantics

- Status: accepted
- Date: 2026-09-23
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `DRAFT-CORE-001`, `DRAFT-CORE-002`, `VAL-CORE-001`, `VAL-CORE-002`, `R040-003`

## Context

v0.4.0 supports non-authoritative reference images, calibration, and traced objects. Calibration with coincident or nearly coincident points cannot produce a meaningful scale or transform. The specification requires calibration to preview effects and prohibits silently rescaling source geometry.

## Decision

Define a named calibration tolerance. If calibration points are coincident or separated by less than that tolerance, the preview remains visible but produces a structural Error diagnostic and no transform. Commit is rejected until the invalid calibration is corrected or cancelled. No source geometry is rescaled as a side effect of preview or failed commit.

Use deterministic tie-breaking only for multiple valid calibration candidates; do not use point-ID ordering to resolve mathematically degenerate calibration input.

Reference images remain non-authoritative. A separate explicit import command is required before traced or detected geometry becomes authoritative source objects.

## Consequences

Calibration behavior is deterministic, explainable, and compatible with the command validation boundary. The tolerance becomes a named compatibility-sensitive policy requiring tests and documentation. Reference placement, opacity, locking, and calibration preview remain derived/presentation behavior until an explicit command commits supported source changes.

## Validation

- Coincident and below-tolerance inputs show an Error diagnostic in preview and cannot commit.
- Valid calibration previews the affected traced-object effect without mutating authoritative source until commit.
- Cancel leaves source, history, selection, and caches unchanged.
- Explicit import creates authoritative objects through the normal command path.
- Save/load and undo/redo preserve reference identity and calibration command semantics.

## Persistence and Migration Impact

Reference-image metadata and calibration records must remain optional and forward-compatible with schema version 1 where possible. If the named tolerance or calibration record changes persisted meaning, add migration/backup analysis before changing schema semantics.
