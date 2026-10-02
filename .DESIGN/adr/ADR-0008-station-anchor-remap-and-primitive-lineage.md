# ADR-0008: StationAnchor Remap Signature and Primitive Lineage

- Status: accepted
- Date: 2026-09-27
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `STAT-CORE-002`, `STAT-CORE-003`, `STAT-CORE-004`, `STAT-CORE-005`, `R050-002`

## Context

RoadSegment boundaries and future road attachments need semantic locations that survive centerline edits. A station number alone cannot preserve intent when primitives change length, split, merge, or disappear. The remap data must distinguish the current semantic location from historical evidence used to recover it.

## Decision

A StationAnchor has this structure:

```text
StationAnchor
    id
    resolvedStation
    affinity
    remapSignature
        normalizedStation
        primitiveId
        primitiveT
        worldPosition
```

`resolvedStation` is the only current authoritative location. The remap signature is evidence, not a competing source of truth.

Affinity behavior is explicit:

- `startLocked`: preserve distance from RoadSpline Start.
- `endLocked`: preserve distance from RoadSpline End.
- `geometryLocked`: follow the same primitive and local parameter when it survives.
- `worldLocked`: project the prior world position onto the edited curve within a configured search distance.
- `normalized`: preserve the fraction of total plan length.

Segment boundaries and network attachments default to `geometryLocked`.

Every primitive-structural edit MUST produce a transaction-local parameter remap table before anchor remapping:

```text
old primitive P
    [0.00, 0.37) -> P1 [0.00, 1.00)
    [0.37, 1.00) -> P2 [0.00, 1.00)
```

A geometry-locked anchor first consults this table. If its primitive survives, its prior local parameter is reused. If neither lineage nor the primitive survives, world-position projection is a fallback only when it produces one candidate within the configured search distance. Zero candidates or multiple equally valid candidates produce an ambiguity result and require explicit resolution before a later command can commit.

This checkpoint implements the data model and deterministic remap planner. The command transaction, interactive shortening preview, and Move/Delete dependent/Cancel resolution are checkpoint D behavior.

## Consequences

Positive: remapping is deterministic for known structural edits, affinity expresses user intent, and ambiguous recovery is visible rather than silently guessed. The redundancy in the signature supports repair without making historical evidence authoritative.

Costs: primitive edits must produce lineage tables, and world-locked fallback requires bounded projection/search behavior. Ambiguity must be represented in previews and cannot be hidden by a nearest-point tie-break.

## Validation

- All five affinities have focused deterministic tests.
- Primitive split lineage maps local parameters deterministically.
- Missing primitive fallback is bounded by search distance.
- Ambiguous and out-of-domain remaps return explicit unresolved results.
- Remap output is stable for identical source, signature, lineage table, curve, and tolerance inputs.

## Persistence and Migration Impact

StationAnchor source records will be persisted with RoadSegment and future attachment records under ADR-0010's schema-generation-2 migration. Raw curve parameters are not persisted as semantic locations; `primitiveT` is remap-signature evidence associated with a stable primitive ID. No package writer or schema change occurs in checkpoint C.
