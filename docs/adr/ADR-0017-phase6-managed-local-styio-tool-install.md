# ADR-0017: Phase-6 Activates Managed Local `styio` Installation Through `pafio tool install`

**Purpose:** Record the decision, context, alternatives, and consequences for the first native `tool install` surface and the managed local compiler layout under `PAFIO_HOME`.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

`pafio` now has native resolver, workflow-planning, source-package, and publish-preflight paths, but the last reserved public command is still `pafio tool install`. Leaving it stubbed would keep the package-manager surface incomplete.

At the same time, this repository still does not define a remote compiler registry, release feed, download manifest, signature model, or multi-file tool bundle contract. A networked installer would therefore be guesswork.

`pafio` already depends on a published `styio --machine-info=json` handshake and a package-manager-side compatibility matrix. That makes local compiler admission and managed reuse possible without inventing any new remote protocol.

## Decision

1. Activate `tool install` as a managed local compiler install command, not as a remote downloader.
2. Freeze the first public surface as:
   - `pafio tool install --styio-bin <path>`
3. The current native core only installs local self-contained `styio` executables.
   - the selected path must already be executable
   - the selected executable must support `--machine-info=json`
4. `tool install` must validate the selected compiler through the existing public compatibility path before writing managed state.
   - it reuses `styio --machine-info=json`
   - it reuses `pafio/contracts/compat/styio-support.toml`
5. Managed install layout lives under hermetic `PAFIO_HOME/tools/styio/`.
   - versioned install root: `PAFIO_HOME/tools/styio/<channel>/<compiler-version>/`
   - managed default root: `PAFIO_HOME/tools/styio/current/`
   - managed binary path: `.../bin/styio`
6. `tool install` writes stable metadata beside both the versioned install root and the managed default root.
7. Compiler selection order becomes:
   - explicit `--styio-bin <path>`
   - `PAFIO_STYIO_BIN`
   - managed default compiler at `PAFIO_HOME/tools/styio/current/bin/styio`
8. Reserve exit code `18` for tool-install-specific local failures.
   - handshake failures and compatibility failures keep using the existing compiler-probe and contract exit codes

## Alternatives

1. Keep `tool install` fully stubbed until a remote registry exists.
   - Rejected because a useful local compiler-management workflow can already be provided without inventing network behavior.
2. Make `tool install` download arbitrary compiler releases from ad hoc URLs.
   - Rejected because no published release or trust contract exists in this repository.
3. Accept local compilers without compatibility validation.
   - Rejected because that would make the managed default compiler path too easy to populate with a binary the current `pafio` release cannot actually use.
4. Store managed compilers outside `PAFIO_HOME`.
   - Rejected because it would violate the repository's hermetic state rules and make tool reuse less predictable across gates.

## Consequences

Positive:

1. `tool install` is no longer a stub; it provides a concrete compiler-management workflow that fits the current published contracts.
2. `check`, `build`, `run`, and `test` gain a usable managed-binary fallback when callers do not pass `--styio-bin`.
3. The repository gets a stable first tool-install layout that later remote-install work can build on top of.

Negative:

1. The first native `tool install` only handles local self-contained executables; it does not solve remote distribution.
2. Installing a managed compiler still does not bypass the published compile-plan gate for non-dry-run workflow commands.
3. Managed installs currently duplicate the active compiler into a `current` root for simple fallback resolution.
