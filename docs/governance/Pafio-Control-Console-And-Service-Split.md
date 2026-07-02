# Pafio Control Console And Service Split

**Purpose:** Freeze the product cut between the repo-hosted control-console frontend and the service-side backend planes so UI work does not leak into native core, registry hosting, or cloud-platform internals.

**Last updated:** 2026-04-24

## Scope

This document owns:

- the human-facing control-surface boundary for `pafio`
- the repository-layout expectation for frontend assets versus backend code
- the split between package-manager client surfaces and `pafio` backend planes
- the rule that frontend code consumes published backend contracts only
- the requirement that control-console consumer mappings stay documented

This document does not own:

- CLI grammar
- registry repository layout details
- private auth, account, or trust policy implementations

Those remain in the existing governance, registry, and security documents.

## 1. Frontend: Repo-Hosted Control Console

The control-console frontend is the human-facing management surface for:

- workspace inventory and lifecycle
- registry status and publish visibility
- toolchain install/use/pin status
- dependency and deployment progress
- execution lane and worker-pool summaries

Repository placement:

- tracked frontend assets live under `frontend/console/`
- the console may be hosted from a repository-backed static site or CDN

Non-negotiable rules:

- the console talks to backend surfaces through published HTTP/JSON contracts only
- the console must not link native `C++` code, scrape `PAFIO_HOME`, or read registry backing-store paths directly
- the console must not absorb dependency resolution, manifest parsing, publish orchestration, or worker-scheduling logic

## 2. Backend Plane A: Repository Hosting

The repository-hosting backend owns:

- immutable package objects and registry `v2` metadata
- read-root versus write-root separation
- publish control-plane and promotion flows
- package repository serving over local paths or HTTP(S)

This plane is now service-side `pafio` ownership. `pafio` keeps package-manager client compatibility docs and local test shims while server implementations move downstream.

Authoritative references:

- [../registry/Pafio-Registry-V2-Protocol.md](../registry/Pafio-Registry-V2-Protocol.md)
- [../registry/Pafio-Registry-Client-Contract.md](../registry/Pafio-Registry-Client-Contract.md)
- [../registry/Pafio-Registry-Control-Plane-Contract.md](../registry/Pafio-Registry-Control-Plane-Contract.md)
- [../registry/Pafio-Registry-V2-Publish-Control-Plane.md](../registry/Pafio-Registry-V2-Publish-Control-Plane.md)
- [Pafio Control Console Backend Surface](./Pafio-Control-Console-Backend-Surface.md)

## 3. Backend Plane B: Cloud Platform

The `pafio` cloud backend owns:

- hosted workspace lifecycle
- project-graph publication
- managed toolchain operations
- dependency fetch and vendor workflows
- execution, runtime-event, and deployment workflows
- future queue, scheduler, and worker-pool orchestration

`pafio` freezes this plane as local machine-readable client compatibility and hosted-client expectations. `pafio` owns remote services and must preserve the existing route families and payload nouns used by current consumers:

- `POST /workspaces/open`
- `GET /workspaces/{id}/project-graph`
- `POST /workspaces/{id}/tool/*`
- `POST /workspaces/{id}/dependencies/*`
- `POST /workspaces/{id}/execution/*`
- `POST /workspaces/{id}/deployment/*`

Authoritative references:

- [Pafio Hosted Control-Plane Contract](./Pafio-Hosted-Control-Plane-Contract.md)
- [Pafio Control Console Backend Surface](./Pafio-Control-Console-Backend-Surface.md)

## 4. Native Core Position

`src/` remains the package-manager core and local compatibility renderer for:

- CLI automation
- registry publish and consume client behavior
- toolchain state and project graph payloads
- cloud policy rendering for platform compatibility
- workflow orchestration

Rules:

- the native CLI is an admin and machine surface, not the human control console
- backend payloads must stay domain-shaped and machine-readable, not page-specific view models
- private auth and account policy remain behind `src-private/` and [../security/Pafio-Private-Security-Module-Contract.md](../security/Pafio-Private-Security-Module-Contract.md)

## 5. Cross-Plane Rules

- frontend and backend may share vocabulary, but they do not share implementation
- repository hosting and cloud platform may share security policy and deployment ownership inside `pafio`, but `pafio` should consume them through contracts
- `styio-view` may consume the same backend planes through product-owned adapters; `pafio` must not fork backend semantics to suit a single frontend
- the control console must keep a consumer map of every backend interaction instead of relying on page-local assumptions
