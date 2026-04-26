# Styio Ecosystem File Governance Alignment Plan

**Purpose:** 作为 `styio-spio` 对三仓文件治理对齐计划的镜像入口，固定 `spio` 在文件治理、文档生命周期、repo hygiene 和脚本复用上的职责与本仓出口。

**Last updated:** 2026-04-17

**Authority:** The canonical copy lives at [`styio-nightly/docs/plans/Styio-Ecosystem-File-Governance-Alignment-Plan.md`](../../../../styio-nightly/docs/plans/Styio-Ecosystem-File-Governance-Alignment-Plan.md).

## `styio-spio` 的对齐目标

`spio` 需要从“工程门禁强、文档生命周期偏轻”对齐到 `nightly` 的治理水位：

1. 补齐 `docs/history/`、`docs/archive/`、`docs/rollups/`。
2. 引入 docs index / audit / lifecycle 检查。
3. 让 `Docs-Maintenance-Model`、verification matrix、repo hygiene、submit gate 与文档生命周期形成一套流程。

## 里程碑映射

| 里程碑 | `spio` 侧完成物 | 本仓主要落点 | 最低 gate |
|--------|------------------|--------------|-----------|
| `FG0` | 镜像计划、维护模型、runbook 接线 | `docs/planning/` `docs/governance/` `docs/teams/` | `repo-hygiene-check.py --repo-root . --mode tracked` |
| `FG1` | `history/archive/rollups` 补齐，docs index/audit/lifecycle 接入 | `docs/history/` `docs/archive/` `docs/rollups/` `scripts/` | `repo-hygiene-check.py` `submit-gate.py --profile pre-push` |
| `FG2` | 保持与 `view` 和 `nightly` 的目录职责对齐 | `docs/README.md` `docs/governance/Docs-Maintenance-Model.md` | docs + hygiene floor |
| `FG3` | 继续作为 shared baseline 参考仓，维持 required pattern、fixture negate 与 gate 语义和 `nightly` / `view` 同级 | `.gitignore` `scripts/repo-hygiene-check.py` `docs/operations/Spio-Verification-Matrix.md` | `repo-hygiene-check.py` `delivery-gate.py` `submit-gate.py` |
| `FG4` | 稳态治理 | 全仓治理入口 | full submit floor |

## 本仓规则

1. `spio` 不复制 `nightly` 的完整目录树，但必须复制其治理能力。
2. 文件治理变化优先更新本镜像，再更新 `Docs-Maintenance-Model.md`、`COORDINATION-RUNBOOK.md` 与相关 operations 文档。
3. 任何 ignore 规则变更都必须同时考虑 tracked fixture 的显式 negate rule。
4. `spio` 的 required pattern / negate-rule 模型是 shared baseline 参考之一；后续若 `nightly` / `view` 升级 gate 语义，应优先与这里保持等强，而不是各自分叉。
5. 新增 docs/file governance 脚本时，优先复用现有 `repo-hygiene-check.py`、`delivery-gate.py`、`submit-gate.py` 的接线方式。
