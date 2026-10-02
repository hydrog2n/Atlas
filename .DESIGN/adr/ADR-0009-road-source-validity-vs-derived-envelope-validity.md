# ADR-0009: Road Source Validity vs. Derived Envelope Validity

- Status: accepted
- Date: 2026-10-01
- Decision owners: Atlas project maintainers
- Supersedes: none
- Affects: `GEOM-CORE-001`-`GEOM-CORE-005`, `R050-005`, `AC-001`

## Context

A RoadSpline can be structurally valid while a requested cross-section offset cannot produce a valid road envelope. Cusps, insufficient local radius, self-intersection, and orientation inversion are derived-geometry failures; they do not necessarily invalidate the authoritative centerline source. Atlas must distinguish such failures from malformed source data and define whether an edit may commit when envelope generation fails.

The v0.5.0 Phase 1 ledger records the user-accepted Model B decision: valid source may commit while invalid derived geometry is diagnosed and previewed. This ADR formalizes that existing decision and introduces no new product policy.

## Decision

Atlas uses Model B: source validity and derived-envelope validity are separate.

- A structurally valid RoadSpline source edit may commit even if its required envelope cannot be generated.
- Envelope generation failure produces an `Error` diagnostic and a bounded invalid preview. It must never be presented as valid or silently committed as valid derived geometry.
- Operations that require a valid envelope remain blocked until the envelope can be rebuilt successfully.
- Source-level failures remain commit-blocking, including non-finite coordinates, invalid ownership or references, impossible primitive definitions, and invalid station domains.
- Envelopes and their previews are derived products. They are deterministic for identical source, tolerance policy, geometry-engine version, and output profile, and are not authoritative persisted source.

## Alternatives Considered

- Model A - require a valid envelope before committing RoadSpline source. Rejected because derived geometry would constrain source validity and prevent iterative authoring through temporarily invalid shapes.
- Model B - allow structurally valid source to commit while reporting failed derived output. Accepted because it preserves the source/derived boundary and supports repair workflows without permitting invalid output to be treated as valid.

## Consequences

- The command layer must distinguish a nonblocking derived `Error` from blocking source validation errors.
- Failed generation must return bounded preview geometry and stable diagnostics; dependent operations that require valid envelope output must check that status.
- Tests must demonstrate that source commits survive injected envelope failures, diagnostics are retained, invalid previews are bounded, and source failures still reject commits.

## Validation

Checkpoint F validates this policy with a failure-injection case for each required offset diagnostic category, verifies bounded invalid previews and nonblocking source commit behavior, and proves deterministic derived output for identical inputs.
