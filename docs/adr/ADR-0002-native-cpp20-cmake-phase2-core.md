# ADR-0002: Native C++20 and CMake as the Phase-2 Implementation Path

**Purpose:** Record the decision, context, alternatives, and consequences for making native `C++20` + `CMake` the authoritative implementation path for `pafio` phase 2.

**Last updated:** 2026-04-10

## Status

Accepted

## Context

The repository started with a Python bootstrap to freeze CLI shape, manifest/lock validation, and compiler-handshake boundaries. That scaffold was useful for bootstrapping contracts, but the long-term project direction is a standalone package manager aligned operationally with `styio`.

The project owner explicitly requested that `pafio`'s tech stack remain consistent with `styio`, which is built with native `C++20` and `CMake`.

## Decision

1. Treat native `C++20` + `CMake` as the authoritative implementation path for phase 2 and onward.
2. Keep the Python bootstrap only as temporary migration reference while native parity is completed.
3. Align operational tooling with `styio` where it does not violate repository independence:
   - `C++20`
   - `CMake`
   - `.clang-format`
   - native test targets
4. Do not interpret stack alignment as permission to depend on `styio` internals or share compiler-private code.

## Alternatives

1. Continue growing the Python bootstrap into the real implementation.
   - Rejected because it diverges from the requested long-term stack and would create another migration later.
2. Rebuild `pafio` in a different native stack unrelated to `styio`.
   - Rejected because it would weaken operational consistency without providing a compensating architectural benefit.
3. Embed `pafio` inside the `styio` source tree.
   - Rejected because it violates the repository independence boundary.

## Consequences

Positive:

1. The active implementation path now matches the requested long-term stack.
2. Native build/test infrastructure can evolve without a second re-platforming step.
3. Operational expectations between `styio` and `pafio` are easier to align.

Negative:

1. The repository temporarily carries both native and Python bootstrap paths during migration.
2. Native parsing and serialization work is more verbose than the original Python scaffold.
