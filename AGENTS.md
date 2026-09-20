# Atlas Agent Instructions

## Project context

- Atlas is a standalone, game-agnostic editor for fictional and simulated worlds.
- The repository is currently in the v0.1.0 foundation phase. It may contain planning documents without an implementation stack yet.
- Read [README.md](README.md) for product boundaries and design principles. Read [ROADMAP.md](ROADMAP.md) for release sequencing, work-packet requirements, quality gates, and stop conditions.
- The canonical Atlas Master Design Specification is referenced by the documentation but is not currently present in this repository. Do not invent missing requirements; ask for the relevant source or record an ADR proposal.

## Engineering rules

- Treat world units, stable identity, explicit ownership, deterministic behavior, reversible commands, and source-versus-derived separation as architectural invariants.
- Keep authoritative source data separate from rebuildable geometry, previews, indexes, caches, render output, and other derived products.
- Preserve stable IDs across edits, persistence, migrations, undo/redo, and array reordering. Collection position and display names are never identity.
- Keep physical geometry separate from semantic connectivity. Visual crossings do not create network connections.
- Keep the core engine-agnostic. Engine integrations, game-specific meaning, schemas, styles, and exporters belong behind explicit adapter or extension boundaries.
- Persist canonical values in real-world units and treat display units, pixels, tessellation, and camera transforms as presentation or derived concerns.
- Route mutations through the command/transaction boundary once it exists. Preview and cancel must not mutate authoritative state; committed changes need validation, undo/redo, persistence, and deterministic invalidation behavior.
- Treat migrations and save/load as product behavior: preserve unknown extension data, protect the last readable package, and test normalized round trips.
- Build accessibility, deterministic output, diagnostics, security, and performance evidence with each applicable workflow rather than deferring them to release hardening.

## Agent workflow

- Before implementation, identify the release and work-packet scope, affected requirements, authoritative records, derived products, persistence impact, tests, non-goals, and stop conditions.
- Make the smallest complete vertical change. Avoid speculative infrastructure and unrelated refactors.
- Add focused tests for deterministic behavior, identity, ownership, validation, transactions, persistence, and migrations whenever the change affects them.
- Run the repository's documented checks once they exist. At this stage, no build, test, lint, or format commands are defined; do not claim that they were run.
- Stop and request a decision or propose an ADR when a change would alter ownership, identity, units, persisted meaning, migration guarantees, dependency direction, or a normative requirement.
- Report the files changed, requirements addressed, validation performed, persistence impact, known limitations, and any ADR or decision still needed.

## Documentation changes

- Keep architecture and roadmap decisions traceable to the existing documentation instead of duplicating long specifications in code comments or agent instructions.
- Update [README.md](README.md) or [ROADMAP.md](ROADMAP.md) when project-level behavior, scope, or delivery policy changes.