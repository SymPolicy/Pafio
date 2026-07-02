# ADR-0013: Phase-4 Activates `pafio run --dry-run` and Freezes the Current Test-Target Gap

**Purpose:** Record the decision, context, alternatives, and consequences for the first native `run` compile-plan path and the decision not to fake `test`.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

Phase 4 now has native compile-plan emission for `pafio build`. The same `compile-plan v1` schema also has an `intent = "run"` branch, which means `pafio` can already model executable entry selection without waiting for a separate schema revision.

`test` is different. The current manifest rules freeze only `[lib]` and `[[bin]]`, while the current `compile-plan v1` package shape exposes only `lib` and `bins` target sets. Even though `entry.target_kind` allows `"test"`, the contract does not yet define where those tests come from or how they are enumerated.

## Decision

1. Activate `pafio run` in phase 4 with the minimal public surface:
   - `pafio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]`
2. `pafio run --dry-run` uses the same compile-plan generation pipeline as `pafio build`, but emits `intent = "run"`.
3. `pafio run` only targets explicit binary targets.
   - `--lib` is invalid for `run`
   - if the selected package has exactly one `[[bin]]`, it is the default entry
   - if the selected package has multiple binaries, `--bin <name>` is required
4. Non-dry-run `pafio run` uses the same published compatibility gate as `pafio build`.
   - under the current compatibility matrix it fails before compiler execution begins
5. `pafio test` remains reserved and stubbed.
   - phase 4 must not invent a fake test-target discovery rule
   - a later phase must publish both a manifest target model and a compile-plan package extension for tests before native `test` can activate

## Alternatives

1. Keep `pafio run` stubbed until the compiler consumer ships.
   - Rejected because `run` entry selection is already representable in the current compile-plan schema and benefits from the same local validation as `build`.
2. Allow `pafio run` to reuse `--lib` and silently reject it later.
   - Rejected because it makes the public surface broader than the contract can support.
3. Activate `pafio test` by guessing that tests map to bins, libs, or filesystem conventions.
   - Rejected because it would create a contract by accident and would likely drift from future `styio` expectations.

## Consequences

Positive:

1. `pafio` now exercises both build and run compile-plan intents locally.
2. Binary entry selection rules are frozen early instead of being inferred from ad hoc CLI behavior later.
3. The missing `test` target contract is now explicit and discoverable in ADR form.

Negative:

1. `run` is still only partially active in the current published compatibility phase.
2. `test` remains intentionally unavailable until the package and compile-plan contracts grow a real test model.
