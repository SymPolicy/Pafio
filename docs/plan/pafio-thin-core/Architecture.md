# Pafio 薄包管理核心 — 架构（可信依赖内核）

**Purpose:** 记录 Pafio 内部可信依赖内核的确定性解析、CAS 与提交架构。

**Plan:** `pafio-thin-core`  
**Last updated:** 2026-07-30

## 1. 内核流水线

```text
Manifest (pafio.toml)
  │ parse + canonical digest
  ▼
Lock-aware exact resolver ──> immutable ResolvedGraph
  │                              │
  │ missing descriptor            │ canonical lock
  ▼                              ▼
Registry reader + verifier       Atomic lock write
  │
  ▼
CAS store + bounded materializer ──> Atomic resolution-v1 write
```

核心只保留五个领域：Manifest/Lock、Resolver、Registry Reader、Store、Resolution Contract。

## 2. 数据与约束（面向性能）

- 以 `unordered_map` 做按名去重与 O(1) 检索，输出前再排序以获得稳定序列化。  
- 每图节点使用按 alias 排序的固定向量边列表。  
- 解析/验证通过 DFS color + 边状态给出循环与冲突路径。  
- `O(V + E)` 的精确版本解析，不引入 SAT/PubGrub 全量候选集。  
- 只缓存经过验证内容摘要，避免重复解包与重复哈希扫描。

## 3. 同步事务

`sync` 分为：

1. Prepare：解析、差异计算、待取 digest 归集、hash 校验计划；  
2. Commit：原子 lock 提交 -> resolution 提交；  
3. 失败回退保持旧字节，不产出新 resolution。

Resolution 必须是最后提交项，确保消费者能通过 `lock_sha256` 做 fail-closed。

## 4. Store 与缓存

- 缓存路径按摘要划分，不以版本名/path/FNV/mtime 为身份。  
- 每个摘要下载、解包使用单飞（single-flight）互斥。  
- 热同步下只验证标记与摘要，不重复解包。  
- 不做 GC/GC-like 功能。

## 5. 收敛顺序

当前只交付薄核心闭环（A）。  
A 完成后停止，不自动进入后续闭环，等待下一次独立迁移决策。
