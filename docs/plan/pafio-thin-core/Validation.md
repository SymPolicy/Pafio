# Pafio 薄包管理核心 — 冻结验收合同（可信依赖内核）

**Purpose:** 保存 Pafio 内部可信依赖内核已通过的冻结验收合同。

**Last updated:** 2026-07-30

**Plan:** `pafio-thin-core`  
**Node:** `0861f893-f534-42a9-99d1-05480d9fe05b`  
**Acceptance dispatch:** `c9d208e7-5670-423b-8c8c-c25d3f60df94`  
**Frozen:** 2026-07-28

## 1. 场景边界

`sync` 形成 `pafio.lock` 与 `.pafio/resolution-v1.json`。  
第二次执行（含服务关闭后的 `--locked --offline`）重放相同锁定状态，不产生字节漂移。  

`sync`/CAS/resolution 内核是本次交付边界，不包含 build/run/test、toolchain、cloud、hosted control-plane、
Vityo、pack 或 publish 客户端行为。

## 2. 合同字节约束

- resolution 路径固定为 manifest 同目录的 `.pafio/resolution-v1.json`。  
- `pafio.lock` 与 resolution 均需 canonical UTF-8、两空格、末尾 LF。  
- 根路径与 package 列表是 deterministic 排序输出。  
- `manifest_sha256` 与 `lock_sha256` 均基于提交字节。  
- `path/workspace` 的 `content_sha256` 为 JSON `null`，registry 依赖使用真实 SHA-256 摘要。  
- 解析只读取 `schema_version`、`manifest_sha256`、`lock_sha256`、`roots`、`packages` 等字段。

## 3. 回归映射（内部核验）

| Case | 目标 |
|---|---|
| AC-01 | canonical serializer 稳定性（同等输入顺序变化，bytes 不变） |
| AC-02 | invalid graph 拒绝与专属错误码 |
| AC-03 | `--locked --offline` 复现与字节不变 |
| AC-04 | Styio 消费者最小读取路径验证 |
| AC-05 | 双进程 single-flight 下载行为 |
| AC-06 | 离线保活下不改状态 |
| AC-07 | 损坏对象离线失败、在线可恢复 |
| AC-08 | stale/形状错误触发 fail-closed |
| AC-09 | resolver/security 现有能力继承 |
| AC-10 | boundary audit 不含 cloud/toolchain/workflow/service 字段 |

## 4. 现有可复用证据

`tests/native` 与 `tests/interop` 的现有合同文件用于 AC-01/AC-09 复用证据；本节点不新增新测试设施。

## 5. 已收敛

1. `path/workspace content_sha256` 固定为 JSON `null`。  
2. prepare/commit 失败不更改 lock/resolution 字节，lock 与 resolution 两阶段提交不回滚，但靠 `lock_stale` fail-closed。  

剩余设计缺口：无。
