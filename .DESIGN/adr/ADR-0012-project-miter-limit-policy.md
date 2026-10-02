# ADR-0012: Project Miter Limit Policy

- Status: accepted
- Date: 2026-10-01
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `GEOM-CORE-004`, `GEOM-CORE-005`, `R050-005`

## Context

The road geometry specification requires miter joins to use a project miter limit, while round joins remain the default. The current source model did not carry a project-level value, so derived output could otherwise change when an application default changes.

## Decision

- Persist `miterLimitRatio` in the project's namespaced geometry policy.
- The accepted default is 4.0 times the applicable lateral offset (the half-width for a uniform centered cross-section).
- The ratio is dimensionless, finite, and at least 1.0.
- Round remains the default join style. The miter limit affects output only when a road selects a miter join.
- A requested miter beyond the persisted ratio produces an Error diagnostic and an invalid bounded preview that bevels that corner; under ADR-0009 this derived failure does not block structurally valid source commits.
- Envelope generation consumes the project's persisted ratio and must not substitute a future application default.

## Alternatives Considered

- Store a miter limit per RoadSegment: rejected because this is a project geometry policy and can vary independently from segment identity and width.
- Read an application-global miter limit during each derivation: rejected because the same persisted project could produce different geometry after an application default change.

## Consequences

- Project normalized source carries a namespaced geometry policy with the resolved ratio.
- Existing projects without the policy receive the accepted default when loaded and normalized.
- Derived output hashes include the resolved ratio and algorithm/tolerance versions.

## Validation

Tests verify defaulting, validation, normalized round trips, and deterministic miter output using the persisted project ratio.
