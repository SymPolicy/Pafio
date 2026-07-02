# ADR-0022: Styio Handoff Spec and Black-Box Acceptance Gate

**Purpose:** Record the decision that `pafio` must define the required external `styio` interfaces as a handoff spec and validate them through a black-box acceptance gate instead of prose-only coordination.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

`pafio` now depends on a real external compiler boundary:

- `styio --machine-info=json`
- future `styio --compile-plan <path>`
- stable machine-readable diagnostics

Those requirements have been implied across several documents, but implied requirements are not enough for a split-repository workflow. The package-manager team needs a single document it can hand to the compiler team, and it also needs an executable gate that proves the published binary meets that document.

Without that, the project keeps accumulating the same failure mode:

- roadmap prose says one thing
- local fake compiler fixtures encode something narrower
- compatibility checks enforce only a subset
- joint integration is blocked by ambiguity instead of actual code gaps

## Decision

1. `docs/external/for-styio/Styio-External-Interface-Requirement-Spec.md` becomes the handoff specification for the compiler-side published interface required by `pafio`.
2. The handoff spec defines:
   - required published commands
   - required machine-info fields and types
   - compile-plan consumer expectations
   - diagnostics expectations
   - black-box acceptance criteria
3. `scripts/styio-interface-gate.py` becomes the executable black-box gate for validating a released `styio` binary against the published interface.
4. The handshake contract is tightened so the published machine-info payload must include:
   - `tool = "styio"`
   - `compiler_version`
   - `channel`
   - `supported_contracts`
   - `capabilities`
   - `edition_max`
5. `pafio` compatibility checks and fake compiler fixtures must align with that handshake instead of relying on narrower local assumptions.
6. The handoff gate validates direct compiler behavior through process boundaries only.
   - it does not read compiler source
   - it does not link against compiler internals

## Alternatives

1. Keep the current roadmap and knowledge-pack prose without a direct handoff spec.
   - Rejected because it is too vague for cross-repository ownership.
2. Define the external interface only in ADRs.
   - Rejected because ADRs explain decisions, but they are not the right SSOT for a published compiler interface.
3. Rely on `pafio check` alone as the compiler handoff gate.
   - Rejected because `pafio check` only covers handshake compatibility and not direct compile-plan execution behavior.
4. Reach into the `styio` source tree during integration.
   - Rejected because it violates the decoupling boundary this project has already frozen.

## Consequences

Positive:

1. The compiler team gets a concrete implementation target instead of scattered expectations.
2. The package-manager team gets an executable acceptance gate instead of manual checklist drift.
3. Interface drift becomes visible earlier because docs, compatibility checks, and fake compiler payloads all have to agree.

Negative:

1. The project now owns one more normative external-dependency document and one more gate script.
2. Tightening the handshake requires updating local fake compiler fixtures.
3. The gate still cannot certify full end-to-end `pafio build/run/test` execution until the compatibility matrix enables compile-plan support for the active phase.
