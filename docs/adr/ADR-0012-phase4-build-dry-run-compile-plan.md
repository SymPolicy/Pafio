# ADR-0012: Phase-4 Activates `spio build --dry-run` and Local Compile-Plan Emission

**Purpose:** Record the decision, context, alternatives, and consequences for the first native compile-plan generation path behind `spio build`.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

Phase 3 now has a real resolver graph, lock generation, tree rendering, dependency editing, and fetch behavior. The next missing layer is build orchestration.

`spio` already owns `contracts/compile-plan/v1`, but the published compatibility matrix still records `supported_compile_plan_versions = []`, which means the compiler-facing phase is not yet live. Waiting for the compiler consumer before implementing any build-side planning would leave the package manager unable to validate or materialize its own build intent. Pretending that the compiler integration already exists would violate the published boundary.

## Decision

1. Activate `spio build` in phase 4, but only `--dry-run` is guaranteed to succeed in the current published compatibility phase.
2. Freeze the initial public surface as:
   - `spio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]`
3. `spio build --dry-run` must:
   - resolve the active graph through the same phase-3 resolver path
   - generate a canonical `compile-plan v1`
   - write the plan to `.spio/build/<cache-key>/plan.json`
   - create sibling `artifacts/` and `diag/` directories under the same build root
   - avoid compiler probing unless a later phase explicitly requires it
4. Entry package selection is limited to root packages of the selected manifest graph.
   - if the selected manifest itself defines `[package]`, that root package is the default entry
   - if the graph has multiple root packages and no explicit root package at the selected manifest path, `--package <namespace/name>` is required
5. Initial target selection supports only manifest-defined `lib` and `bin` targets.
   - `[[test]]` remains deferred because the current manifest and compile-plan contracts do not freeze a real test-target model yet
   - if a package exposes more than one plausible build target, the caller must disambiguate with `--lib` or `--bin <name>`
6. Compile-plan generation is stricter than general graph resolution in two ways:
   - the resolved package graph must be acyclic so `resolution.package_order` can be emitted deterministically
   - the resolved package graph must have a uniform `package.edition`, `[toolchain].channel`, and `[toolchain].implicit-std` because `compile-plan v1` has only one top-level toolchain object
7. Phase-4 compile-plan generation uses a temporary sentinel encoding for `toolchain.std_package_id`:
   - `builtin:std@<channel>/<edition>` when implicit std is enabled
   - `builtin:std-disabled@<channel>/<edition>` when implicit std is disabled
   - this is an implementation placeholder until a real builtin/std package identity model is published
8. The local build cache key must include:
   - compiler version when known, otherwise `unbound`
   - compile-plan major version
   - edition
   - profile
   - selected target identity
   - source hash derived from resolved manifest and target source files
9. Non-dry-run `spio build` remains gated by the published compatibility phase.
   - it requires `--styio-bin <path>` or `SPIO_STYIO_BIN`
   - it may proceed to `styio --compile-plan <path>` only when the published compatibility matrix advertises compile-plan v1 for the current phase

## Alternatives

1. Keep `spio build` completely stubbed until `styio --compile-plan` ships.
   - Rejected because it leaves compile-plan generation untested and blocks local orchestration work behind an external dependency.
2. Advertise compile-plan support in `spio machine-info` as soon as local plan generation exists.
   - Rejected because local schema ownership and dry-run emission do not prove published compiler interoperability.
3. Allow build planning over mixed editions or mixed toolchain channels and silently pick the entry package values.
   - Rejected because that would produce a lossy compile-plan whose top-level toolchain lies about the graph.

## Consequences

Positive:

1. `spio` can now produce and persist a real compile-plan artifact before the compiler-facing phase is published.
2. The first native build command exercises resolver metadata, target selection, cache-key derivation, and plan serialization end to end.
3. Future compiler execution work is reduced to a compatibility gate plus one process invocation path.

Negative:

1. `spio build` is only partially active in the current published phase because non-dry-run execution is still blocked by the compatibility matrix.
2. Compile-plan generation currently rejects mixed-edition or mixed-toolchain graphs even if later schema revisions might support them.
3. `std_package_id` is still a sentinel string rather than a real resolved package identity.
