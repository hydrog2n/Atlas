# ADR-0010: Road Persistence Record Architecture

- Status: accepted
- Date: 2026-09-27
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `MAP-CORE-001`, `ARCH-CORE-002`, `ARCH-CORE-003`, `ARCH-CORE-004`, `SAVE-CORE-005`, `R050-001`, `R050-006`

## Context

v0.5.0 introduces RoadSpline and RoadSegment as authoritative source records. They have ownership, station, primitive, segment, and lineage semantics that do not fit safely as opaque generic MapObject payloads. The current implemented persistence schema generation is 1 and generic MapObjects already exist. The road source model must preserve stable identity and permit an additive migration without silently changing earlier package meaning.

## Decision

RoadSpline and RoadSegment are first-class authoritative record families stored in dedicated top-level project arrays, alongside generic MapObjects. They are not typed extensions of MapObject.

The road record architecture is introduced under persistence schema generation 2. Generation 2 is an independent compatibility dimension from the Atlas application version. Existing generation-1 packages remain readable as projects with empty road arrays; generation-1 data is not reinterpreted or rewritten merely by loading it. A later checkpoint must provide a transactional generation-1-to-2 migration with recoverable backup, readable report, and last-known-good preservation under `SAVE-CORE-005` before road records are persisted.

For checkpoint B, the domain source model may construct and normalize road records without changing package read/write behavior. Persistence wiring, migration, fixture creation, and schema-version validation belong to checkpoint G (`R050-006`).

RoadSpline owns its authoritative identity, owning `mapId`, ordered curve primitive source records, canonical Start-to-End direction, style/metadata extension data, and references to its RoadSegments. Station domain is derived from the primitives and is not an independent source of truth. RoadSegment owns its stable identity, parent RoadSpline ID, StationAnchor boundary IDs, placeholder cross-section state, and lineage metadata.

Every RoadSpline belongs to exactly one Map. A RoadSpline cannot span coordinate contexts. RoadSegments do not own independent centerline geometry. Collection order is presentation/serialization order only and never identity.

Unknown fields and namespaced extension content in road records must be preserved by the eventual persistence adapter under ADR-0002. Raw array indexes and raw curve parameters must not be persisted as semantic references.

## Alternatives Considered

- Typed generic MapObject extensions: rejected because road records have dedicated ownership, station, segment coverage, and future migration semantics; treating them as opaque geometry would weaken domain invariants.
- Immediate in-place mutation of schema generation 1: rejected because it would silently change persisted meaning and remove the migration boundary.
- Persist derived station caches or envelopes as authoritative road fields: rejected by `ARCH-CORE-008`; those products remain rebuildable derived data.

## Consequences

Positive: road ownership and identity are explicit, future lane/network records have a stable parent boundary, and schema evolution is additive and reviewable. The v0.4 generic-object contract remains unchanged.

Costs: checkpoint G must implement a schema generation migration, backup/report behavior, road fixtures, and unknown-field preservation. The package manifest must carry independent schema and application/generator version information.

## Validation

- Domain construction rejects empty or duplicate road IDs and invalid Map ownership.
- RoadSpline normalization is deterministic and does not persist derived station length as source meaning.
- RoadSegment references a valid parent RoadSpline within the same owning Map when a complete project model is available.
- Existing v0.4 packages continue to normalize and load with empty road arrays before migration.
- Checkpoint G validates generation-1-to-2 migration, failure rollback, fixtures, and normalized round trips.

## Persistence and Migration Impact

Schema generation 2 is required when road arrays become persisted. No package schema is changed by checkpoint B alone. Checkpoint G must add the migration, backup, report, compatibility fixtures, manifest/version handling, and failure-injection tests before the v0.5 release can proceed to integration.
