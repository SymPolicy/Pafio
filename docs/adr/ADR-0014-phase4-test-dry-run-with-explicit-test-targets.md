# ADR-0014: Phase-4 Activates `spio test --dry-run` with Explicit `[[test]]` Targets

**Purpose:** Record the decision, context, alternatives, and consequences for the first native `test` compile-plan path and the manifest-side test target model.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

Earlier phase-4 work intentionally left `spio test` stubbed because the project did not yet freeze where test targets came from. The current compile-plan schema already allows `intent = "test"` and `entry.target_kind = "test"`, but package target metadata still needed a stable source.

Without an explicit manifest-side model, any `test` implementation would be guessing from filenames or directories, which would create an accidental contract.

## Decision

1. Freeze explicit package tests as `[[test]]` entries in `spio.toml`.
   - each `[[test]]` entry requires `name` and `path`
   - `[[test]].name` values must be unique within the package
   - `[[test]].path` follows the same explicit project-relative path rule as `[[bin]].path`
2. Extend `compile-plan v1` package target metadata with an additive optional `targets.tests` array.
   - each test record contains `name` and absolute `path`
   - plans that do not expose explicit package tests may continue omitting `targets.tests`
3. Activate `spio test` in phase 4 with the minimal public surface:
   - `spio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]`
4. `spio test --dry-run` uses the same compile-plan pipeline as `build` and `run`, but emits `intent = "test"`.
5. `spio test` only targets explicit manifest `[[test]]` entries.
   - `--bin` and `--lib` are invalid for `test`
   - if the selected package has exactly one `[[test]]`, it is the default entry
   - if the selected package has multiple tests, `--test <name>` is required
6. Non-dry-run `spio test` follows the same published compile-plan compatibility gate as `build` and `run`.
   - under the current compatibility matrix it fails before compiler execution begins

## Alternatives

1. Continue leaving `spio test` stubbed until the compiler consumer ships.
   - Rejected because the project can already validate and emit the package-manager side of the test contract.
2. Infer tests from a hard-coded directory such as `tests/`.
   - Rejected because it creates a magic rule that is not reflected in the manifest or the compile-plan schema.
3. Add tests as a required field in `compile-plan v1`.
   - Rejected because `v1` only permits additive optional schema growth without a major version bump.

## Consequences

Positive:

1. The compile workflow now covers all three plan intents that the current schema already exposes: `build`, `run`, and `test`.
2. Package tests are now explicit package metadata instead of an implied filesystem convention.
3. `spio test --dry-run` can validate entry selection and package target metadata before compiler integration is published.

Negative:

1. Non-dry-run `test` is still phase-gated by the published compiler interface.
2. The manifest surface is now larger and must stay synchronized with compile-plan target metadata.
