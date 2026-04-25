# Styio Ecosystem Delivery Master Plan

**Purpose:** 作为 `styio-spio` 对三仓统一交付总纲的镜像入口，固定 `spio` 在每个里程碑中的职责、文档落点和本仓 gate。

**Last updated:** 2026-04-17

**Authority:** The canonical copy lives at [`styio-nightly/docs/plans/Styio-Ecosystem-Delivery-Master-Plan.md`](../../../../styio-nightly/docs/plans/Styio-Ecosystem-Delivery-Master-Plan.md).

## `styio-spio` 的长期职责

在统一产品里，`styio-spio` 持续负责：

1. manifest and lockfile schema
2. resolver, cache, and vendor workflow
3. build/run/test orchestration
4. tool install / use / pin / switch lifecycle
5. project graph payloads
6. toolchain and registry/package state payloads

## 里程碑映射

| 里程碑 | `spio` 侧完成物 | 本仓权威文档 | 本仓最低 gate |
|--------|------------------|--------------|---------------|
| `M0` | 镜像总纲、维护模型、协调 runbook、verification matrix 接线 | `docs/planning/Styio-Ecosystem-Delivery-Master-Plan.md` `docs/governance/Docs-Maintenance-Model.md` | `repo-hygiene-check.py` `submit-gate.py --profile pre-push` |
| `M1` | compat matrix、`machine-info`/`compile-plan` round-trip、compiler failure payload | `docs/styio/Styio-External-Interface-Requirement-Spec.md` | `styio_contract_compat_gate` `styio_compile_plan_contract_gate` |
| `M2` | manifest/lock、resolver/cache、fetch/vendor、build/run/test、pack/publish、tool lifecycle live | `docs/governance/Spio-CLI-Contract.md` `docs/operations/Spio-Verification-Matrix.md` | `spio_cli_gate` `spio_manifest_lock_gate` `spio_workflow_gate` `spio_registry_server_gate` |
| `M3` | `project_graph`、`toolchain_state`、`source_state`、deploy preflight 成为 `view` 的正式消费接口 | `docs/governance/Spio-Entry-Argument-Index.md` `docs/styio/` `docs/for-spio` consumers | `submit-gate.py --profile pre-push` + cross-repo fixtures |
| `M4` | module/distribution/agent-support payload 与 registry/deploy 深化 | `docs/registry/` `docs/teams/` `docs/planning/Spio-Workstreams-and-TODOs.md` | distribution/registry/toolchain gates |
| `M5` | hosted/cloud/mobile support 所需 environment/distribution contract | `docs/registry/` `docs/operations/` | hosted/distribution fixtures |
| `M6` | split-ready、release-grade package manager、sample matrix hardening | `docs/planning/Spio-Master-Plan.md` `docs/operations/Spio-Verification-Matrix.md` | full submit/release floor |

## `styio-spio` Checkpoint Rules

1. Any change to compiler compatibility or machine-info assumptions must be reviewed by Compat / Security.
2. Any new project graph or workflow payload must be mirrored into `styio-view` handoff docs and fixtures in the same checkpoint.
3. `spio` may not infer unpublished compiler behavior when `styio --machine-info=json` or the published compatibility matrix can answer the question.
4. If `styio` has not published a capability, `spio` must return a machine-readable contract error instead of guessing.
5. Any cross-repo milestone, repo exit, or checkpoint-ID change must first land in the authoritative nightly plan, then in this mirror, then in local owner docs.

## 本仓优先顺序

当前 `spio` 的推进顺序固定为：

1. compiler handshake and compat policy
2. live workflow closure
3. project graph and environment payload publication
4. registry/deploy lifecycle depth
