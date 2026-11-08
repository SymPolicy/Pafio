# Pafio 薄包管理核心 — 需求（可信依赖内核）

**Purpose:** 冻结 Pafio 内部可信依赖内核的已完成需求边界。

**Plan:** `pafio-thin-core`  
**Last updated:** 2026-07-30

## 1. 核心目标

Pafio 负责把 `pafio.toml + pafio.lock + registry` 变成可信可复现的本地包图：

```text
manifest + existing lock + registry metadata
    -> resolve
    -> verify/materialize
    -> canonical lock + local resolution
```

Pafio 不编译 Styio，不执行测试，不管理 Styio 安装，不调度云任务，不承载 registry 平台端，只提供内部可信依赖内核所需的数据和边界。

## 2. 一次可独立验收能力

给定合法项目 manifest，执行 `pafio sync` 后：

1. 依赖图按 lock-first 方式确定性解析；  
2. lockfile 包含完整传递依赖版本与来源；  
3. 缺失制品下载并经过 SHA-256 校验后写入内容可寻址缓存；  
4. 写入 `.pafio/resolution-v1.json`，只包含 Styio 所需最小 package 映射；  
5. 再次执行 `--locked --offline` 复用 lock/resolution，不改动状态；  
6. Styio 只消费 resolution，不反向读取 Pafio 缓存/计划/工具链状态。

## 3. 功能范围

### REQ-THIN-001 同步内核闭环

`sync` 是唯一写依赖状态路径：解析、校验、物化、锁与 resolution 原子写入。  
`add`/`remove` 仅是 manifest 编辑后触发 `sync`。

### REQ-THIN-002 lock-first 精确解析

`pafio.lock` 复用为默认源；新增、删除、变更边才允许变更版本决策。  
输出必须稳定、可复现，复杂度应控制在 `O(V + E)`。

### REQ-THIN-003 验证型内容寻址缓存

下载、校验、解包、原子 rename 与并发互斥按 digest 组织，避免重复对象。  
单摘要并发仅一次获取，多摘要可并发。

### REQ-THIN-004 resolution-v1 最小合同

文件仅包含 `schema_version/manfiest_sha256/lock_sha256/roots/packages` 与
`package.id/root/content_sha256/dependencies`。  
禁止出现 target、profile、cloud policy、toolchain、IDE/输出目录、receipt 等字段；  
path/workspace 的 `content_sha256` 为 `null`。

### REQ-THIN-005 严格边界

首个闭环不新增 build/run/test/check、toolchain、cloud、hosted control-plane 或 pack/publish
内核行为。相关旧表面保持收敛清单待移除，不在本节点新增开发。
