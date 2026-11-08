# Pafio 薄包管理核心 — 证据与取舍

**Purpose:** 保存已完成可信依赖内核的实现证据与设计取舍。

**Plan:** `pafio-thin-core`  
**Last updated:** 2026-07-28

## 1. 仓库现状

### 已经可复用的包管理核心

| 能力 | 当前证据 |
|---|---|
| manifest / lock | `src/PafioManifest/Manifest.*`, `src/PafioManifest/Lockfile.*` |
| 确定性图解析 | `src/PafioResolve/Resolver.*` |
| registry 只读客户端 | `src/PafioRegistryClient/Client.*` |
| TUF 与制品安全 | `src/PafioSecurity/` |
| 文件锁与原子写 | `src/PafioCore/FileLock.*`, `src/PafioCore/AtomicFile.*` |
| add/remove/sync | `src/PafioWorkflow/Dependencies.*`, `src/PafioApp/PackageApp.*` |
| 现有针对性测试 | `tests/native/ManifestTests.cpp`, `LockTests.cpp`, `SyncTests.cpp`, `SecurityTests.cpp`, `TufTests.cpp` |

`docs/plan/package-manager-roadmap/Checkpoints.json` 已记录 TUF、归档预扫描、trust descriptor、绝对
子进程路径、文件锁/原子写和冲突诊断等节点完成。这些成果应被薄核心复用，不应因为缩小产品边界
而重写。

### 当前膨胀面

`src/PafioCLI/CLI.cpp` 当前路由了 new/init/doctor/project-graph/cloud/install/use/set/check、
add/remove/sync/fetch/build/run/test/lock/tree/vendor/pack/publish/registry/tool 等二十余个入口。

源码的主要体积也集中在超出薄核心的领域：

| 领域 | 代表文件 |
|---|---|
| 编译工作流与 compile-plan | `src/PafioApp/WorkflowApp.cpp`, `src/PafioPlan/CompilePlan.cpp` |
| 编译器安装与 toolchain | `src/PafioApp/ToolApp.cpp`, `src/PafioTool/`, `src/PafioToolchain/` |
| 云策略与任务 | `src/PafioApp/CloudApp.cpp`, `src/PafioCloud/`, `src/pafio_cloud_stress/` |
| IDE/Vityo 聚合载荷 | `src/PafioResolve/ProjectGraphContract.cpp` |
| registry 服务端/控制面 | `src/pafio_registry_v2/`, `scripts/registry-v2-control-plane-server.py`, hosted/control-plane contracts |

`ProjectGraphContract.cpp` 直接依赖 cloud、tool、toolchain 和 lock 状态，说明所谓“project graph”
已不再是包图，而是跨产品聚合对象。继续完成原路线中的 workflow receipts、Vityo payloads 和
platform policy 会加深这一耦合。

### 当前求解与缓存事实

- `Resolver.cpp` 使用按 alias 排序的 DFS、source fingerprint 去重和包名索引，适合精确版本图；
- 循环和版本/来源冲突已经能输出依赖链；
- registry blob 已按 SHA-256 布局并在获取时校验；
- Git cache 仍以 FNV 快捷键组织，证明 Git 来源会额外引入身份、进程、归档和跨平台复杂度；
- `docs/governance/Pafio-Manifest-and-Lock-Conventions.md` 已冻结精确版本与 canonical lock 输出。

因此最快路线是把已经工作的 path + registry 消费链收成一个事务，而不是先替换求解器或继续
增加工作流命令。

## 2. 优秀开源实现对照

### Cargo

Cargo 把解析结果固定到 lockfile；普通操作尽量复用锁定版本，`--locked` 禁止改锁，
`--offline` 禁止联网。可借鉴的是 lock-first 行为与显式可复现模式，不是把 Cargo 的 build、
feature 和多来源复杂度整体复制进 Pafio。

参考：

- https://doc.rust-lang.org/cargo/reference/resolver.html
- https://doc.rust-lang.org/cargo/commands/cargo-generate-lockfile.html
- https://docs.rs/cargo/latest/src/cargo/core/resolver/mod.rs.html

### Go Modules

Go 的 Minimal Version Selection 以“最低要求 + 单调选择”换取简单、确定和可解释的求解过程，
避免通用区间求解的回溯复杂度。它要求生态把破坏性 major 版本视为新模块身份。这个模型适合作为
Styio 有真实多版本数据后的首选，而不是首个闭环的前置条件。

参考：

- https://go.dev/ref/mod#minimal-version-selection
- https://go.dev/src/cmd/go/internal/mvs/

### Dart Pub

Pub 将 lockfile 与本机生成的 `.dart_tool/package_config.json` 分开：前者可提交并记录精确解，
后者把包名映射到本机缓存 URI。Pafio 的 `resolution-v1` 采用这一职责分离，避免让 Styio 读取
Pafio 私有缓存布局或把绝对路径写入 lockfile。

参考：

- https://dart.dev/tools/pub/cmd/pub-get
- https://dart.dev/tools/pub/packages
- https://github.com/dart-lang/pub/blob/master/lib/src/solver/version_solver.dart

PubGrub 的 incompatibility、unit propagation 和 conflict-driven backjumping 能给出优秀冲突
说明，但实现与维护成本明显高于当前生态所需，因此只作为 MVS 不适用时的后续候选。

### pnpm

pnpm 证明全局内容寻址存储和项目侧轻量映射可以同时降低重复 IO 与磁盘占用。Pafio 只借鉴
digest identity、一次存储和链接/直接引用思想，不复制 Node 的 peer dependency 和
`node_modules` 布局复杂度。

参考：

- https://github.com/pnpm/pnpm

## 3. 方案比较

| 方案 | 首次闭环 | 长期复杂度 | 诊断 | 结论 |
|---|---:|---:|---:|---|
| 保留精确版本 DFS | 最快 | 低 | 当前已可用 | 首个闭环采用 |
| 立即实现 Cargo 式范围回溯 | 慢 | 高 | 需大量边界启发式 | 拒绝 |
| 立即实现 PubGrub | 慢 | 中高 | 最好 | 缺少真实 corpus，延后 |
| 满足兼容前提后采用 MVS | 中 | 低 | 简单可解释 | 长期首选 |

## 4. 关键判断

1. Pafio 的核心价值不是命令数量，而是稳定地产生一个可信包图。
2. 安全校验、原子写和并发互斥属于可信包图，不是平台功能。
3. build/run/test 与 toolchain 管理即使用户体验相关，也不属于包解析/物化内核。
4. `resolution-v1` 是 Pafio 与 Styio 唯一需要共同冻结的窄接口。
5. 先闭环消费者，再做 pack/publish；没有稳定消费者时，扩展 registry 控制面只会制造第二条
   产品主线。
6. 现有 Git 来源不再扩展；共享依赖走不可变 registry，本地开发走 path，能显著缩小缓存身份与
   跨平台进程面。
