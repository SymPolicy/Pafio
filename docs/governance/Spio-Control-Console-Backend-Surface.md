# Spio Control Console Backend Surface

**Purpose:** Freeze every backend interaction that the repo-hosted `spio` control console is allowed to perform so frontend and backend teams can ship independently against published contracts.

**Last updated:** 2026-04-21

## Source Of Truth

The control console is a consumer of two backend contract families:

1. Hosted control-plane native JSON package
   - [`../../contracts/hosted-control-plane/v1/hosted-control-plane.contract.json`](../../contracts/hosted-control-plane/v1/hosted-control-plane.contract.json)
   - [`../../contracts/hosted-control-plane/v1/hosted-control-plane.examples.json`](../../contracts/hosted-control-plane/v1/hosted-control-plane.examples.json)
   - [`./Spio-Hosted-Control-Plane-Contract.md`](./Spio-Hosted-Control-Plane-Contract.md)
2. Registry `v2` read/control contracts
   - [`../registry/Spio-Registry-V2-Protocol.md`](../registry/Spio-Registry-V2-Protocol.md)
   - [`../registry/Spio-Registry-Client-Contract.md`](../registry/Spio-Registry-Client-Contract.md)
   - [`../registry/Spio-Registry-Control-Plane-Contract.md`](../registry/Spio-Registry-Control-Plane-Contract.md)

This document is the control-console consumer map. It does not replace the backend-owned contracts above.

## Frontend Surface Mapping

| Control-console surface | Backend contract | Operation or path family | Notes |
|-------------------------|------------------|---------------------------|-------|
| workspace bootstrap | hosted control-plane | `openWorkspace` | Starts or resumes a hosted workspace session. |
| workspace/project refresh | hosted control-plane | `projectGraph` | Refreshes hosted graph, toolchain, distribution, and source-state views. |
| managed compiler install | hosted control-plane | `toolInstall` | Operator install action for managed compilers. |
| managed compiler activate | hosted control-plane | `toolUse` | Switches active compiler for the workspace. |
| project compiler pin | hosted control-plane | `toolPin` | Persists project-local compiler version. |
| clear project compiler pin | hosted control-plane | `toolClearPin` | Removes project-local compiler pin. |
| dependency fetch | hosted control-plane | `fetchDependencies` | Refreshes hosted dependency cache/materialization state. |
| dependency vendor | hosted control-plane | `vendorDependencies` | Produces vendor output at an explicit path. |
| execution run lane | hosted control-plane | `runWorkflow` | Returns diagnostics plus `runtime_events` for console execution panels. |
| execution build lane | hosted control-plane | `buildWorkflow` | Returns diagnostics plus `runtime_events` for build summaries. |
| execution test lane | hosted control-plane | `testWorkflow` | Returns diagnostics plus `runtime_events` for test summaries. |
| pack artifact | hosted control-plane | `packProject` | Produces archive metadata for console deployment cards. |
| publish preflight | hosted control-plane | `preparePublish` | Produces preflight result before registry publication. |
| publish artifact | hosted control-plane | `publishToRegistry` | Publishes an artifact to the selected registry root. |
| registry config visibility | registry `v2` protocol | `GET /config.json` | Reads registry protocol/version/capability metadata from the read root. |
| namespace targets visibility | registry `v2` protocol | `GET /trust/targets/<namespace>.json` | Reads signed package/index pointers for the namespace. |
| package index visibility | registry `v2` protocol | `GET /index/<namespace>/<name>.jsonl` | Reads append-only release records from the read root. |
| source artifact visibility | registry `v2` protocol | `HEAD /artifacts/source/sha256/<xx>/<yy>/<sha256>.spio.src.tar` | Optional operator probe for immutable published source artifacts. |
| publish transport visibility | registry control-plane contract | `POST /api/spio-registry-control/v1/publish`, `POST /api/spio-registry-control/v1/verify` | Write-side backend capability; surfaced in the console as backend status, not implemented in frontend logic. |

`RuntimeEventAdapter` for the console consumes `runtime_events` embedded in the execution envelopes. `v1` does not define a separate live event stream route.

## Workflow Entry Points

When console work spans multiple backend calls, use:

1. `hosted-control-plane.contract.json` for routes, schemas, examples, and operation identifiers
2. `hosted-control-plane.examples.json` for end-to-end operator payloads
3. registry `v2` protocol/control-plane contracts for static read-root and publish orchestration semantics

## Control-Console Rules

1. The console must not call undocumented routes or scrape backend backing-store paths.
2. The console must not parse manifests, resolve dependencies, or emulate publish orchestration locally.
3. Registry visibility must come from published `v2` config/targets/index/artifact paths, not from repository internals.
4. Hosted workspace/toolchain/dependency/execution/deployment interactions must match the versioned hosted contract package exactly.
5. Any new control-console workflow that becomes product-critical must be added to the native JSON contract package before frontend and backend teams rely on it.
