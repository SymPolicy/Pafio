# Evidence

## Source Inventory

- `docs/plan/repository-delivery-convergence/Evidence.md`: Styio Public Interface Roadmap for Pafio (signal lines captured: 3)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Planning Index (signal lines captured: 1)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Pafio Audit Backlog 2026-04-22 (signal lines captured: 12)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Pafio Bootstrap Checklist (signal lines captured: 1)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Pafio Future Direction and Styio Coordination (signal lines captured: 5)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Pafio Master Plan (signal lines captured: 6)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Pafio Native Target Split (signal lines captured: 5)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Pafio Stage Review and Future Features (signal lines captured: 12)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Pafio Styio Cloud Migration Handoff (signal lines captured: 1)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Pafio Workstreams and TODOs (signal lines captured: 5)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Planning Docs
- `docs/plan/repository-delivery-convergence/Evidence.md`: Styio Ecosystem Delivery Master Plan (signal lines captured: 4)
- `docs/plan/repository-delivery-convergence/Evidence.md`: Styio Ecosystem File Governance Alignment Plan

## Open Work Signals

- `docs/plan/repository-delivery-convergence/Evidence.md:3`: **Purpose:** Describe the exact compiler-facing interfaces that `pafio` needs from `styio`, so future `pafio` maintainers know what to request, test, and vendor without depending on compiler internals.
- `docs/plan/repository-delivery-convergence/Evidence.md:46`: `pafio` needs:
- `docs/plan/repository-delivery-convergence/Evidence.md:56`: - must not be guessed or reverse-engineered from compiler internals
- `docs/plan/repository-delivery-convergence/Evidence.md:13`: | `Pafio-Audit-Backlog-2026-04-22.md` | [Pafio Audit Backlog 2026-04-22](./Pafio-Audit-Backlog-2026-04-22.md) | Preserve the 2026-04-22 styio-pafio external audit findings as durable tracked work after removing the ignor
- `docs/plan/repository-delivery-convergence/Evidence.md:1`: # Pafio Audit Backlog 2026-04-22
- `docs/plan/repository-delivery-convergence/Evidence.md:9`: This backlog migrates the ignored `docs/audit/defects/STYIO-PAFIO-2026-04-22.md` record into tracked planning ownership. The migration does not claim unresolved defects are fixed. It gives each finding a stable identifie
- `docs/plan/repository-delivery-convergence/Evidence.md:15`: 3. Closure evidence must name the code change, regression coverage, data or resource lifecycle proof, and gate command.
- `docs/plan/repository-delivery-convergence/Evidence.md:16`: 4. If a finding moves to a GitHub issue or downstream platform task, record the durable link before removing it from this backlog.
- `docs/plan/repository-delivery-convergence/Evidence.md:17`: 5. Run `./scripts/delivery-gate.sh --mode checkpoint` and external `styio-audit gate --repo /home/unka/styio-pafio --project styio-pafio` before declaring the backlog migration complete.
- `docs/plan/repository-delivery-convergence/Evidence.md:19`: ## Backlog
- `docs/plan/repository-delivery-convergence/Evidence.md:26`: | PAFIO-AUD-004 | Fixed for public client root pinning, residual tracked | Registry / Publish | Registry verification still needs an external trust anchor for non-local registries instead of trusting only metadata fetche
- `docs/plan/repository-delivery-convergence/Evidence.md:28`: | PAFIO-AUD-006 | Tracked | Registry / Publish | Archive manifest discovery needs per-member size limits in addition to canonical member selection. | Reject oversized candidate manifests before reading them into memory;
- `docs/plan/repository-delivery-convergence/Evidence.md:30`: | PAFIO-AUD-008 | Partially fixed, residual tracked | Registry / Publish | Native registry fetch validates shape and hashes but does not yet enforce registry v2 trust-chain parity with the Python verifier. | Native remot
- `docs/plan/repository-delivery-convergence/Evidence.md:31`: | PAFIO-AUD-009 | Tracked | Registry / Publish | Native registry checkout needs pre-extraction tar entry validation. | Pre-scan and reject absolute paths, `..` segments, links, and special files before extraction; add ma
- `docs/plan/repository-delivery-convergence/Evidence.md:33`: | PAFIO-AUD-011 | Tracked | Registry / Publish | Registry v2 trust policy still needs threshold, expiration, freeze, and monotonic-version enforcement. | Enforce thresholds, expiry, and monotonic metadata versions; add e
- `docs/plan/repository-delivery-convergence/Evidence.md:34`: | PAFIO-AUD-012 | Fixed for scoped runtime paths, residual tracked | Core / Workflow | Runtime subprocesses now have scoped wall-clock timeouts, but timeout configurability and curl low-speed controls remain residual wor
- `docs/plan/repository-delivery-convergence/Evidence.md:9`: The bootstrap stage is considered prepared when these areas all have an owner, a TODO list, and a gate:
- `docs/plan/repository-delivery-convergence/Evidence.md:20`: That creates a new problem: the project no longer needs only phase checklists. It needs a shared direction document that explains what kind of package manager it is trying to become and which cross-team interfaces must m
- `docs/plan/repository-delivery-convergence/Evidence.md:68`: `pafio` and `styio` must continue to integrate only through:
- `docs/plan/repository-delivery-convergence/Evidence.md:103`: If a capability cannot be validated through a black-box gate, it is not yet ready to anchor a split-repository workflow.
- `docs/plan/repository-delivery-convergence/Evidence.md:120`: Needs from `styio`:
- `docs/plan/repository-delivery-convergence/Evidence.md:164`: - the next meaningful gap is no longer remote publication itself; it is trust, auth, and operational hardening
- `docs/plan/repository-delivery-convergence/Evidence.md:30`: - `pafio` must remain movable as a self-contained subtree and later as its own repository.
- `docs/plan/repository-delivery-convergence/Evidence.md:33`: - `pafio` must only assume compile-plan support for versions that `styio` advertises and the compatibility matrix enables.
- `docs/plan/repository-delivery-convergence/Evidence.md:34`: - source, cache, build output, test temp data, and integration fixtures must remain isolated.
- `docs/plan/repository-delivery-convergence/Evidence.md:147`: - this phase depends on `styio` publishing a real consumer; `pafio` must not guess ahead
- `docs/plan/repository-delivery-convergence/Evidence.md:190`: - local filesystem and anonymous HTTP registry transport are already live; remaining work is concentrated in auth, trust hardening, and higher-scale deployment models
- `docs/plan/repository-delivery-convergence/Evidence.md:216`: Each workstream must end in a gate that can be run without hidden local state.
- `docs/plan/repository-delivery-convergence/Evidence.md:32`: 2. `PafioCLI/CLI.cpp` remains a routing shell and must not become a second service layer.
- `docs/plan/repository-delivery-convergence/Evidence.md:33`: 3. Future hosted/control-plane binaries should compose from backend/service targets and must not depend on `pafio_cli_shell` unless they intentionally expose CLI behavior.
- `docs/plan/repository-delivery-convergence/Evidence.md:34`: 4. `frontend/console/` must stay decoupled from `src/` implementation details and consume published contracts instead of direct native coupling.
- `docs/plan/repository-delivery-convergence/Evidence.md:35`: 5. If a new domain needs public machine payloads, the owning contract still lives under `contracts/` or `docs/governance/`, not inside this planning note.
- `docs/plan/repository-delivery-convergence/Evidence.md:39`: This split is not yet a process-level service decomposition. It is the native build-graph prerequisite for that next step:
- `docs/plan/repository-delivery-convergence/Evidence.md:102`: - same package name must resolve to one effective version and one effective source fingerprint
- `docs/plan/repository-delivery-convergence/Evidence.md:190`: - [ADR-0013](../adr/ADR-0013-phase4-run-dry-run-and-test-gap.md)
- `docs/plan/repository-delivery-convergence/Evidence.md:286`: ## 4. What Is Still Partial or Blocked
- `docs/plan/repository-delivery-convergence/Evidence.md:292`: - non-dry-run execution must keep producing `receipt.json` and output-root materialization evidence.
- `docs/plan/repository-delivery-convergence/Evidence.md:302`: - they do not yet submit remote jobs or talk to a queue or worker pool.
- `docs/plan/repository-delivery-convergence/Evidence.md:336`: ### 5.3 Manifest Rules Must Be Frozen Before Resolver Work Grows
- `docs/plan/repository-delivery-convergence/Evidence.md:364`: ### 5.5 Pinned Git Needs Hermetic Snapshots, Not Host-Leaking Paths
- `docs/plan/repository-delivery-convergence/Evidence.md:372`: - pinned sources must be materialized into immutable snapshots, and transitive path traversal must remain inside that snapshot boundary
- `docs/plan/repository-delivery-convergence/Evidence.md:428`: - install and activation must be split
- `docs/plan/repository-delivery-convergence/Evidence.md:460`: ### 6.2 rustup and Go: Toolchain Selection Must Be a Real Dependency Axis
- `docs/plan/repository-delivery-convergence/Evidence.md:509`: - `pafio` already has a workspace model and a hermetic home directory; a content-addressed store is a natural next step
- `docs/plan/repository-delivery-convergence/Evidence.md:605`: - current hermetic directories are correct, but they are not yet fully content-addressed
- `docs/plan/repository-delivery-convergence/Evidence.md:27`: `pafio` must keep an offline path. If the required package graph is
- `docs/plan/repository-delivery-convergence/Evidence.md:217`: - largest external dependency; work here must wait for compiler publication rather than local assumptions
- `docs/plan/repository-delivery-convergence/Evidence.md:272`: - extractability and compatibility checks must already exist
- `docs/plan/repository-delivery-convergence/Evidence.md:288`: Before declaring the planning stage complete, every workstream must have:
- `docs/plan/repository-delivery-convergence/Evidence.md:302`: ### Open / Not Yet Closed
- `docs/plan/repository-delivery-convergence/Evidence.md:304`: - **A, B, C, D, E, F, G, H** remain open for full stage closure unless the stream-specific TODO list is fully converted to verified acceptance items.
- `docs/plan/repository-delivery-convergence/Evidence.md:34`: 1. Any change to compiler compatibility or machine-info assumptions must be reviewed by Compat / Security.
- `docs/plan/repository-delivery-convergence/Evidence.md:35`: 2. Any new project graph or workflow payload must be mirrored into `styio-view` handoff docs and fixtures in the same checkpoint.
- `docs/plan/repository-delivery-convergence/Evidence.md:37`: 4. If `styio` has not published a capability, `pafio` must return a machine-readable contract error instead of guessing.
- `docs/plan/repository-delivery-convergence/Evidence.md:38`: 5. Any cross-repo milestone, repo exit, or checkpoint-ID change must first land in the authoritative nightly plan, then in this mirror, then in local owner docs.

## Current State Summary

- Imported planning source count: 13
- Extracted signal count: 55
- Better Plan root: `docs/plan`
- Repository: `SymPolicy/Pafio`

This evidence is derived from repository files that existed before consolidation. It intentionally records the current final planning structure instead of preserving old planning-root names as active navigation.
