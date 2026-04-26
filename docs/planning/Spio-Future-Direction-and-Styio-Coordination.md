# Spio Future Direction and Styio Coordination

**Purpose:** Define the next development direction for `spio`, the engineering qualities it should optimize for, and the shared coordination expectations that `styio` developers also need to reference.

**Audience:** `spio` maintainers, `styio` maintainers, and anyone planning cross-repository compiler/package-manager work.

**Last updated:** 2026-04-12

## 1. Why This Document Exists

`spio` is no longer only a bootstrap scaffold. It already has:

- a native `C++20` core
- a real manifest and lockfile model
- a real resolver for workspace, path, pinned git, and registry sources
- dry-run compile-plan emission
- local packaging, registry publication, and registry consumption through a static repository layout
- managed `styio` installation, switching, and project pinning

That creates a new problem: the project no longer needs only phase checklists. It needs a shared direction document that explains what kind of package manager it is trying to become and which cross-team interfaces must mature first.

## 2. Product Direction

The intended direction is:

- Cargo-like in contract clarity, workspace discipline, and reproducibility
- Vite-like in development feedback speed
- not npm-like in implicit behavior, weak lock semantics, or permissive dependency-tree ambiguity

This means `spio` should prefer:

- explicit manifests
- deterministic lock behavior
- explicit compiler/toolchain selection
- hermetic and inspectable cache/layout rules
- fast local iteration once the compiler-facing phase is live

It should avoid:

- hidden source discovery
- silent dependency graph rewrites
- environment-sensitive default behavior that is hard to reproduce in CI
- coupling to `styio` internals for the sake of short-term convenience

## 3. Agility Goal

The target is not “flexible at any cost.” The target is “fast while staying predictable.”

For this project, agility means:

- fast edit-to-feedback loops
- low-friction workspace operations
- cheap cache reuse
- direct and stable diagnostics
- minimal ceremony for common local workflows

For this project, agility does not mean:

- skipping lockfiles
- weakening compiler compatibility gates
- letting package resolution become opaque
- hiding important state in process-local heuristics

## 4. Directional Principles

### 4.1 Keep the Boundary Clean

`spio` and `styio` must continue to integrate only through:

- published CLI commands
- machine-readable handshake payloads
- versioned compile-plan contracts
- stable diagnostics

This is the most important architectural rule because it preserves split-repository independence.

### 4.2 Make the Fast Path the Normal Path

Once the compiler-facing phase is live, the normal developer loop should be:

- `spio build`
- `spio run`
- `spio test`
- watch-mode variants for the same commands

without extra wrapper scripts, manual cache cleanup, or hidden compiler wiring.

### 4.3 Prefer Deterministic State Over Implicit Magic

The project should continue to favor:

- canonical manifest and lock write-back
- stable cache keys
- explicit output directories
- explicit workspace and target selection

This may feel stricter than some ecosystems, but it is what keeps large workspaces debuggable.

### 4.4 Optimize for Black-Box Verification

Every important cross-team feature should be testable against a released binary and a temporary project root.

If a capability cannot be validated through a black-box gate, it is not yet ready to anchor a split-repository workflow.

## 5. Priority Order

The next work should be sequenced in this order.

### 5.1 First Priority: Live Compiler Workflow

Goal:

- turn dry-run compile-plan generation into real non-dry-run `build/run/test`

Why first:

- this is the most important missing user-facing capability
- it is also the main blocker to delivering a truly fast development loop

Needs from `styio`:

- published `styio --compile-plan <path>`
- accurate `supported_contracts.compile_plan` advertisement
- stable diagnostics and output-directory behavior

`spio` work after that interface lands:

- build receipt writing
- compiler-result harvesting
- real non-dry-run execution
- black-box workflow gate activation

### 5.2 Second Priority: Fast Feedback Tooling

Goal:

- make local iteration feel immediate instead of batch-oriented

High-value features:

- `spio build --watch`
- `spio test --watch`
- cached incremental rebuilds keyed by compile-plan identity
- direct artifact reuse when graph, toolchain, and profile state have not changed

This is where `spio` should learn from Vite-like development ergonomics, even though it remains a package manager/orchestrator rather than a frontend dev server.

### 5.3 Third Priority: Registry Distribution and Trust

Goal:

- move from “can publish and consume through a shared static registry” to “can do it with stronger trust and operational discipline”

High-value features:

- remote registry auth and account policy on top of the existing static layout
- stronger checksum and trust policy
- registry-aware vendoring and offline guarantees
- install/resolve workflows that work equally against local and cloud-hosted repositories

Reasoning:

- the current registry path now has local and remote publish plus registry consumption, and the repository layout is already static-host friendly
- the next meaningful gap is no longer remote publication itself; it is trust, auth, and operational hardening

### 5.4 Fourth Priority: Reproducible Supply-Chain Hardening

Goal:

- strengthen CI and offline operation once registry consumption exists

High-value features:

- stronger checksum/index validation
- vendor consistency checks
- content-addressed source and artifact caches
- stricter `--locked` / `--offline` / `--frozen` behavior for all relevant workflows

### 5.5 Fifth Priority: Large-Workspace Ergonomics

Goal:

- make multi-package development fast without weakening graph discipline

High-value features:

- workspace filters
- target filters
- affected-package selection
- patch/override model for local development

## 6. Shared Responsibilities

### 6.1 Spio Owns

- manifest and lock semantics
- resolver policy
- compile-plan schema publication
- cache/layout rules on the package-manager side
- package-manager CLI surface
- black-box acceptance tooling for the compiler boundary

### 6.2 Styio Owns

- published compiler behavior
- handshake payload correctness
- compile-plan execution semantics on the compiler side
- diagnostics emitted by compiler workflows
- artifact-generation behavior inside declared output directories

### 6.3 Shared Accountability

Both teams are jointly responsible for:

- keeping the handoff spec accurate
- keeping fake compiler fixtures aligned with the published contract
- keeping black-box gates green against released binaries

## 7. What Styio Developers Need to Watch

`styio` developers do not need to follow every package-manager detail, but they do need to understand these direction changes because they will affect the external interface:

- compile-plan is moving from dry-run-only to live execution
- fast local development will require stable output and diagnostics behavior
- future build receipts and cache reuse depend on deterministic compiler outputs
- registry growth will increase the importance of reproducible diagnostics and artifact identity

In practice, the `styio` team should watch these documents together:

- [Styio-External-Interface-Requirement-Spec.md](../external/for-styio/Styio-External-Interface-Requirement-Spec.md)
- [Styio-Public-Interface-Roadmap.md](../external/for-styio/Styio-Public-Interface-Roadmap.md)
- [Spio-CLI-Contract.md](../governance/Spio-CLI-Contract.md)
- this document

## 8. Non-Goals for the Near Term

The project should not dilute effort into these before the higher-priority items above are complete:

- compiler-internal embedding APIs
- remote registry auth and account systems before the current trust and build/run/test priorities are in place
- overly flexible multi-version dependency semantics before the current single-version registry path is hardened
- convenience shortcuts that bypass compile-plan or handshake validation

## 9. Practical Definition of Success

This direction will be working when all of these are true:

1. A released `styio` binary passes the handoff gate and execute-path gates.
2. `spio build/run/test` work end-to-end against a published compiler without source-tree coupling.
3. The common local workflow feels fast enough that developers do not need ad hoc wrapper scripts.
4. Registry consumption exists with explicit and reproducible lock behavior.
5. Large workspaces can target only the packages they are actively touching.

## 10. Known Tradeoffs

- This direction is stricter than ecosystems that accept more ambiguity.
- It puts more upfront pressure on interface design and black-box validation.
- The payoff is a package manager that can stay fast and reliable as the language and ecosystem grow.
