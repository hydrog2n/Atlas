# ADR-0002: Canonical Package Representation and Unknown Data

- Status: accepted
- Date: 2026-09-22
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `v0.2.0`, `DATA-CORE-001`, `DATA-CORE-002`, `DATA-CORE-003`, `FILE-CORE-001`, `FILE-CORE-002`, `FILE-CORE-003`, `SAVE-CORE-001`, `SAVE-CORE-002`, `SAVE-CORE-003`, `SAVE-CORE-004`, `SAVE-CORE-005`, `AC-012`, `AC-013`, `AC-014`, `AC-017`

## Context

Atlas v0.2.0 introduces durable project persistence before interactive editing and specialized source records exist. The canonical specification defines the logical package identity, manifest, authoritative source records, referenced assets, exports, and disposable caches, but permits the physical package representation to evolve.

The implementation must preserve Atlas's source-versus-derived boundary and must not create compatibility obligations for an unpublished prototype representation. It must also remain forward-compatible when a newer or optional extension writes data that the current application does not understand.

## Decision

### Logical package model

The logical `.atlas` package model is independent of its physical storage mechanism. Persistence code MUST expose package operations through interfaces or adapters so a future container representation can be introduced without changing the logical package model, domain ownership, or canonical record contracts.

### v0.2 physical representation

The canonical v0.2 `.atlas` representation is a directory package, conventionally named with the `.atlas` extension. The initial layout follows the canonical specification:

```text
project.atlas/
  manifest.json
  definitions.json
  styles.json
  maps/
    <map-id>/
      map.json
      objects.json
      network.json
  prefabs/
  assets/
  exports/
  cache/
```

The directory representation is an implementation choice for v0.2, not a permanent guarantee. Authoritative source files, referenced assets, exports, and disposable caches MUST remain distinguishable. Caches are derived and discardable; they are never the source of truth.

### Standalone v0.1 normalized JSON

The standalone normalized JSON emitted by the v0.1 domain implementation is an internal conformance and test representation. It is not a shipped project format and is not a supported legacy import format for v0.2.

Existing normalized JSON fixtures MUST remain supported for deterministic tests and round-trip conformance. v0.2 MUST NOT infer a general legacy import obligation from those fixtures and MUST NOT claim to import arbitrary standalone v0.1 normalized JSON files.

### Unknown data preservation

Unknown fields at every supported record level MUST survive load/save with unchanged semantic content. Unknown namespaced extension records MUST also survive load/save.

Atlas MUST NOT:

- reinterpret unknown data;
- relocate it to another record or namespace;
- normalize it into a different namespace;
- silently discard it because the current application version does not understand it; or
- treat it as derived data merely because no current feature consumes it.

Unknown data may be represented internally through an opaque preservation structure, provided that load-save behavior preserves its semantic content and the canonical serializer does not claim ownership of its meaning. Destructive cleanup of unknown data, if ever introduced, requires an explicit user action, an explicit report, and a separate compatibility decision.

## Alternatives considered

- **Make v0.1 normalized JSON a supported legacy format:** rejected because it was never shipped as a project package and would create an unnecessary compatibility obligation.
- **Commit permanently to a directory-only package:** rejected because a future container can improve distribution or performance without changing the logical package model.
- **Discard unknown fields or extension records:** rejected because it would make Atlas destructive when optional or newer data is encountered and would prevent safe round trips through older versions.
- **Normalize all unknown data into a core namespace:** rejected because it would reinterpret ownership and change data meaning without authority.

## Consequences

Positive consequences:

- v0.2 packages remain inspectable and easy to diagnose with ordinary filesystem tools.
- Storage implementation can evolve without changing the logical package contract.
- Older Atlas versions can preserve newer or optional data they do not understand.
- Test fixtures remain useful without accidentally becoming a public compatibility promise.
- Persistence maintains the authoritative source versus derived output boundary.

Costs and risks:

- Unknown-data preservation requires record-level capture and careful serializer behavior.
- Directory packages require explicit path normalization, traversal protection, staging, replacement, and checkpoint handling.
- Future container representations need adapters and compatibility tests against the same logical package model.
- Semantic preservation tests must compare normalized meaning rather than only the current typed fields.

## Implementation requirements

1. Define a logical package interface separate from directory storage.
2. Implement the v0.2 directory adapter behind that interface.
3. Keep canonical manifest, schema, application, generator, exporter, and plugin API versions distinct.
4. Preserve unknown fields and namespaced records at every supported record level.
5. Reject path traversal and writes outside the approved package root.
6. Keep standalone v0.1 normalized JSON fixtures in the conformance suite without exposing them as a supported import command.
7. Add round-trip tests for unknown fields, unknown namespaced records, and nested unknown content.
8. Add migration and recovery tests without overwriting the last readable package.

## Persistence and migration impact

This decision establishes the first physical v0.2 package representation and the forward-compatibility preservation contract. It does not change the meaning of existing domain records or make standalone v0.1 normalized JSON a persisted product format.

Any future change to the logical package model, unknown-data preservation guarantee, or schema interpretation requires a new ADR and a migration/compatibility plan before implementation.
