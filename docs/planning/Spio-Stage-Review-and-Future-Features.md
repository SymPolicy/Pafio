# Spio Stage Review and Future Features

**Purpose:** Summarize the current implemented `spio` surface, capture the durable lessons from the implementation path so far, and rank the next high-value features using mature package-manager patterns as reference points.

**Last updated:** 2026-04-12

## 1. Scope and Ownership

This document is a planning summary.

It may summarize:

- the current implemented command surface
- implementation-stage gaps
- lessons learned during delivery
- recommended next features and sequencing

It does not own:

- CLI syntax or exit-code rules
- manifest or lockfile rules
- compatibility policy
- durable implementation decisions

Those remain owned by governance and ADR documents.

## 2. Stage Snapshot

As of 2026-04-12, the authoritative implementation path is the native `C++20` + `CMake` core recorded in [ADR-0002](../adr/ADR-0002-native-cpp20-cmake-phase2-core.md). The project is no longer only a bootstrap scaffold: it has a real manifest core, a real resolver, a real local packaging path, and a managed local compiler lifecycle.

Current local validation status:

- native test suite: `95/95` passing
- native workflow verification: passing
- extractability verification: passing
- styio handoff spec and black-box gate: present

The active public command surface is indexed in [Spio-Entry-Argument-Index.md](../governance/Spio-Entry-Argument-Index.md) and governed by [Spio-CLI-Contract.md](../governance/Spio-CLI-Contract.md).

## 3. Implemented Functionality by Area

### 3.1 Core Documents and Native Runtime

Implemented:

- dedicated ADR workflow under [`docs/adr/`](../adr/INDEX.md)
- native `C++20` + `CMake` implementation path
- stable top-level CLI with `--help`, `--version`, `--json`
- machine-readable `spio machine-info`

Why it matters:

- the project now has a stable implementation spine and a stable place to record decisions
- command shape drift is reduced because syntax ownership was centralized in the argument index

Owner documents:

- [Spio-CLI-Contract.md](../governance/Spio-CLI-Contract.md)
- [Spio-Entry-Argument-Index.md](../governance/Spio-Entry-Argument-Index.md)
- [ADR-0001](../adr/ADR-0001-spio-adopts-dedicated-adr-directory.md)
- [ADR-0002](../adr/ADR-0002-native-cpp20-cmake-phase2-core.md)
- [ADR-0003](../adr/ADR-0003-entry-argument-index-ssot.md)

### 3.2 Manifest, Lockfile, Targets, and Workspace Core

Implemented:

- native parsing and validation for `spio.toml` and `spio.lock`
- canonical manifest and lockfile write-back
- explicit `toolchain`, `[lib]`, `[[bin]]`, and `[[test]]` target model
- explicit workspace membership and exclusion rules
- adjacent lockfile drift detection

Current scope:

- accepted dependency sources are `workspace`, `path`, pinned `git`, and registry
- registry dependencies now use explicit `package`, `version`, and `registry` URL fields
- manifest and lock conventions are stable enough to support editing, resolving, and packaging

Owner documents:

- [Spio-Manifest-and-Lock-Conventions.md](../governance/Spio-Manifest-and-Lock-Conventions.md)
- [ADR-0004](../adr/ADR-0004-phase2-canonical-manifest-lock-writeback.md)
- [ADR-0005](../adr/ADR-0005-phase2-lock-command-local-graph-scope.md)
- [ADR-0006](../adr/ADR-0006-phase2-lock-cli-and-local-identity.md)

### 3.3 Resolver, Graph, and Hermetic Source Cache

Implemented:

- `single-version-v1` resolver over workspace, path, pinned git, and registry
- lock generation from the active resolver graph
- read-only tree rendering from the resolver graph
- `fetch` to materialize pinned git and registry cache state
- hermetic git mirrors and snapshots under `SPIO_HOME`
- hermetic registry metadata, blob, and checkout cache under `SPIO_HOME/registry/`
- project-local vendored git snapshots under `.spio/vendor/`

Current behavior:

- same package name must resolve to one effective version and one effective source fingerprint
- pinned git snapshots are authoritative and path traversal outside a pinned snapshot is rejected
- registry blobs are authoritative and are verified by immutable `sha256` before extraction
- `check` is graph-aware and lock-drift-aware

Tradeoff:

- the resolver is intentionally conservative and will reject graphs that more permissive ecosystems might accept

Owner documents:

- [Spio-Manifest-and-Lock-Conventions.md](../governance/Spio-Manifest-and-Lock-Conventions.md)
- [Spio-Version-Decoupling-Constraints.md](../governance/Spio-Version-Decoupling-Constraints.md)
- [ADR-0007](../adr/ADR-0007-phase3-minimal-single-version-resolver.md)
- [ADR-0008](../adr/ADR-0008-hermetic-git-snapshot-cache-under-spio-home.md)
- [ADR-0009](../adr/ADR-0009-phase3-tree-renders-resolver-graph.md)
- [ADR-0011](../adr/ADR-0011-phase3-check-validates-resolver-graph-and-lock-drift.md)
- [ADR-0019](../adr/ADR-0019-phase6-reproducible-workflow-flags-and-project-local-vendor.md)

### 3.4 Reproducible Workflow Flags

Implemented:

- `--locked`
- `--offline`
- `--frozen`
- `spio vendor`

Why it matters:

- the project now has a first-class reproducibility surface for core workflow commands instead of relying only on convention
- vendored snapshots reduce dependence on prewarmed `SPIO_HOME` state

Current scope:

- the current vendor implementation targets pinned git snapshots only
- vendored state lives under `.spio/vendor/` so it does not collide with ordinary project paths such as local `vendor/` dependencies

Owner documents:

- [Spio-CLI-Contract.md](../governance/Spio-CLI-Contract.md)
- [Spio-Entry-Argument-Index.md](../governance/Spio-Entry-Argument-Index.md)
- [ADR-0019](../adr/ADR-0019-phase6-reproducible-workflow-flags-and-project-local-vendor.md)

### 3.5 Dependency Editing

Implemented:

- `spio add`
- `spio remove`
- manifest canonical rewrite after successful edit
- adjacent lock refresh after successful edit
- rollback of manifest and lockfile if post-edit resolution fails

Why it matters:

- the project now has a real dependency-edit loop instead of a validate-only loop
- dependency editing is transaction-like at the local workspace boundary

Owner documents:

- [Spio-CLI-Contract.md](../governance/Spio-CLI-Contract.md)
- [ADR-0010](../adr/ADR-0010-phase3-basic-dependency-edit-and-fetch-commands.md)

### 3.6 Compile-Plan Generation and Dry-Run Workflow

Implemented:

- `spio build --dry-run`
- `spio run --dry-run`
- `spio test --dry-run`
- local `compile-plan v1` emission under project-local `.spio/build/<cache-key>/plan.json`
- explicit target selection for `lib`, `bin`, and `test`
- compatibility gating that prevents claiming live compiler execution before the published `styio` side exists

Important boundary:

- `spio` owns local plan generation
- `spio` does not yet claim active compile-plan interoperability because the published compiler consumer is still gated
- compiler handoff is now defined explicitly through the `styio` spec and executable gate, even though the live compiler phase is still blocked

Owner documents:

- [Spio-CLI-Contract.md](../governance/Spio-CLI-Contract.md)
- [Spio-Version-Decoupling-Constraints.md](../governance/Spio-Version-Decoupling-Constraints.md)
- [ADR-0012](../adr/ADR-0012-phase4-build-dry-run-compile-plan.md)
- [ADR-0013](../adr/ADR-0013-phase4-run-dry-run-and-test-gap.md)
- [ADR-0014](../adr/ADR-0014-phase4-test-dry-run-with-explicit-test-targets.md)

### 3.7 Packaging and Publish Preflight

Implemented:

- deterministic `spio pack`
- canonical package archive generation
- workspace-aware package selection
- `spio publish --dry-run` local preflight
- local filesystem registry publish transport with immutable archive blobs and version index entries
- remote HTTP registry publish transport over the same static layout
- registry dependency metadata in published version entries

Current limit:

- published packages may carry registry-addressable dependencies, but path/git dependencies remain invalid for publishable artifacts
- registry consumption is now live through `file://`, `http://`, and `https://` repository roots using the same static blob-and-index layout
- anonymous remote registry writes are now live through HTTP `PUT`, but auth and stronger trust hardening are still not implemented

Owner documents:

- [Spio-CLI-Contract.md](../governance/Spio-CLI-Contract.md)
- [ADR-0015](../adr/ADR-0015-phase4-pack-deterministic-source-archive.md)
- [ADR-0016](../adr/ADR-0016-phase6-publish-dry-run-preflight-without-registry-transport.md)
- [ADR-0021](../adr/ADR-0021-phase6-filesystem-registry-publish-transport.md)
- [ADR-0023](../adr/ADR-0023-phase6-url-based-registry-consume-and-cloud-static-layout.md)
- [ADR-0024](../adr/ADR-0024-phase6-anonymous-http-remote-registry-publish.md)

### 3.8 Managed `styio` Compiler Lifecycle

Implemented:

- `spio tool install --styio-bin <path>`
- versioned managed compiler roots under `SPIO_HOME/tools/styio/<channel>/<version>/`
- managed current compiler under `SPIO_HOME/tools/styio/current/`
- `spio tool use --version <compiler-version> [--channel <channel>]`
- `spio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]`
- project-local `spio-toolchain.toml` pin files with upward discovery from the selected manifest
- compiler selection fallback order across explicit path, environment variable, project-local pin, and managed current compiler

Why it matters:

- compiler lifecycle is now durable local state instead of an ad hoc environment variable convention
- project-level compiler selection is now shareable repository state instead of only developer-local machine state
- the package manager can evolve toward a real toolchain manager without violating the decoupling contract

Owner documents:

- [Spio-Version-Decoupling-Constraints.md](../governance/Spio-Version-Decoupling-Constraints.md)
- [ADR-0017](../adr/ADR-0017-phase6-managed-local-styio-tool-install.md)
- [ADR-0018](../adr/ADR-0018-phase6-managed-styio-version-switching.md)
- [ADR-0020](../adr/ADR-0020-phase6-project-local-managed-styio-pinning.md)

## 4. What Is Still Partial or Blocked

The project is not feature-empty anymore, but three important boundaries remain explicit:

1. Real compiler execution is not yet live.
   - `build`, `run`, and `test` only guarantee `--dry-run` today.
   - non-dry-run execution remains gated on a published `styio --compile-plan <path>` consumer and compatible handshake.
2. Registry work is only partially live.
   - local/filesystem and anonymous remote HTTP publish transport now exist
   - registry dependency resolution and fetch are live through static `file://`, `http://`, and `https://` repository roots
   - auth, signatures, and stronger trust-policy hardening are still not implemented
3. The resolver is still phase-3 conservative.
   - `single-version-v1` is a deliberate constraint, not a full semver/feature solver.

These are the correct current boundaries. None of them should be hidden behind optimistic wording.

## 5. Problems Encountered and Durable Lessons

### 5.1 Scattered Parameter Definitions Drift Quickly

Problem:

- command spellings and parameter rules started to spread across code, contract text, and scripts

Lesson:

- user-visible arguments need a single owner document from the beginning

What changed:

- the project created [Spio-Entry-Argument-Index.md](../governance/Spio-Entry-Argument-Index.md) and recorded that decision in [ADR-0003](../adr/ADR-0003-entry-argument-index-ssot.md)

### 5.2 Python Bootstrap Was Useful, Then Became Friction

Problem:

- the Python scaffold helped freeze the boundary quickly, but once the contracts stabilized it introduced a second implementation path

Lesson:

- bootstrap languages are good for freezing a contract, but bad as a long-term parallel implementation after the native path is chosen

What changed:

- the project moved the authoritative path to native `C++20` + `CMake` and kept the compiler boundary process-based

### 5.3 Manifest Rules Must Be Frozen Before Resolver Work Grows

Problem:

- scaffolding code had already started using `toolchain`, targets, and workspace structure before the native validator had fully frozen them

Lesson:

- if the manifest model is vague, resolver and workflow layers will multiply ambiguity

What changed:

- explicit `toolchain`, `lib/bin/test`, workspace membership, and canonical write-back rules were frozen before phase-3 growth

### 5.4 A Lockfile Without an Active Resolver Is Weak

Problem:

- early lock validation can only check syntax and shape; it cannot say whether the file is semantically current

Lesson:

- lockfiles become trustworthy only after the package manager can recompute the active graph and compare it to the stored graph

What changed:

- `check` and `lock --check` became resolver-backed and drift-aware

### 5.5 Pinned Git Needs Hermetic Snapshots, Not Host-Leaking Paths

Problem:

- plain filesystem path reuse from a git dependency can accidentally leak outside the pinned revision and couple results to host-local state

Lesson:

- pinned sources must be materialized into immutable snapshots, and transitive path traversal must remain inside that snapshot boundary

What changed:

- git mirrors and snapshots now live under hermetic `SPIO_HOME` state

### 5.6 Owning a Schema Is Not the Same as Supporting the Workflow

Problem:

- once `compile-plan/v1` existed in-repo, there was a real risk of over-claiming support before the compiler consumer existed

Lesson:

- local schema ownership, local dry-run generation, and end-to-end interoperability are three different maturity levels

What changed:

- `spio machine-info` keeps compile-plan support empty until the published compatibility matrix and compiler consumer authorize it

### 5.7 Workspace and Target Magic Becomes Expensive Fast

Problem:

- workspace roots, member selection, test targets, and default package selection become ambiguous quickly if the CLI relies on inference

Lesson:

- defaulting is useful only while ambiguity is impossible; beyond that, explicit package and target selection is cheaper than trying to be clever

What changed:

- `build`, `run`, `test`, `pack`, and `publish` all favor explicit target or package disambiguation when ambiguity exists

### 5.8 Publish Should Start With Deterministic Local Packaging, Not Registry Guesswork

Problem:

- adding a publish command before the package archive shape and preflight contract are stable creates false progress

Lesson:

- deterministic local packaging is the correct front half of publishing

What changed:

- `pack` became real first, then `publish --dry-run` reused the same archive contract

### 5.9 Tool Installation and Tool Activation Are Different Operations

Problem:

- once more than one local compiler version exists, reinstalling from a raw filesystem path is not a version management workflow

Lesson:

- install and activation must be split

What changed:

- the project now has both `tool install` and `tool use`

## 6. Industry Patterns Worth Borrowing Next

The next feature choices should not be made in isolation. Mature ecosystems already show which investments pay off.

### 6.1 Cargo: Explicit Workspaces, Profiles, Packaging Verification, Offline Modes

Useful patterns:

- workspace-wide defaults with explicit package selection and default members
- first-class build profiles
- deterministic packaging that rewrites the manifest and verifies the packaged result
- `--locked`, `--offline`, and `--frozen`
- a dedicated vendor workflow for registry and git sources

Why this matters for `spio`:

- `spio` already resembles Cargo structurally more than a pip/npm-style installer
- the missing pieces are not conceptual; they are the exact operational layers around reproducibility, packaging verification, and offline workflows

References:

- [Cargo workspaces](https://doc.rust-lang.org/cargo/reference/workspaces.html)
- [Cargo profiles](https://doc.rust-lang.org/cargo/reference/profiles.html)
- [cargo package](https://doc.rust-lang.org/cargo/commands/cargo-package.html)
- [cargo vendor](https://doc.rust-lang.org/cargo/commands/cargo-vendor.html)

### 6.2 rustup and Go: Toolchain Selection Must Be a Real Dependency Axis

Useful patterns:

- explicit toolchain override order
- project-local pinned toolchain files
- workspace-local minimum toolchain and preferred toolchain
- automatic or policy-driven toolchain switching only when the version requirement demands it

Why this matters for `spio`:

- the project already treats compiler version as a first-class compatibility axis
- toolchain choice should eventually be visible at workspace level, not only through ad hoc CLI flags and global managed state

References:

- [rustup overrides](https://rust-lang.github.io/rustup/overrides.html)
- [Go toolchains](https://go.dev/doc/toolchain)

### 6.3 Go Modules: Checksums, Proxy Separation, Workspace Sync, and Module Identity Discipline

Useful patterns:

- a checksum database independent from origin servers
- proxy and checksum trust separated from version control origin
- workspace sync back into member manifests
- vendoring and module cache discipline

Why this matters for `spio`:

- once `spio` gains registry transport, integrity and repeatability will matter more than raw download speed
- the current publish preflight is already compatible with an immutable source archive + checksum model

References:

- [Go modules reference](https://go.dev/ref/mod)
- [Go toolchains](https://go.dev/doc/toolchain)

### 6.4 pnpm: Content-Addressed Storage, Strict Workspace Protocols, and Monorepo Filtering

Useful patterns:

- a content-addressed shared store
- strict local-workspace dependency protocol
- graph-aware package filtering for large workspaces
- supply-chain hardening settings such as minimum release age and trust policy

Why this matters for `spio`:

- `spio` already has a workspace model and a hermetic home directory; a content-addressed store is a natural next step
- workspace-only dependency intent should become explicit rather than inferred
- monorepo ergonomics will matter once the resolver and build workflow are fully live

References:

- [pnpm workspace](https://pnpm.io/workspaces)
- [pnpm filtering](https://pnpm.io/filtering)
- [pnpm symlinked node_modules structure](https://pnpm.io/symlinked-node-modules-structure)
- [pnpm settings](https://pnpm.io/settings)

### 6.5 uv: Automatic Lock/Sync Discipline, Shared Lockfile Workspaces, and Managed Tools

Useful patterns:

- commands that automatically check lock freshness
- separate `locked`, `frozen`, and exact-sync behaviors
- shared-lock workspaces with explicit per-package execution
- dedicated managed tool environments and directories
- clear separation between disposable cache and persistent tool state

Why this matters for `spio`:

- `spio` already has the beginnings of these layers: lock drift checks, project-local `.spio/`, and managed tools under `SPIO_HOME`
- the next useful step is to make these layers more operationally precise

References:

- [uv locking and syncing](https://docs.astral.sh/uv/concepts/projects/sync/)
- [uv workspaces](https://docs.astral.sh/uv/concepts/projects/workspaces/)
- [uv tools](https://docs.astral.sh/uv/concepts/tools/)
- [uv storage](https://docs.astral.sh/uv/reference/storage/)

## 7. Highest-Value Next Features and Implementation Direction

The ranking below is based on current `spio` state, not on ecosystem novelty alone.

### Priority 1. Real `styio --compile-plan` Execution and Build Receipts

Why it is first:

- the project already has manifest validation, a resolver, dry-run compile-plan emission, and managed compiler selection
- the largest missing value is end-to-end local build execution

Recommended implementation:

- keep the current process boundary
- extend compatibility policy only when `styio` publishes real compile-plan support
- turn non-dry-run `build`, `run`, and `test` into:
  - preflight
  - plan emission
  - compiler spawn
  - artifact capture
  - receipt write
- introduce a build receipt under `.spio/build/<cache-key>/receipt.json` with:
  - selected compiler identity
  - compile-plan hash
  - selected package and target
  - artifact list
  - diagnostic paths
  - wall-clock timing

Important constraint:

- do not bypass the published machine contract even if `spio` and `styio` are developed side by side

### Priority 2. Registry Transport With Integrity Before Convenience

Why it is third:

- `pack` and `publish --dry-run` already establish the front half of publishing
- registry transport is now the real missing back half

Recommended implementation:

- start with an immutable package archive plus signed or at least checksum-verified registry metadata
- separate:
  - source archive upload
  - registry index update
  - checksum publication
  - client download path
- keep private-registry and public-registry trust policy configurable from the beginning
- plan for a checksum or transparency-log model, not only origin-server trust

Good minimum target:

- registry index entry containing package name, version, digest, dependencies, and publish timestamp

Do not do first:

- do not start with mutable git-based publish semantics pretending to be a registry

### Priority 3. Content-Addressed Source and Artifact Cache

Why it is fourth:

- current hermetic directories are correct, but they are not yet fully content-addressed
- cache deduplication and artifact reuse will matter as soon as non-dry-run builds are live

Recommended implementation:

- store fetched source archives and extracted normalized trees by digest under `SPIO_HOME/cache/`
- key compiled artifacts by:
  - compiler version
  - compile-plan major
  - package graph fingerprint
  - target identity
  - profile
  - source digest
- keep project-local `.spio/build/` as the active working area and `SPIO_HOME` as the reusable global store

Borrowed pattern:

- this is closer to pnpm and uv than to classic language-specific caches, and it fits `spio`'s existing decoupling rules

### Priority 4. Workspace-Level Execution Filters and Defaults

Why it is fifth:

- once build/test are fully live, multi-package repositories will need fast, explicit targeting

Recommended implementation:

- keep `--package` as the precise selector
- add:
  - `--workspace`
  - `--exclude <package>`
  - `--filter <selector>` later
- selectors should be graph-aware, not only string-match based
- changed-package and dependent-package filters are worth doing only after the basic selector model is stable

Borrowed pattern:

- Cargo’s explicit workspace/default-members model and pnpm’s filter syntax are the most useful references here

### Priority 5. Project-Local Toolchain Pinning

Why it is sixth:

- managed compiler install/use exists, but reproducible repository-level compiler pinning does not

Recommended implementation:

- extend the toolchain model with a repository-local pin file or workspace-root toolchain block that records:
  - preferred channel
  - exact version
  - optional components/targets later if `styio` grows them
- resolve compiler choice in this order:
  - explicit CLI flag
  - environment override
  - workspace-local pin
  - managed current compiler
- keep toolchain pinning compatible with the existing machine-info handshake and compatibility matrix

Borrowed pattern:

- rustup and Go both treat the toolchain as a versioned dependency axis instead of a hidden system prerequisite

### Priority 6. Workspace-Only Dependency Intent and Patch/Override Model

Why it is seventh:

- the current resolver supports workspace/path/git, but there is no first-class way to say “this must resolve locally” or “temporarily replace this dependency graph edge”

Recommended implementation:

- add an explicit workspace-only dependency marker instead of relying on source-kind inference
- add a workspace-root override mechanism for local development, similar in spirit to Cargo’s `[patch]` or Go’s `replace`
- exclude override state from published package metadata

Why this is valuable:

- it improves local iteration speed without weakening the publish boundary

## 8. Work That Is Tempting but Premature

The following ideas are real, but they are not the highest-value next steps for the current repository state:

- a full semver + feature-flag multi-version resolver before registry transport exists
- remote compiler downloads before trust policy and checksum rules exist
- in-process `styio` integration
- automatic workspace magic that hides package or target ambiguity
- publish transport before immutable package identity and checksum story are defined

## 9. Summary

`spio` has crossed the line from bootstrap scaffold into a functioning local package manager core. The most important thing it still lacks is not “more commands”; it is live end-to-end compiler execution, a fuller integrity-preserving registry path, and artifact/cache discipline that extends beyond the current local offline workflow.

That is also where the best ecosystems converge: Cargo, Go, pnpm, rustup, and uv all treat reproducibility, explicit workspace behavior, toolchain identity, and cache discipline as first-class features. `spio` should continue in that direction rather than chase surface-area growth for its own sake.
