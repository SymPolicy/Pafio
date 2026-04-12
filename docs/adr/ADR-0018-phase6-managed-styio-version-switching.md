# ADR-0018: Phase-6 Activates Managed `styio` Version Switching Through `spio tool use`

**Purpose:** Record the decision, context, alternatives, and consequences for switching the active managed `styio` compiler after multiple versions have been installed under `SPIO_HOME`.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

`spio tool install` now installs local compatible `styio` executables into versioned roots under `SPIO_HOME/tools/styio/<channel>/<compiler-version>/` and refreshes a managed default compiler under `SPIO_HOME/tools/styio/current/`.

That closes installation, but not lifecycle management. Once multiple versions are installed, users still need a stable way to switch the active managed compiler without reinstalling from its original source path.

## Decision

1. Activate managed compiler switching as a separate tool subcommand:
   - `spio tool use --version <compiler-version> [--channel <channel>]`
2. `tool use` only targets already installed managed compiler roots under `SPIO_HOME/tools/styio/`.
   - it does not install from a raw filesystem path
   - it does not download remote releases
3. Selection rules are:
   - `--version` is required
   - `--channel` is optional
   - if `--channel` is omitted and the version matches more than one installed channel, the command fails as ambiguous
4. `tool use` re-validates the selected managed compiler through the existing public compatibility path before updating managed current state.
5. Promoting a managed compiler to current refreshes:
   - `SPIO_HOME/tools/styio/current/bin/styio`
   - `SPIO_HOME/tools/styio/current/install.json`
6. The existing compiler fallback order remains unchanged:
   - explicit `--styio-bin <path>`
   - `SPIO_STYIO_BIN`
   - managed current compiler under `SPIO_HOME/tools/styio/current/bin/styio`
7. `tool use` shares the same tool-management exit code family as `tool install`.

## Alternatives

1. Overload `tool install` to also switch versions.
   - Rejected because installation and activation are separate lifecycle operations with different inputs.
2. Switch by rewriting `SPIO_STYIO_BIN`.
   - Rejected because environment variables are caller-local state, not durable managed tool state.
3. Allow `tool use` to point at arbitrary unmanaged filesystem paths.
   - Rejected because that collapses the distinction between install and use and bypasses the managed version store.

## Consequences

Positive:

1. Managed compiler installs now have a real activation workflow instead of forcing reinstall-from-source to switch versions.
2. The active managed compiler remains explicit, durable, and compatible with the existing fallback order.
3. Later remote tool distribution can build on top of a stable install/use split.

Negative:

1. `tool` now has more than one active subcommand, which slightly expands the CLI surface.
2. The current implementation still refreshes `current/` as a copied managed root instead of a lighter alias or symlink model.
