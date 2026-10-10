# 天数智芯 · PRODMRDEV-9416 实施计划

> **依据**:`FlagCX厂商适配（v3）.pdf`(29 页,下称【文档】)+ 本仓库源码
> **代码基线**:**HEAD `ff5f80c`**(已复核;原分析基线为 `250ee84`,差异见 **§0.4**)
> **读者**:天数是首次接触 FlagCX Device API 的工程师(初学者友好,概念处均有注解)
> **标注约定**:
> - 【文档】= v3 PDF 明文规定,附节号(如 §4.2)
> - 【代码】= 从本仓库源码核实,附 `文件:行号`,可自行点开核对
> - 【推断】= 我的推理,**未经实证**,需你在真机确认
> - 【建议】= 我的工程意见,不是文档要求
> - ✅ 已核实为真 / ❌ 与现状不符 / ⚠️ 有风险
>
> **文档结构**:第 0-2 节是全局;第 3-4 节是排期与分工;第 5 节是逐 subtask 详述(主体);第 6-7 节是验收与交付;**第 8 节是决策记录(6 条已拍定)**;第 9 节是风险。

---

## 📍 快速定位索引

> **怎么用**:`Ctrl+F` 搜下面的**唯一串**即可直达(比行号可靠 —— 行号会随编辑漂移,搜索串不会)。
> VS Code 里也可以用 `Ctrl+Shift+O` 打开**大纲**,按标题跳转。
> 文中所有 ID(`M1`/`D3`/`N2`/`A1`…)**本身就是稳定锚点**,直接搜 ID 也行。

**做 S1 只需要这 10 处**

| 你要找什么 | 在文档里搜这个串 | ID |
|---|---|---|
| S1 全部内容(从这里往下读) | `### 5.1 S1` | §5.1 |
| **5 处代码改动清单** | `#### 要做什么 —— 实现清单` | M1–M5 |
| M1 完整实现代码 | `**M1 的代码**` | M1 |
| M1 的四个"必须"两个"绝不" | `**M1 的四个` | M1 |
| **为什么必须改 `device_api.cu`/`device_ir.cu`** | `**为什么 M3/M4 必须改测试源` | §5.1 |
| M3 桥接 launcher 代码 | `**M3 的代码**` | M3 |
| M4 指针导出代码 | `**M4 的代码**` | M4 |
| 明确不做的事 | `避免范围蔓延` | §5.1 |
| 完成定义 DoD | `#### 完成定义` | §5.1 |
| **验证清单速览(7 用例)** | `**一眼看全` | A1–A3, B1–B4 |
| A1/A2 测试代码 | `##### A1 / A2 的具体代码` | A1, A2 |
| A3 测试代码 | `##### A3 的具体代码` | A3 |
| B1–B5 负例代码 | `##### B1–B5 的具体代码` | B1–B5 |
| `K<n>` 编号体系警告 | `#### 5.1.1` | — |
| **目标环境 + GPU0 坏卡硬约束 + W2 方案** | `#### 5.1.2` | §5.1.2 |
| ↳ GPU0 硬约束细节 | `GPU0 是坏的` | — |
| ↳ W2 执行步骤(含备份/回滚) | `**W2 执行步骤` | — |
| **三个待定决策** | `#### 三个待定决策` | §5.1 末 |

**全部章节**(搜节号即可)

`## 0. 先读这一页` · `### 0.4 上游变更复核` · `## 1. 概念速查` · `### 1.5 术语表` · `## 2. 总览` · `### 2.1` · `## 3. 依赖关系` · `### 3.1 依赖图` · `### 3.2 推荐阶段划分` · `## 4. 本机` · `## 5. 逐 subtask` · `### 5.1 S1` · `### 5.2 S2` · `### 5.3 S3` · `### 5.4 S4` · `### 5.5 S5` · `### 5.6 S6` · `## 6. 验收` · `## 7. 交付物` · `## 8. 决策记录` · `## 9. 风险清单` · `## 10.` · `## 附录 A` · `## 附录 B` · `## 附录 C` · `## 下一步建议`

**稳定 ID 直达**(直接搜 ID)

| ID | 内容 |
|---|---|
| **D1** ~ **D7** | 7 条决策,每条在 `§8` 下有独立小节(`### D1 —` … `### D7 —`) |
| **D1** | S1 `launchKernel` 真实现,不打通公共 API |
| **D2** | S2 实现 `getLastError`,不改上游 |
| **D3** | S5 `hostGetDevicePointer` 真实现 + **成套**改 `flagcxMemHost` |
| **D4** | S3 不改 trap,但 **acquire fence 必须补** |
| **D5** | T3/T4 目标不打折,预设豁免预案 |
| **D6** | rank wrapper 我们自己写 |
| **D7** | GDR 可见性:接受 `UNKNOWN`,补 N1/N2 覆盖 |
| **N1** ~ **N5** | 上游 5 个 commit 带来的 5 项新工作(在 `### 0.4` 内) |
| **M1** ~ **M5** | S1 的 5 处代码改动(在 `§5.1` 实现清单内) |
| **A1** ~ **A3** | S1 验证正例(黄金对照) |
| **B1** ~ **B5** | S1 验证负例(失败须返回真实错误) |

---

## 0. 先读这一页(5 分钟看懂全局)

### 0.1 一句话结论

【文档 §12】把厂商适配分成四阶段:**基础可编译 → 选后端 → 功能完整性 → 性能增强**。天数的 `DeviceAdaptor`、`PlatformTraits`、`Default` 后端接线**大部分已经就位**(有 commit `3dcea55` 打底),当前 6 个 subtask 的**真实工作量分布极不均匀**:

| 类别 | subtask | 实际工作量 |
|---|---|---|
| **真的没做完,有明确活干** | S2 `getLastError`、S3 内存序/scope 修正、S5 `hostGetDevicePointer` 桩 | 小~中 |
| **主要工作是"跑通+出报告"** | S4 构建、S6 IPC/GDR 验证 | 中(但必须上真机) |
| **前提与代码现状不符,需先澄清口径** | S1 `launchKernel` | **可能为 0,也可能很大** |

### 0.2 五条最重要的发现(会改变你的做法)

**① S1 的实现"无调用者",但【文档】给了合规出口。**
`launchKernel` 在全仓库、全部 466 个 git commit 中**零调用点**【代码】。但【文档 §4.1】的原文是:

> Kernel launch | `launchKernel` **或平台对应的 kernel launcher** | 能够启动厂商编译器生成的 Device API kernel

**"或平台对应的 kernel launcher"** —— 而 `test/kernel/iluvatar/*.cu` 里的 host launcher(用 `<<<>>>` 直接启动)正是"平台对应的 launcher"。所以 S1 **存在"书面声明等价实现"这条合规路径**,不一定非要填那个槽位。这需要向 Jira 提交人确认选哪条路。

**② S3 的两个问题不是"可能有风险",而是【文档】明文的 P0-Core 违规。**

【文档 §5.2】原文:
> 必须正确映射:… system/device/block/thread scope
> …**仅保证原子性但忽略 acquire/release 可见性,会产生难以复现的跨 rank hang。**

而【代码】`iluvatar_platform_traits.h`:
- `:160`、`:176` 有 `(void)Scope;` —— **scope 参数被直接丢弃** ❌
- `:165-170` acquire/acq_rel/seq_cst 退化成**裸 volatile 读,不加 fence**;而 release 侧(`:181`)是有 fence 的 ❌

这两条**逐字命中**文档警告的失效模式。文档说的"难以复现的跨 rank hang"正是它的临床表现。

**③ S3 真正会炸的不是 6 个 trap,而是一处静默扩宽导致的挂死。**
经两个独立后台调查逐点追踪(标记为 FACT):【代码】`iluvatar_platform_traits.h` 的 6 个 `__builtin_trap()`(第 69、79、287、300、312、387 行)**当前验收 kernel 集一个都碰不到**。真问题是:

【代码】`test/kernel/nvidia/device_ir.cu:1497-1501` 把 `FLAGCX_COOP_WARP` 的工作**只分配给 lane 0-31**:
```c
if (coopKind == FLAGCX_COOP_WARP) return FLAGCX_THREAD_IDX_X < 32;   // 假设 32 lane 一个 wave
```
但天数上 `FLAGCX_COOP_WARP = CoopTile<64>`【代码 `iluvatar_platform_traits.h:277`】,被分类为 `SyncWarp`【`:356`】,`sync()` 变成 `syncwarp(fullMask())` → **覆盖全部 64 lane**【`:380`】。**若 CoreX 的 `__syncwarp()` 是波前级 barrier,lane 32-63 永远到不了 → 挂死**。这是【推断】,因为【文档 §5.1】要求"厂商必须根据真实执行模型实现 mask、tile 和 cooperative group 语义",而 CoreX 的 `__syncwarp()` 真实语义**本仓库无法确定**(调查标为 UNDETERMINED)——**必须用真机微基准测**。

**④ S4 的验收命令不能照抄 Makefile。**
【文档 §13.3.2~13.3.6】规定的基线环境只有 **3 个变量**(`FLAGCX_USE_HETERO_COMM=1`、`FLAGCX_VMM_ENABLE=0`、`LD_LIBRARY_PATH`)且 T1/T2 必须带 `-R 2`。而【代码】`test/unittest/device_api/Makefile` 的 `run-*` 目标:
- `:212` `run-mpi-intra` **一个尺寸参数都不传**
- `:232` `run-mpi-inter` 用 `-b 1M -e 4M`(**不是文档要求的 1K–16M**)
- `:186` `run-mpi-inter` 还额外设了 `FLAGCX_P2P_DISABLE=1`,**把 P2P 关掉了**——而 T2 要测的就是 one-sided Put/Get/remote signal

**所以:验收必须手工敲【文档 §13.3.3~13.3.6】的四条命令,不能用 `make run`。** 否则你会跑着"不是验收条件"的命令,却以为通过了。

**⑤ S5 的 NULL 槽位已经造成过真实崩溃(exit=139 = SIGSEGV),而且根因比表面更深。**
【代码】`ixcuda_adaptor.cc:415`(旧 :387)的 `hostGetDevicePointer` 是 `NULL`,而全仓库有 **3 处无条件裸调用**(无 NULL 检查):`flagcx/core/launch_kernel.cc:48`、`flagcx/runner/uni_runner_impl.cc:1903`、`flagcx/core/proxy.cc:2094`。你仓库里既有的 `dsh-fixes/README.md` 已记录过这个崩溃。

**而根因是:天数 `deviceMalloc(flagcxMemHost)` 用的是 `cudaMallocHost`(`:49-50`,**不带 `Mapped` 标志**),NVIDIA 用的是 `cudaHostAlloc(ptr, size, cudaHostAllocMapped)`(`cuda_adaptor.cc:76-77`)。**没有 `Mapped`,`cudaHostGetDevicePointer` 根本拿不到有效别名** —— 所以**只把 NULL 改成桩或只补实现都无效,必须成套改**(详见 §8 **D3**)。

### 0.3 状态判定口径(往后每次汇报都用这套词)

【文档 §15】明确规定**"接口槽位非空、能够编译、功能通过"是三个不同状态**,并且:

> 不应根据 adaptor 中存在同名函数就标记为已支持。

| 状态 | 定义【文档 §15】 | 天数当前(我的判定) |
|---|---|---|
| 未接入 | 尚无实现 | — |
| 可编译 | 接口和 kernel 能编译,未完成运行验证 | **← 大致在此** |
| 基础通过 | 所选路径的 P0 功能通过 | 未达 |
| 完整通过 | T1–T4 均覆盖 1K–16M,连续运行 ≥3 次通过【§13.3.9】 | 未达 |
| 部分支持 | 需在备注中列出**缺失接口、运行限制和回退路径**【§15】 | 预计最终会用到 |

### 0.4 上游变更复核(基线 `250ee84` → **`ff5f80c`**)🔴 必读

> 我们在分析期间上游合并了 5 个 commit(120 文件 / +14712 −1518)。本节是**复核结论**。
> **凡本文档中引用 `ixcuda_adaptor.cc`、`flagcx_device_adaptor.h`、`flagcx_p2p.cc`、`test_device_adaptor.cpp` 的行号,一律以本节的"行号重映射表"为准**(旧行号是 `250ee84` 的)。

**✅ 好消息:上游替我们解决了一项,并且新增测试对我们的桩是"跳过"而非"失败"。**

| 结论 | 详情 |
|---|---|
| **① `getPointerType` 上游已为天数实现** | 新增 `ixcuda_adaptor.cc:381-407`,已接线到 `:475`。→ **本文档原"隐患:S6 缺 `getPointerType`、类型判别无兜底"作废**(见 §5.6 已改写)。新实现自带 `cudaGetLastError()` 清理探测错误(`:388`、`:393`) |
| **② 新增测试对 `NotSupported` 桩会 SKIP,不会 FAIL** | `test_device_adaptor.cpp` 里 `GetAddressRangeForInteriorGdrPointer:277`(SKIP 于 `:290-292`)、`HostRegisterUnregister:806`(SKIP 于 `:808`/`:819`)、三个 `IpcMemHandle*`(`:985`/`:1011`/`:1049`)都有显式 `flagcxNotSupported → GTEST_SKIP`。→ **不强制我们实现 `getAddressRange`,也不强制实现 `hostRegister`** |
| **③ 新增 GDR 可见性子系统对天数是"保守安全"** | 新 `flagcx/core/gdr_visibility.h:43-56` 的 `flagcxGdrVisibilityContext` 接收 `deviceFamily` / `deviceArchitecture`;`:82-85` 明确"**Unknown or partially modelled topology is fail-closed and retains the device default**"。新枚举 `FLAGCX_GDR_DEVICE_{UNKNOWN,CUDA,METAX,DU,PPU}`(`flagcx_device_adaptor.h:26-30`)**没有天数条目** → 天数落为 `UNKNOWN`(零初始化),**不会获得任何 CUDA 专属豁免,保持保守**。✅ |
| **④ 挂死风险与内存序问题未被上游触碰** | `iluvatar_platform_traits.h`、`device_utils.h`、`bindings/ir/iluvatar/*`、`makefiles/iluvatar.mk`、`test/kernel/iluvatar/*` **全部未改动** → **S3 全部结论与行号依然有效**,S4 接入点未变 |

**⚠️ 两处必须留意的变化:**

| 变化 | 影响 |
|---|---|
| **`latest` 结构体尾部新增 2 个成员** | `gdrDeviceFamily`(`:352`)+ `getDeviceArchitecture`(`:357`,**最后一个成员**)。天数的位置初始化器(`ixcuda_adaptor.cc:409-481`)未显式赋值 → **零初始化**,即 `UNKNOWN` + `NULL`,符合新测试 `V1UpgradePreservesSignalAcquire` 对 v1 的期望(`gdrDeviceFamily == FLAGCX_GDR_DEVICE_UNKNOWN`、`getDeviceArchitecture == nullptr`) |
| **新增过渡标志 `FLAGCX_DEVICE_ADAPTOR_INTERNAL_IPC_POINTER_INFERENCE`**(`:69`) | 注释要求"各适配器拿到权威 `getPointerType` 后**应移除此位**"。天数**没有**设置它(`:477` 是 `FLAGCX_DEVICE_ADAPTOR_INTERNAL_NONE`)✅ 正确。全仓库只有 3 个适配器还在用它:`tsmicro_adaptor.cc:477`、`tops_adaptor.cc:506`、`ptpu_adaptor.cc:439` |

**🔴 指针类型判定逻辑已迁移到新文件**(原 `flagcx_p2p.cc:1631-1643` 的引用全部失效):
- 新位置: **`flagcx/core/p2p_pointer.cc:15-101`,`flagcxP2pDetectPointerType()`**
- 新逻辑(`:32-65`):`legacyV1`(`:32-34`)/`transitionalInference`(`:35-38`)两个开关;若有权威 `getPointerType`(`:40-50`)则优先;**若判定为 HOST 在 `:64-65` 直接返回,完全不碰 IPC**
- 关键设计说明(`:26-31`):"**Latest adaptors must classify through their runtime; IPC is only optional sharing metadata after a GPU result.**"
- `flagcx_p2p.cc` 本身大改(+1054),原引用不可再用

**📋 行号重映射表(`250ee84` → `ff5f80c`)**

| 文件 | 项目 | 旧行号 | **新行号** |
|---|---|---|---|
| `flagcx_device_adaptor.h` | `FLAGCX_GDR_READ_REQUIRES_FLUSH` | 65 | **79** |
| | `FLAGCX_GDR_WRITE_REQUIRES_FLUSH` | 66 | **80** |
| | v1 `launchKernel` | 136-141 | **150-155** |
| | latest `launchKernel` | 222-227 | **236-241** |
| | `getLastError` | 292 | **306** |
| | `gdrFlushRequirements` | 331 | **348** |
| | `gdrDeviceFamily`(**新**) | — | **352** |
| | `getDeviceArchitecture`(**新,末位**) | — | **357** |
| | `GetPointerTypeNotSupported` | 349-353 | **371-375** |
| | `GetAddressRangeNotSupported` | 355-362 | **378-385** |
| | `UpgradeV1` 设 gdrFlushRequirements | 376 | **399** |
| `ixcuda_adaptor.cc` | `gdrMemAlloc` / `gdrMemFree` | 99-111 / 113-119 | **不变** |
| | `flagcxMemHost` 分支(`cudaMallocHost`) | 49-50 | **49-50(不变)** |
| | 5× `ipcMemHandle*` | 239-280 | **不变** |
| | `HostRegister` / `HostUnregister` | 341-346 | **不变** |
| | `ixcudaAdaptorGetPointerType`(**新**) | — | **381-407** |
| | 结构体初始化器 | 381-453 | **409-481** |
| | **`hostGetDevicePointer` = NULL** | 387 | **415** |
| | **`launchKernel` = NULL** | 413 | **441** |
| | `copyArgsInit` / `copyArgsFree` | 417 / 418 | **445 / 446** |
| | `launchDeviceFunc` = NULL | 419 | **447** |
| | `hostRegister` / `hostUnregister` 接线 | 437 / 439 | **465 / 467** |
| | **`getLastError` = NULL(仍为 NULL)** | 446 | **474** |
| | `getPointerType` 接线 | 447(桩) | **475(`ixcudaAdaptorGetPointerType`)** |
| | `getAddressRange`(仍 NotSupported) | 448 | **476** |
| | `internalFlags`(= NONE) | 449 | **477** |
| | `gdrFlushRequirements`(= 仅 WRITE) | 452 | **480** |
| `flagcx/core/` | 指针类型判定 | `flagcx_p2p.cc:1631-1643` | **`p2p_pointer.cc:15-101`** |
| | **`getLastError()` 调用点** | `flagcx_p2p.cc:1641-1642` | **`p2p_pointer.cc:94-95`** |
| `test/unittest/adaptor/` | `HostGetDevicePointer` 测试 | 760-779 | **780-799** |
| | `GetAddressRangeForInteriorGdrPointer` | — | **277-337** |
| | `HostRegisterUnregister` | — | **806** |
| | `GetPointerType` | — | **209** |
| | `V1UpgradePreservesSignalAcquire` | 15 | **15** |

> 📌 **补充说明(复核确认)**:`flagcx_device_adaptor.h` 的偏移**不是统一的** —— `:292` 及以后的**声明**偏移 **+14**,两个 inline 桩与 `UpgradeV1` 偏移 **+23**。而 `ixcuda_adaptor.cc` 中**初始化器之前的所有行号完全不变**(`:99`/`:49-50`/`:239-280`/`:341-346` 都照旧),只有初始化器槽位 **+28**。

**🆕 复核新发现的 5 项工作(原分析未识别)**

| # | 新发现 | 证据 | 影响 | 建议优先级 |
|---|---|---|---|---|
| **N1** | **`GetPointerType` 单测对天数从"跳过"变成"真跑",且断言 interior pointer** | `test_device_adaptor.cpp:209-275`;旧的 `NotSupported` 逃生口在 `:214-221` **不再被走**(天数新实现对 NULL 返回 `flagcxInvalidArgument`,走的是 `:223-225`);`:241-244`/`:255-258`/`:269-272` 断言 `ptr + 64` | **这是最可能出现的新失败点**。依赖 CoreX 的 `cudaPointerGetAttributes` 对"基址+偏移"的行为与 NVIDIA 一致 | 🔴 **高** |
| **N2** | **天数的 GDR flush 默认值**在任何地方都没有被断言 | `GdrFlushPolicyTest.ActivePlatformUsesDocumentedDefault`(`test_device_adaptor.cpp:96-113`)因 `IXCUDA` 不在 `{CUDA, MACA, DUCUDA, PPU_CUDA}` 列表中而在 `:112` `GTEST_SKIP` | 天数"只声明 WRITE"这个**关键策略决定无测试兜底**;而该测试对其他平台期望的是 `READ\|WRITE`,所以天数需要**单独一个分支** | 🟠 中 |
| **N3** | **天数被刻意排除在新的 GDR 可见性 kernel 覆盖之外** | 无 `test/kernel/iluvatar/gdr_visibility.cu`(只有 nvidia/maca 有);`test/kernel/include/gdr_visibility_test_kernel.cuh:15-17` 在没有受支持适配器宏时 **`#error`**;`test/unittest/rma/Makefile:22-31` 用 `VISIBILITY_PLATFORM := $(filter 1,$(USE_NVIDIA) $(USE_METAX) $(USE_PPU))` 门控 | 要做对等覆盖需 4 处改动:shim `.cu` + `test/kernel/iluvatar/Makefile` 加 `gdr_visibility.o` 规则 + `.cuh` 加 `#elif defined(USE_ILUVATAR_ADAPTOR)` + `rma/Makefile` 的 `VISIBILITY_PLATFORM` | 🟠 中 |
| **N4** | **潜在构建断点**:`test/kernel/Makefile:9-10` 新增转发目标 `gdr_visibility.o`,而 `test/kernel/iluvatar/Makefile` 没有对应规则 | `test/kernel/Makefile:9-10` vs `test/kernel/iluvatar/Makefile:16-36` | `make -C test/kernel gdr_visibility.o` 在天数上会 "No rule to make target" 而失败。**当前无 in-tree 调用者**(只有 rma 套件,且已被 N3 的门控挡住),属**潜伏**问题 | 🟡 低(但 N3 一旦做就必须一起修) |
| **N5** | **`flagcxGdrDeviceFamily_t` 没有天数条目** → 天数是**永久 UNKNOWN** | `flagcx_device_adaptor.h:26-30`(只有 UNKNOWN/CUDA/METAX/DU/PPU) | 天数的 GDR 可见性策略永远是"保留设备默认",**拿不到任何 CUDA 专属豁免**。这是**安全**的(见下),但若将来需要差异化策略,得由上游再加枚举成员 | 🔵 需**决策**(见 §8 **D7**) |

**关于 N5 的补充事实(复核确认,消除一个担心)**:新增 resolver **不会因为 UNKNOWN 而报错或告警** —— `isKnownDeviceFamily` 接受 UNKNOWN(`gdr_visibility.cc:22-24`),校验在 `:59` 通过;CUDA 豁免分支 `:82-84` 要求 `deviceFamily == FLAGCX_GDR_DEVICE_CUDA`,天数永远不会进入;唯一可观测后果是 reason 位 `FLAGCX_GDR_VISIBILITY_REASON_DEVICE_DEFAULT`(`:72-74`)。同理 `getDeviceArchitecture == NULL` 时 `init.cc:362-375` 静默保持 `comm->compCap = 0`。

**关于 S6 的一个利好(复核确认)**:【文档】层面的"READ 不需 flush"**依然是合法声明** —— `flagcxOneSideGetCompletionRequiresFlush` 仅在 READ 位存在时返回 true(`flagcx_hetero.cc:705-713`),`flagcxRmaProxyTrackGetVisibility` 在无 READ 位时直接返回(`:839-844`),`flagcxP2pValidateReadVisibility` 同理(`p2p_visibility.h:22-32`)。`docs/environment_variables.md` **未被这 5 个 commit 改动**,且 #640 **没有引入新环境变量**(只有 CI/测试专用的 `FLAGCX_CI_*`);注意该文件还有第三个相关变量 `FLAGCX_GDR_FLUSH_DISABLE`(`:169`)。

---

## 1. 概念速查(初学者必读)

> 📖 **配套阅读**:`天数智芯-句柄是什么.md` —— 专门讲**句柄(handle)与指针的区别**、IPC 句柄的**两层结构**、5 个 `ipcMemHandle*` 的**两条独立生命周期**,以及"句柄只认整块分配"如何引出 S6 的隐患①。读 S5/S6 之前建议先看它。
> 其它可交叉参考的既有笔记见附录 B 末尾。

### 1.1 三层抽象:谁在哪一侧

【文档 §1】把 Device API 分成 Host 侧和 Device 侧。用一句话记忆:

```
Host(CPU 上跑的库代码)                    Device(GPU 上跑的 kernel 代码)
├── DeviceAdaptor                          └── DeviceAPI = CommTraits<所选后端>
│     └── 内存/stream/event/kernel启动/IPC/VMM        ├── PlatformTraits<厂商平台>
│                                                            │     └── Intrin / Atomic / Coop / fence
└── DevApiBackend(Default/CCL/SHMEM)                        └── CommTraits
                                                              └── Comm / Window / Team / Barrier / Net
```

【文档 §1】还有一句必须记住的分工:

> **`PlatformTraits` 不负责通信;`CommTraits` 才是 Device API 的设备侧通信抽象。`CommTraits` 会使用 `PlatformTraits` 提供的底层设备原语。**

**为什么这对你重要**:Default 路径下【文档 §6.1、§16.2】明确"**通常不需要**厂商实现 CommTraits",FlagCX 已提供 `default_comm_traits.h`,天数只需实现 `PlatformTraits<VendorPlatform>` 并实例化。所以**你的 6 个 subtask 里,真正要写的设备侧代码只有 `PlatformTraits` 那一块(S3)**;其余都是 Host 侧的 `DeviceAdaptor`(S1/S2/S5/S6)和构建/验收(S4)。

### 1.2 优先级体系:P0 是**分路径**的,不是全局的

【文档 §2】的硬约束(初学者最容易误解的一条):

> **接口优先级必须与所选 Device API 后端绑定,不能将某一路径的要求写成所有厂商的全局要求。**

| 优先级 | 含义 |
|---|---|
| P0-Core | 任意 Device API 后端都需要的公共能力 |
| P0-Default | **只有**选择 FlagCX Default IPC/Net 后端时必须实现 |
| P0-CCL | 只有选择厂商 CCL Device API 后端时必须实现 |
| P0-SHMEM | 只有选择厂商 SHMEM Device API 后端时必须实现 |
| P1 | 完整功能、性能增强或可选运行模式所需 |
| P2 | 不属于本次推广核心,或仅被 Host API 等其他路径使用 |

天数已选 **Default**(【代码 `makefiles/iluvatar.mk:27`】`ADAPTOR_FLAG := -DUSE_ILUVATAR_ADAPTOR -DFLAGCX_COMM_TRAITS_DEFAULT`)。所以适用集 = **P0-Core + P0-Default**,**CCL/SHMEM 那两栏与你的 6 个 subtask 完全无关**,不要花时间。

### 1.3 最重要的一条纪律:"槽位非空"≠"功能已支持"

【文档 §2】原文:

> "接口槽位必须非空"和"功能上必须支持"是两个不同概念。部分 plugin ABI 会要求函数指针存在;**如果当前路径不使用该能力,可以提供返回 `flagcxNotSupported` 的实现,但不能把这种实现标记为功能已支持。**

**这条是你这个项目里最省力也最容易被误判的规则。** 由此派生两条实操准则:
1. **允许**用返回 `flagcxNotSupported` 的桩满足 ABI 槽位(这是文档承认的合法终态)。
2. **禁止**把这样的桩在交付表里填成"基础通过"。必须填"部分支持"或"未接入",并在备注写清【§15】"缺失接口、运行限制和回退路径"。

⚠️ 反向的坑:【文档 §4.5】还要求"**所有 P0 接口返回值与实际执行结果一致,不能 silent no-op**"。**返回 `flagcxNotSupported` 是"如实声明不支持",是合规的;而"静默什么都不做"是违规的。** 两者的区别是:调用方能不能从返回值判断出"这条能力不可用"。天数需要警惕的反例是【代码】`du_platform_traits.h:64-67`,它的 `namedBarrierSync` 函数体被注释掉,**静默不做任何事** —— 这是反例,不要照抄。

### 1.4 六个 subtask 分别落在哪一层

```
Host 侧 ─────────────────────────────────────────────────────────
  S1  launchKernel              ← DeviceAdaptor / Kernel launch     (§4.1 P0-Core)
  S2  getLastError              ← DeviceAdaptor / 错误处理           (§4.1 P0-Core)
  S5  hostGetDevicePointer      ← DeviceAdaptor / Host 映射          (§4.2 P0-Default + §4.3 条件P0)
  S6  gdrMemAlloc + 5×ipcMemHandle ← DeviceAdaptor / GDR + IPC       (§4.2 P0-Default)
  S4  构建 libflagcx.so + 四个验收程序 ← 编译接入                     (§10.3/§10.4)

Device 侧 ───────────────────────────────────────────────────────
  S3  iluvatar_platform_traits.h ← PlatformTraits / Intrin+Atomic+Coop (§5.1~§5.3 P0-Core)
```

### 1.5 术语表

| 术语 | 一句话解释 | 在哪 |
|---|---|---|
| **Device API** | FlagCX 的一套"让 GPU kernel 自己发起通信"的接口,替代"CPU 发指令、GPU 干等"的传统模式 | 【文档 §1】 |
| **DeviceAdaptor** | Host(CPU)侧的一张大函数指针表,包住厂商 runtime(内存/流/事件/IPC/kernel 启动) | 【代码】`flagcx_device_adaptor.h` |
| **PlatformTraits** | Device(GPU)侧的"芯片原语"适配层:原子操作、内存序、fence、cooperative 同步 | 【代码】`iluvatar_platform_traits.h` |
| **CommTraits** | Device 侧的"通信语义"层:team、barrier、window、Put/Get、Signal、Wait、Flush | 【代码】`default_comm_traits.h`(FlagCX 提供) |
| **P0-Core / P0-Default** | 优先级档位;P0-Default 只在选 Default 后端时才是硬要求 | 【文档 §2】 |
| **slot(槽位)** | `DeviceAdaptor` 结构体里的一个函数指针位置。为 `NULL` = 没有实现 | — |
| **`flagcxNotSupported`** | "这条能力本平台不支持"的**如实**返回码。**合规的终态** | 【文档 §2】 |
| **IPC** | Inter-Process Communication。**同一台机器内**跨进程共享显存的技术(单机路径靠它) | 【文档 §4.2】 |
| **GDR** | GPUDirect RDMA。网卡**不经过 CPU**直接把数据写进显存(跨机路径靠它) | 【文档 §4.2】 |
| **MR** | Memory Region。RDMA 网卡要"注册"一块内存才能对它做远程读写 | — |
| **symmetric window** | 把各 rank 的缓冲区组织成"每张卡按同一算法就能算出别人缓冲区地址"的全局视图 | 【文档 §6】 |
| **wave / warp** | GPU 上真正"同步执行的一批 lane"。**NVIDIA 是 32,天数是 64** | 【文档 §5.1】 |
| **lane mask** | 64 位掩码,第 i 位表示第 i 个 lane 是否参与本次同步 | 【代码】`flagcxLaneMask_t` = `uint64_t` |
| **sticky error** | CUDA 风格的"粘性错误":`cudaGetLastError()` 会**取走并清除**它。取一次就没了 | — |
| **scope** | 内存操作的可见范围:thread / block / device / system。system 最宽(跨 GPU、跨网卡) | 【文档 §5.2】 |
| **T1–T4** | 四个统一验收测试:两个 operator 测试 + 两个 Unified IR 测试 | 【文档 §13.3】 |
| **`-R 2`** | 测试命令行参数,选择 **symmetric window 注册模式** | 【文档 §13.3.2】 |
| **rank wrapper** | 一个 shell 脚本,按 rank 号设置"可见设备 / HOSTID / 网卡",把单机 8 卡伪装成两个 4 卡逻辑节点 | 【文档 §13.3.1】 |

---

## 2. 总览:【文档】要求 ↔ 【代码】现状 ↔ subtask 映射

| # | subtask | 【文档】依据 | 【代码】现状 | 判定 |
|---|---|---|---|---|
| **S1** | 补齐 `launchKernel` | §4.1「`launchKernel` **或平台对应的 kernel launcher**」;§4.5「不能 silent no-op」;§2 槽位≠支持;§15 同名函数≠已支持 | 16 个适配器全 `NULL`(含 NVIDIA);**全仓库+466 commit 零调用点**;所有 kernel 用 `<<<>>>` 直启 | ⚠️ **前提与现状不符,需先澄清口径**;【文档】有合规出口 |
| **S2** | 补齐 `getLastError` | §4.1「错误处理:`getLastError` 或厂商等价机制 / **能够在 kernel launch 和 runtime 调用失败时返回有效错误**」;§4.5「返回值与实际执行结果一致,不能 silent no-op」;§16.5「厂商需要确保 DeviceAdaptor **返回真实 IPC 结果**」 | 槽位仍为 `NULL`(`ixcuda_adaptor.cc:474`);NVIDIA 有 4 行参考实现(`cuda_adaptor.cc:1106-1109`);调用点已迁至 `p2p_pointer.cc:94-95`,**丢弃返回值是正确设计**;天数未被 `IPC_POINTER_INFERENCE` 门控影响 | ❌ **必须实现**(理由已按上游变更改写,见 §5.2)。NULL 会让"设备指针 IPC 导出失败"的粘性错误残留 → 违反 §4.5 |
| **S3** | PlatformTraits 64 宽 wave / 内存序 / 部分 mask | §5.1(simtWidth 不假设 32;lane id/mask、active mask、sync、popcount、barrier、spin backoff、**system-scope fence**);§5.2(8 个 atomic + **5 种内存序** + **4 种 scope**);§5.3(每种 coop 类型 `threadRank/size/sync`) | 6 个 trap **当前不可达**;但 **scope 被丢弃**(`:160,176`)、**acquire 无 fence**(`:165-170`)、`spinBackoff` 空操作(`:82-84`)、`activemask` 从未验证、**32/64 波前扩宽导致潜在挂死** | ❌ **与 §5.2 直接冲突**;trap 设计与 §5.1/§5.3 有张力 |
| **S4** | 编出 `libflagcx.so` + 四个验收程序(Default) | §10.3(三部分同参数、主库 `COMPILE_KERNEL=1`);§10.4(编译验收标准:**`ldd` 必须加载本次构建的 so**、无 unresolved device symbol / 重复 alias / vtable 冲突、版本一致、记录完整命令);§13.3.2~6(四条标准命令) | `iluvatar.mk` 已设 `FLAGCX_COMM_TRAITS_DEFAULT`(`:27`)并链 `default_dev_api_backend.cc`(`:33`);`test/unittest/device_api/` 四个程序源码齐全 | ✅ **Jira 三条描述全部为真**;⚠️ **本机无工具链**,必须在 Linux 跑;⚠️ Makefile 的 run 目标 ≠ 文档命令 |
| **S5** | signal/barrier 用 IPC 还是 host-mapped | §4.2(Host 映射 `hostGetDevicePointer` 属 P0-Default);§4.3(不支持则**明确返回 `flagcxNotSupported`** 并在**平台默认配置中关闭**该模式);§4.5(指针类型不能混用) | `SIGNAL_HOST_ENABLE` 默认 **0** → 走 IPC【代码 `utils.cc:119`】;IPC 分支**完全不碰** hostRegister/hostGetDevicePointer【`default_dev_api_backend.cc:179`】;`hostRegister/Unregister` 桩**正确**;但 `hostGetDevicePointer` 是 **`NULL` → 3 处裸调用 → exit=139** | ✅ 决策正确;**❌ 但 NULL 必须改成桩** |
| **S6** | 单机验证 IPC 与 GDR | §4.2(GDR/VMM 分配、IPC handle、Host 映射);§4.5(IPC handle 可跨**同一节点**进程交换/打开/关闭;**`deviceFree`、IPC `close` 和 `handle free` 生命周期不交叉**;**Host 与 Device 指针类型不能混用**) | 1+5 个接口实现**齐全且结构正确**;`FLAGCX_GDR_WRITE_REQUIRES_FLUSH`(`:452`);但 `getAddressRange` 仍 **NotSupported** → 基址探测无兜底(`getPointerType` **上游已实现**,见 §0.4);`gdrMemAlloc` **无任何 GPUDirect/RDMA 能力查询** | ✅ 实现齐全;**测试已现成**(15 用例,2 rank);⚠️ 三处策略断言需真机验证 |

### 2.1 【文档 §14 厂商交付检查表】逐行对照

| 模块 | 检查项【文档 §14】 | 优先级 | 天数状态 | 关联 subtask |
|---|---|---|---|---|
| 构建 | 平台 `.mk`、编译器、device-link、runtime link | P0-Core | ✅ `iluvatar.mk` 齐全 | S4 |
| 构建 | 主库、测试 kernel、operator 测试、Unified IR **使用相同 backend 配置** | P0-Core | ✅ 宏路径一致;需实跑确认 | S4 |
| DeviceAdaptor | 基础内存、device、stream、event、**kernel launch** | P0-Core | ❌ `launchKernel` 为 NULL | **S1** |
| PlatformTraits | Intrin、Atomic、Coop、**memory order/scope** | P0-Core | ❌ scope 未映射、acquire 无 fence | **S3** |
| Default path | **IPC handle、GDR、proxy/Net 可用** | P0-Default | ⚠️ 已实现,待验证 | **S5、S6** |
| Device API | Comm/Window device pointer 创建销毁 | 所选路径 P0 | ⚠️ 待验证(T1–T4) | S4 |
| Device API | INTRA/INTER/WORLD team | 所选路径 P0 | ⚠️ 待验证 | S4 |
| Device API | Barrier、Put/Get、Signal、Counter、Flush | 所选路径 P0 | ⚠️ 待验证 | S4 |
| 正确性 | memory order、scope、cooperative semantics | P0-Core | ❌ 见 S3 | **S3** |
| 增强 | VMM symmetric mapping / multicast | P1 | 不阻塞(文档明确"不应阻塞基础适配"【§4.4】) | 不在本次范围 |
| 验收 | T1/T2/T3/T4 | P0 | ⬜ 未开始 | S4 |
| 稳定性 | 四项测试**连续运行至少 3 次** | P0 | ⬜ 未开始 | S4 |
| 稳定性 | T1–T4 每条设 **30 分钟超时**并正确传播任一 rank 失败 | P0 | ⬜ 未开始 | S4 |
| 交付 | 提交可复现编译环境模板、统一结果表及完整日志 artifacts | P0 | ⬜ 未开始 | 第 7 节 |

---

## 3. 依赖关系与推荐执行顺序

### 3.1 依赖图(谁是别人的前置)

```
        ┌──────────────────────────────────────────┐
        │ S4 构建打通(libflagcx.so + 四程序)        │  ← 一切运行验证的硬前置
        │   必须先能编、能 ldd 到本次产物             │
        └───────────────┬──────────────────────────┘
                        │
      ┌─────────────────┼──────────────────┬─────────────────┐
      ▼                 ▼                  ▼                 ▼
┌───────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│S2 getLast │   │S5 桩化 NULL  │   │S3 硬件事实   │   │S6 IPC/GDR    │
│Error      │   │hostGetDevPtr │   │确认 + 挂死修复│   │15 用例验证   │
└─────┬─────┘   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘
      │                │                  │                  │
      │ ①指针类型探测路径 │ ②避免 3 处裸调用   │ ③决定 T3/T4 是否  │
      │   依赖它清粘性错误 │   崩溃(exit=139)  │   会被挂死污染     │
      ▼                ▼                  ▼                  │
┌──────────────────────────────────────────────────┐         │
│ 指针类型不混用(§4.5)真正可验证                    │◄────────┘
└──────────────────────┬───────────────────────────┘
                       ▼
        ┌──────────────────────────────────────────┐
        │ S1 决策(实现 or 书面声明"平台等价 launcher")│
        │   若做:两个死 kernel 需补 launcher          │
        └───────────────┬──────────────────────────┘
                        ▼
        ┌──────────────────────────────────────────┐
        │ T1–T4 × 连续 3 次 + 30 分钟超时 + 交付报告  │
        └──────────────────────────────────────────┘
```

**三条关键依赖,请务必记住:**

1. **S2 → S6(已弱化,但仍存在)**:~~原依赖是"`getLastError` 为指针类型探测清粘性错误"~~ —— **上游已给天数实现权威 `getPointerType`,这条依赖消失**(见 §0.4)。现在剩下的是:`p2p_pointer.cc:83` 对**设备指针**仍会尝试 IPC 导出,失败时 `:94-95` 的清理会被 NULL 槽位跳过 → 粘性错误残留。**所以 S2 仍是 S6 的加分项,但不再是硬前置。**

2. **S5 → S6**:`hostGetDevicePointer` 为 NULL 时,`uni_runner_impl.cc:1903` 和 `proxy.cc:2094` 会在检查返回值**之前**就段错误。S6 的测试要跑到这些路径,**不先修就会白跑**。

3. **S3 → S4 的 T3/T4**:**挂死风险不清,`ALL PASS` 的结论不可信**。而且挂死表现为"30 分钟超时失败",【文档 §13.3.8】会把它判为 FAIL,但排查方向极易被误导到 IPC/网络。**建议在跑 T3/T4 之前先做 S3 的微基准。**

4. **S1 → S3(有条件)**:【代码】`test/kernel/iluvatar/device_api.cu` 的两个天数专属 kernel(`flagcxIluvatarAtomicContractKernel` `:7`、`flagcxIluvatarUnsupportedCoopKernel` `:29`)**没有 launcher、没有声明、没有调用者**。而它们**恰好是唯一能覆盖 trap 69/387 和 32 位 RMW 契约的测试**。所以:**如果要把 S3 的 trap 行为变成"有测试支撑的结论",就需要 S1 补 launcher。**

### 3.2 推荐阶段划分

| 阶段 | 内容 | 在哪做 | 产出 | 预计 |
|---|---|---|---|---|
| **P0** | **决策已拍定**(第 8 节 D1–D6,提交人不可用,我方自行解决) | — | ✅ 决策记录(已完成) | 0 |
| **P1** | **S4 构建打通**:`make -j8 COMPILE_KERNEL=1 ...` → `ldd` 确认 → 能编不跑 | Linux | `build/lib/libflagcx.so` + 四个可执行 + `ldd` 输出 | 1–2 天 |
| **P2** | **S5(D3)+ S2(D2)+ S1(D1) 代码改动** + host-mapped 探针 | 本机改 + Linux 验 | `ixcuda_adaptor.cc` 四处改动 + 探针结论 | 1 天 |
| **P3** | **S6 IPC/GDR 验证**:15 个用例 × 2 rank + 上游新增的 `GetPointerType`(interior)/`test_p2p_pointer` / `gdr_visibility` 套件 | Linux(单机 8 卡) | 测试日志 + GDR 可注册性结论 + 新 resolver 裁决确认 | 1.5–2.5 天 |
| **P4** | **S3 硬件事实确认**:`__syncwarp()`/`__activemask()` 微基准 → 补 acquire fence → 据结论决定是否修挂死 | Linux | 微基准结果 + 修正后的 trait | 2–3 天 |
| **P5** | **D6 rank wrapper + D6 变量实测** | Linux | `test/script/iluvatar_rank_wrapper.sh` | 0.5 天 |
| **P6** | **T1–T4 ×3 次 + 交付报告**(D5:目标不打折) | Linux | 第 7 节全部交付物 | 2–3 天 |

**每阶段的"过关条件"(不要跳步):**

- **P1 过关**:`nm -D --undefined-only build/lib/libflagcx.so | grep -i devapibackend` **无输出**(证明 `devApiBackend` 已解析);`ldd` 指向本次 `build/lib`。
  > 为什么查这个:【代码】`makefiles/iluvatar.mk:31-32` 的注释明确警告 —— `device_api/` 目录**不会被自动扫描**,而链接用了 `-shared` 却**没有 `--no-undefined`**,所以**漏掉源文件会编译成功但留下未解析符号,直到运行时才炸**。
- **P2 过关**:(a) host-mapped 探针给出明确结论;(b) 原 exit=139 场景不再段错误;(c) `DeviceAdaptorTest.HostGetDevicePointer` 单测 PASS;(d) 新 `launchKernel` 符号存在于 `ixcuda_adaptor.o`;**（e) `launchKernel` 的验收正例/负例全过** —— 用 `device_api.cu` 与 `device_ir.cu` 里**真实 kernel** 做黄金对照(结果逐字节相同),并验证 4 类失败输入返回真实错误(详见 §5.1「验收方法」的 A1–A3 / B1–B5)。
- **P3 过关**:15 个用例全部 PASS,且 `CrossGpuLargeGdrSubrangesWriteAsync` 通过。
- **P4 过关**:能明确回答"CoreX `__syncwarp()` 是全波前还是掩码 barrier",并据此给出挂死的结论;acquire fence 已补且性能可接受。
- **P5 过关**:T2/T4 能在"单机 8 卡模拟两个逻辑节点"下启动。
- **P6 过关**:见第 6 节判定规则。

---

## 4. 本机(Windows)vs Linux:两条工作线

**这是一条必须先说清的事实:当前工作区 `C:\Users\dapeng.he\Desktop\FlagCX\FlagCX` 完全没有构建能力。**

| 检查项 | 结果 |
|---|---|
| `make` / `clang` / `gcc` / `ldd` / `nvcc` | **全部 not found** |
| `DEVICE_HOME` / `ILUVATAR_ARCH` | 未设置(`iluvatar.mk:8` 默认 `/usr/local/corex`) |
| `build/` 目录、任何 `*.so` | **不存在** |
| `python` | Microsoft Store 空壳(无输出) |

所以:

| 工作类型 | 本机(Windows) | Linux 机器 |
|---|---|---|
| 读代码、写文档、出计划 | ✅ | ✅ |
| 改 `ixcuda_adaptor.cc` / `*.h`(纯文本编辑) | ✅ 可改 | ✅ |
| `make` 编译 libflagcx.so | ❌ | ✅ **必须** |
| `ldd` 检查动态库来源 | ❌(Linux 专有命令) | ✅ **必须** |
| 编 kernel(`clang -x ivcore`) | ❌ | ✅ **必须** |
| 跑 T1–T4 / MPI / GPU 测试 | ❌ | ✅ **必须** |
| 微基准测 `__syncwarp()` 语义 | ❌ | ✅ **必须** |

**结论:本机适合产出"代码改动 + 文档 + 命令脚本";所有"编、链、跑、测"必须到 Linux 机器上。** 请勿在本机尝试 `make`。

> 📌 你 subtask 3 里提到的路径 `/mnt/share/user_homes/pfl/flagos-q3/FlagCX/...` 应该就是那台 Linux 机器。**我这边是这个仓库的 Windows 副本,两边不是同一份工作区** —— 改文件时请确认改动落在哪一份上(建议以 git 同步,不要手工拷)。

---

## 5. 逐 subtask 详述

### 5.1 S1 —— 补齐 `launchKernel`

#### Jira 原文
> [P0] 补齐天数 DeviceAdaptor 的 launchKernel,让 Device API kernel 能启动,所有 backend(Default、CCL、SHMEM)都要靠它启动 kernel。
> `FlagCX/flagcx/adaptor/device/ixcuda_adaptor.cc` 里 launchKernel 现在是 NULL。补上实现,使 `FlagCX/test/kernel/iluvatar/device_api.cu` 和 `device_ir.cu` 编出的 kernel 能被启动。启动失败时返回真实错误。

#### 【文档】依据
- **§4.1 P0-Core「Kernel launch」行**:要求是「`launchKernel` **或平台对应的 kernel launcher**」,能力要求是「能够启动厂商编译器生成的 Device API kernel」。
  → **关键词是"或"。**
- 【文档 §4.5】:「所有 P0 接口返回值与实际执行结果一致,不能 silent no-op」。
- 【文档 §2】:「不能把这种实现标记为功能已支持」。
- 【文档 §15】:「不应根据 adaptor 中存在同名函数就标记为已支持」。

#### 【代码】事实(逐条核实)

| 事实 | 证据 |
|---|---|
| 天数的 `launchKernel` 槽位是 `NULL` | `ixcuda_adaptor.cc:441`(旧 :413) |
| **全部 16 个厂商适配器都是 NULL**,包括 NVIDIA | `cuda_adaptor.cc:1188`、`ducuda_adaptor.cc:1033`、`maca_adaptor.cc:1047`、`kunlunxin_adaptor.cc:505`、`hip_adaptor.cc:401`、`cann_adaptor.cc:342`、`mlu_adaptor.cc:370`、`musa_adaptor.cc:398`、`ppu_cuda_adaptor.cc:866`、`ptpu_adaptor.cc:411`、`tops_adaptor.cc:468`、`tsmicro_adaptor.cc:437` 等 |
| **全仓库零调用点,且全部 466 个 commit 中均无调用点** | 全树 grep + `git log --all -G "launchKernel\("` 为空;无宏包装;无间接派发 |
| 所有 kernel 实际用 `<<<grid, block, shmem, stream>>>` 直启 | `test/kernel/nvidia/device_api.cu:120-123`、`device_ir.cu:52` 等;`flagcx/adaptor/kernel/<厂商>/*.cu` 同理 |
| **公共 API 也到不了它** | `flagcx/include/flagcx.h:143-185` 的 `flagcxDeviceHandle` 无此字段;由 `flagcx/flagcx.cc:125-179` 逐字段拷贝 |
| 插件加载器**故意不校验**它 | `flagcx/adaptor/device/device_plugin_load.cc:66-71`(注释里点名 `launchKernel`) |
| 唯一"非 NULL"是插件示例,且返回 `flagcxInternalError` | `adaptor_plugin/device/example/plugin.cc:185-191`、`:287` |
| 签名(两处结构体各一份,布局必须一致) | `flagcx_device_adaptor.h:136-141`(v1)、`:222-227`(latest) |
| 天数结构体是**位置初始化**,无指定初始化器 | `ixcuda_adaptor.cc:381-453`,`:413` 就是那个槽 |
| 历史线索:`FLAGCX_DEVICE_LAUNCH_KERNEL` 宏曾被定义但**从未使用**,后被删除 | commit `11f9b52` 定义 → `128b60a` 删除 |
| 天数专属两个 kernel **是死代码** | `test/kernel/iluvatar/device_api.cu:7`(`flagcxIluvatarAtomicContractKernel`)、`:29`(`flagcxIluvatarUnsupportedCoopKernel`);grep `flagcxIluvatar` 只有这两行定义 |
| 注释承诺的 "CoreX runtime test" **不存在** | `test/kernel/iluvatar/device_api.cu:25-28` 的注释 vs 全仓库无引用 |
| 天数无 CI | `.github/configs/` 下无 `iluvatar.yml` |

#### 初学者注解:这个签名怎么读

```c
flagcxResult_t (*launchKernel)(void *func,
                               unsigned int block_x, block_y, block_z,   // ← 注意顺序!
                               unsigned int grid_x,  grid_y,  grid_z,    // ← 注意顺序!
                               void **args, size_t share_mem, void *stream, void *memHandle);
```

- **`func`**:kernel 的函数地址(编译出的 kernel stub 指针)。
- **`block_x/y/z` 和 `grid_x/y/z`**:**⚠️ 最大陷阱** —— FlagCX 是 **block 在前、grid 在后**;而 CUDA 的 `cudaLaunchKernel(func, gridDim, blockDim, args, sharedMem, stream)` 是 **grid 在前**。**位置照抄会把两者转置。** 而天数的典型配置是 36 blocks × 512 threads【代码 `flagcx/include/flagcx_kernel_core.h:179-183` 的兜底默认值】,转置后**依然"启动成功"**,只是结果错误或挂死 —— **最难查的一类 bug**。
- **`args`**:指向"每个参数地址"的数组(不是参数本身)。这是 `cudaLaunchKernel` 的原始风格接口。
- **`share_mem`**:动态共享内存字节数,直接对应 CUDA 的 `sharedMem`。
- **`stream`**:**它其实是 `flagcxStream_t`,不是 `cudaStream_t`**。【代码】`iluvatar_adaptor.h:14-16` 定义 `struct flagcxStream { cudaStream_t base; }`。所以要用 `((flagcxStream_t)stream)->base`。测试里两种等价写法都存在:`*(cudaStream_t *)stream`(`device_api.cu:274`)和 `stream->base`(`device_ir.cu:52`)。`NULL` 表示 legacy default stream。
- **`memHandle`**:**全仓库无人使用**,也没有文档定义。只能 `(void)memHandle;` 忽略。

#### 方案 —— 已拍定为 **D1:真实现**(见 §8 D1)

> 下面保留三条候选路径的原始分析(它们解释了为什么最终选 B),**最终决策是做法 B**。

**做法 A(已否决):书面声明"平台等价 launcher"。**
- 依据:【文档 §4.1】"`launchKernel` **或平台对应的 kernel launcher**"。
- 做法:在交付报告中写明"天数通过 `test/kernel/iluvatar/*.cu` 内的 host launcher(基于 CUDA 兼容 runtime 的 `<<<>>>`)实现 kernel 启动能力",并把 `launchKernel` 槽位状态填为"**部分支持**",备注写清"槽位为 NULL,使用平台等价 launcher"。**绝不能填"基础通过"**【§2、§15】。
- ❌ **否决理由**:【文档 §4.1】把 kernel launch 列在 **P0-Core**(「任意 Device API 后端都需要的公共能力」)。虽然措辞允许等效 launcher,但既然真实现成本极低,选它可同时满足两种解读、不留解释空间,并消除"上游将来调用该槽位 → 空指针崩溃"的风险。**"合规但留隐患"不如"直接做对"。**

**做法 B ✅ 已选定:实现它 + 把死 kernel 接上。**
- 在 `ixcuda_adaptor.cc` 加 `ixcudaAdaptorLaunchKernel(...)`,转交给 `cudaLaunchKernel`,**注意 block/grid 顺序**。
- 错误处理:【文档 §4.1】要求"能够在 kernel launch 和 runtime 调用失败时返回有效错误"。但【代码】`iluvatar_adaptor.h:26-31` 的 `DEVCHECK` 宏会**丢弃具体的 `cudaError_t`**,一律返回 `flagcxUnhandledDeviceError`。所以**不要用 `DEVCHECK`**,直接读 `cudaLaunchKernel` 的返回值自行映射。
- 顺手把两个死 kernel 补上 launcher(见 S3),让 S3 的 trap 结论有测试支撑。

**做法 C(已否决):做法 B + 打通公共 API 调用路径。**
- 需要改上游 `flagcx/include/flagcx.h` + `flagcx/flagcx.cc` 给 `flagcxDeviceHandle` 加字段,并把 `test/kernel/iluvatar/*.cu` 的 launcher 改成走 `deviceAdaptor->launchKernel`。
- ❌ **否决理由**:**这已经不属"厂商适配"范围**了(【文档 §13.2】把公共测试驱动、ABI 归给 FlagCX 项目负责)。厂商改公共 ABI 越界。

---

#### 要做什么 —— 实现清单(5 处改动,全部本机可完成)

> **全部为纯文本编辑,可在 Windows 工作区完成;编译与运行需 Linux(见「验证清单」)。**
> 行号基线 **HEAD `ff5f80c`**。

| # | 文件 | 改动 | 规模 |
|---|---|---|---|
| **M1** | `flagcx/adaptor/device/ixcuda_adaptor.cc` | 新增 `ixcudaAdaptorLaunchKernel`(插在 `ixcudaAdaptorLaunchHostFunc`(`:282-288`)之后) | +约 30 行 |
| **M2** | `flagcx/adaptor/device/ixcuda_adaptor.cc:441` | 槽位接线:把 `NULL` 换成 `ixcudaAdaptorLaunchKernel` | **1 个 token** |
| **M3** | `test/kernel/iluvatar/device_api.cu` + `test/kernel/include/device_api.h` | 新增**两个桥接 launcher**(`...CommQueriesViaAdaptor` / `...CoopGroupsViaAdaptor`)+ 声明 + `#include "adaptor.h"` | +约 20 行 |
| **M4** | `test/kernel/nvidia/device_ir.cu` + `test/kernel/include/device_ir.h` | 导出 `kernelCommQueriesS` 的 kernel 指针(`extern "C"` getter)+ 声明 | +3 行 |
| **M5** | `test/kernel/iluvatar/device_api.cu` + `test/kernel/include/device_api.h` | **D1 连带**:给两个死 kernel(`flagcxIluvatarAtomicContractKernel` / `flagcxIluvatarUnsupportedCoopKernel`)补 launcher + 声明(服务 S3) | +约 18 行 |

**M1 的代码**:
```c
flagcxResult_t ixcudaAdaptorLaunchKernel(void *func,
                                        unsigned int block_x, unsigned int block_y,
                                        unsigned int block_z,
                                        unsigned int grid_x, unsigned int grid_y,
                                        unsigned int grid_z,
                                        void **args, size_t share_mem,
                                        void *stream, void *memHandle) {
  // 1) 参数校验:Host 侧能判定的错误给精确错误码
  if (func == NULL || args == NULL || block_x == 0 || block_y == 0 ||
      block_z == 0 || grid_x == 0 || grid_y == 0 || grid_z == 0) {
    return flagcxInvalidArgument;
  }
  (void)memHandle;  // 全仓库未使用,签名保留仅为 ABI 占位

  // 2) 维度装配 —— ⚠️ FlagCX 是 block 在前、CUDA 是 grid 在前,不能位置照抄
  dim3 grid(grid_x, grid_y, grid_z);
  dim3 block(block_x, block_y, block_z);

  // 3) stream:void* 实际是 flagcxStream_t(NULL = legacy default stream)
  cudaStream_t cs =
      (stream == NULL) ? (cudaStream_t)0 : ((flagcxStream_t)stream)->base;

  // 4) 启动。注意:不用 DEVCHECK(它会丢掉具体错误码)
  cudaError_t err =
      cudaLaunchKernel((const void *)func, grid, block, args, share_mem, cs);
  if (err != cudaSuccess) {
    // 参考 test/kernel/du/device_api.cu:289-293 的上报风格
    WARN("launchKernel FAILED: %s (%d) block=(%u,%u,%u) grid=(%u,%u,%u)",
         cudaGetErrorString(err), (int)err, block_x, block_y, block_z, grid_x,
         grid_y, grid_z);
    return flagcxUnhandledDeviceError;
  }
  return flagcxSuccess;
}
```

**M1 的四个"必须"与两个"绝不"**:

| | 要求 | 理由 |
|---|---|---|
| 必须 | `dim3 grid(...)` 与 `dim3 block(...)` **分开装配** | 顺序相反;转置后 36×512 变 512×36,**依然启动成功**但结果错/挂死 |
| 必须 | `((flagcxStream_t)stream)->base` | `void *stream` 实际是 `flagcxStream_t`(`iluvatar_adaptor.h:14-16`) |
| 必须 | `stream == NULL → (cudaStream_t)0` | legacy default stream,不改语义 |
| 必须 | **自己读 `cudaLaunchKernel` 返回值** | 【文档 §4.1】要求"返回有效错误" |
| 绝不 | **不用 `DEVCHECK`** | 它把一切压成 `flagcxUnhandledDeviceError`,信息量为零 |
| 绝不 | **不调 `cudaGetLastError()`** | 会吃掉 S2 的粘性错误;【文档 §4.1】把 launch 与错误处理列为**两项独立能力**(待定,见下) |

**为什么 M3/M4 必须改测试源?(回答"为什么动 `device_api.cu` / `device_ir.cu`")**

`launchKernel` 要两样东西:**kernel 的函数地址**(`void *func`)和**参数地址数组**(`void **args`)。而这两个 `.cu` 文件**目前只对外给出具名参数的 launcher**(如 `launchKernelCommQueries(flagcxDevMem_t, flagcxDevComm_t, int*, flagcxStream_t)`)——`<<<>>>` 把"取 kernel 地址"和"打包参数数组"在**编译期隐式**做掉了,所以这两样东西**从未对外暴露**。要经 `launchKernel` 启动,必须先有人把它们造出来。

**为什么桥必须写在 `.cu` 里,而不能写在驱动 `.cpp` 里** —— 三条硬证据:

| 证据 | 位置 |
|---|---|
| 驱动只 include `check.h / device_api.h / flagcx.h / flagcx_kernel.h / tools.h`,**无任何设备侧头文件** | `test/unittest/device_api/test_device_api_intra.cpp:25-29` |
| 公共 API 只有**不透明句柄**:`typedef struct flagcxDevMemInternal *flagcxDevMem_t` | `flagcx/include/flagcx_device_api.h:28` |
| C++ **值类型** `struct flagcxDevMem` 定义在设备侧头里,只有 `.cu`(经 `device_api/flagcx_device.h`)可见 | `flagcx/adaptor/include/device_api/flagcx_device_core.h:148`;`test/kernel/nvidia/device_api.cu:29` |

⇒ `device_api.cu` 的 kernel 参数含 **按值传递的 `flagcxDevMem`/`flagcxDevComm`** → **驱动连类型名都写不出来**,无法构造 `args[]` → 只能在 `.cu` 内装配(**M3**)。而 `kernelCommQueriesS` 参数全是裸指针 → 驱动可自行装配,只需导出 kernel 地址(**M4**,3 行)。

**被否掉的替代方案(避免重复讨论)**:

| 替代 | 为什么不行 |
|---|---|
| 让驱动 include 设备侧头以命名 `flagcxDevMem` | 会把内部布局/枚举/设备 intrinsic 拖进 host pass → 编译风险,且影响**所有平台** |
| 把现有 launcher 从 `<<<>>>` 改为走 `deviceAdaptor->launchKernel` | 改动 27+ 个 launcher;且让 T1/T2 **依赖 `launchKernel` 已实现** → **NVIDIA 侧槽位是 NULL,会直接失败** |
| 自造小 kernel 代替 | **违反 Jira 原文** |
| 完全不动测试源,只跑 T1–T4 | **无法证明 `launchKernel` 本身可用**(它无调用者)—— 这正是 D1 的代价 |

**⚠️ 可选:** 若放弃 A1、只保留 A2+A3,则 **M3 可删** —— 因为 `flagcxIntraTestCoopGroupsKernel(int *results)` 参数只有 `int*`,驱动能自行装配,`device_api.cu` 只需 3 行指针导出。
**但不建议删**:A1 覆盖的是 `void **args` 语义里**更难的一种** —— `cudaLaunchKernel` 的 `kernelParams` 要求"每个元素指向**参数值**",对按值结构体必须是**结构体在 host 栈上的地址**;若实现者误当"指针参数的指针"处理就会错。**A1 正是在压这个点。** M3 仅约 12 行,且与该文件既有 launcher 风格一致。

> **性质区分**:**M1/M2 = 产品改动**(`ixcuda_adaptor.cc`);**M3/M4/M5 = 测试脚手架**,只为满足 Jira 那句验收要求。M3/M4 属**公共测试源**(【文档 §13.2】归 FlagCX 项目负责)→ 应作 **upstream patch** 提出,不混在厂商适配提交里。

**M3 的代码**(桥接,理由见「验证清单」):

```c
// test/kernel/iluvatar/device_api.cu —— ⚠️ 需新增 #include "adaptor.h"
// (deviceAdaptor 声明在 flagcx/adaptor/include/adaptor.h:61,本 TU 现有 include 链里没有)
flagcxResult_t launchKernelCommQueriesViaAdaptor(flagcxDevMem_t devMem,
                                                 flagcxDevComm_t devComm,
                                                 int *results,
                                                 flagcxStream_t stream) {
  if (!devMem || !devComm || !results) return flagcxInvalidArgument;
  flagcxDevMem dm(*devMem);            // 按值构造,与既有 launcher 一致
  flagcxDevComm dc(*devComm);
  void *args[] = {&dm, &dc, &results};
  return deviceAdaptor->launchKernel((void *)flagcxIntraTestCommQueriesKernel,
                                     32, 1, 1,   // block ← 对应既有 <<<1, 32>>>
                                     1,  1, 1,   // grid
                                     args, 0, stream, nullptr);
}

flagcxResult_t launchKernelCoopGroupsViaAdaptor(int *results,
                                                flagcxStream_t stream) {
  if (!results) return flagcxInvalidArgument;
  void *args[] = {&results};
  return deviceAdaptor->launchKernel((void *)flagcxIntraTestCoopGroupsKernel,
                                     256, 1, 1,  // block ← 对应既有 <<<4, 256>>>
                                     4,   1, 1,  // grid
                                     args, 0, stream, nullptr);
}
```

**M4 的代码**:
```c
// test/kernel/nvidia/device_ir.cu(kernelCommQueriesS 所在处)
extern "C" void *flagcxIluvatarKernelCommQueriesSPtr(void) {
  return (void *)kernelCommQueriesS;
}
// → 声明进 test/kernel/include/device_ir.h
```

**M2 的两个禁忌**:
- ⚠️ **只能替换 `:441` 这一个 token**,不能增删逗号或换行 —— 它是**位置初始化器**,错位后类型相同的槽位会**静默接错且不报错**。
- ⚠️ 若 M1 插在 `:288` 之后,`:441` 的**行号会因 M1 插入而位移**,接线时按"内容"定位而不是按行号。

#### 明确**不做**的事(避免范围蔓延)

| 不做 | 理由 |
|---|---|
| `copyArgsInit` / `copyArgsFree` / `launchDeviceFunc` 保持 `NULL` | 不在【文档 §4.1 P0-Core】必测集内;配套的 `flagcxFuncArgs`(`flagcx.h:136-141`)全仓库只出现它自己的定义一次,该设计线已被放弃 |
| 不打通公共 API `flagcxDeviceHandle` | 【文档 §13.2】公共 ABI 归 FlagCX 项目负责;厂商越界 |
| 不整理 `.cu` 里的 `// K<n>:` 历史注释 | 纯注释、不影响功能,改了只是制造无意义 diff |
| 不动 `iluvatar_platform_traits.h` 的 trap | 那是 **S3** 的范围;为了"消 trap"而扩宽掩码会引入挂死 |
| 不用"自造小 kernel"替代真实 kernel 做验证 | **Jira 原文要求**用 `device_api.cu` / `device_ir.cu` 编出的 kernel |

#### 完成定义(DoD)

- ☐ M1–M5 全部落地,`git diff` 可逐处对应上表
- ☐ Linux 编译通过,且 `nm -C build/obj/.../ixcuda_adaptor.o | grep -i launchkernel` 有输出
- ☐ `nm -D --undefined-only build/lib/libflagcx.so | grep -i devapibackend` **无输出**
- ☐ 验证清单 A1/A2/A3 全 PASS(**黄金对照 + 绝对值双断言**)
- ☐ 验证清单 B1–B5 全 PASS(失败必返回真实错误,不 silent no-op)
- ☐ 交付备注写明:**`launchKernel` 当前无生产调用者**,覆盖来自测试侧主动调用

---

#### 验证方法(按 **Jira 原文要求**设计)

> **Jira 原文的验收判据是两句**:
> ① 「使 `test/kernel/iluvatar/device_api.cu` 和 `device_ir.cu` **编出的 kernel 能被启动**」
> ② 「**启动失败时返回真实错误**」
>
> ⚠️ **因此不能用"自造的小 kernel"来验** —— 必须用这两个文件里**真实存在**的 kernel。以下设计严格对应这两句。

**一眼看全(7 个用例)**:

| 用例 | 类型 | 载体 kernel | 配置 | 证明什么 | 落在哪个 driver |
|---|---|---|---|---|---|
| **A1** | 正例 | `flagcxIntraTestCommQueriesKernel` | block=32, grid=1 | 能启动 + **参数/结果等价于 `<<<>>>` 老路径** | T1 `test_device_api_intra.cpp` |
| **A2** | 正例 | `flagcxIntraTestCoopGroupsKernel` | block=256, grid=4 | 同上 + **唯一能抓 block/grid 转置** | T1 同上 |
| **A3** | 正例 | `kernelCommQueriesS` | block=1, grid=1 | `device_ir.cu` 的 kernel 也能经槽位启动 | T3 `test_device_ir_unified_intra.cpp` |
| **B1** | 负例 | 同上任一真实 kernel | `func=NULL` | 参数校验:返回 `flagcxInvalidArgument` | 同 A1/A3 |
| **B2** | 负例 | 同上 | `args=NULL` | 同上 | 同上 |
| **B3** | 负例 | 同上 | 维度含 0 | 同上 | 同上 |
| **B4** | 负例 | 同上 | `block_x=4096`(超上限) | **厂商运行期真实错误被如实上报**(非 `flagcxSuccess`) | 同上 |
| B5 | 断言 | — | — | B1–B4 统一要求"**不 silent no-op**" | — |

**本机 / Linux 分工**:

| 工作 | 在哪 |
|---|---|
| M1–M5 全部代码改动 | ✅ **本机** |
| A1–A3 / B1–B5 的测试代码 | ✅ **本机** |
| 编译、`nm` 自检、跑 T1/T3 | ❌ **Linux + CoreX + 8 卡** |
| **必须实测才能定论的一点**:CoreX 是否提供 `cudaLaunchKernel` | ❌ **Linux**(编译即可判定;若缺失见下方回退) |

**若 `cudaLaunchKernel` 在 CoreX 上不可用(编译失败)的回退方案**:

| 方案 | 做法 | 代价 |
|---|---|---|
| **R1** | 改用驱动 API `cuLaunchKernel(CUfunction, ...)` | 需要 `CUfunction`,可能得 `cuModuleGetFunction` 取 → 明显更重 |
| **R2** | 用 CoreX 的 native 启动 API(若有,如 `ixcuda*Launch*`) | 先确认它是否接受"**函数指针 + `void **` 参数数组**";**若只接受逐个具名参数,则无法满足签名,此路不通** |
| **R3** | 槽位改为返回 `flagcxNotSupported` 的桩,并登记"部分支持" | **不阻塞 T1–T4 验收**(它无调用者),但放弃 S1 的能力 |

> **⚠️ 编号体系警告(务必先读)**:仓库里存在**三套互不通用**的 `K<n>` 编号,【文档】用的是**测试 driver 那套**,而 `.cu` 文件里的 `K<n>` 注释**自相矛盾、不可靠**。**本文档一律用 kernel 符号名引用**,不用 `.cu` 的 K 编号。详见 §5.1.1。

**桥接:`launchKernel` 需要 `void *func` + `void **args`,而现有 launcher 用的是 `<<<>>>`,所以要先搭两条桥。**

| 文件 | 可用的真实 kernel(**用符号名引用**) | 启动配置 | 桥接方式 | 原因 |
|---|---|---|---|---|
| `device_api.cu` | **`flagcxIntraTestCommQueriesKernel`**(`test/kernel/nvidia/device_api.cu:1261-1278`;该文件旧注释标为 "K9") | `<<<1, 32>>>` → FlagCX 侧 **block=32, grid=1** | **在 kernel TU 内新增 launcher**(内部取 `deviceAdaptor`) | 它的参数是 `flagcxDevMem`/`flagcxDevComm` **按值**的 C++ 设备侧类型,**只能在能看见这些类型的 TU 里构造** |
| `device_api.cu` | **`flagcxIntraTestCoopGroupsKernel`**(`:1294-1366`;旧注释标为 "K10") | `<<<4, 256>>>` → **block=256, grid=4** | 同上 | 同上;**且它是转置检测的关键**(见下) |
| `device_ir.cu` | **`kernelCommQueriesS`**(`test/kernel/nvidia/device_ir.cu:41-48`;IR 侧标为 **S1**,这是**可靠**的) | `<<<1, 1>>>` → **block=1, grid=1** | **只导出 kernel 指针**即可 | 它的参数是**裸设备指针**(`const void *devCommPtr`, `int *results`),`.cpp` 能自己装 `args[]` |

**桥 1(`device_api.cu` 侧,新增一个 launcher)**:
```c
// test/kernel/iluvatar/device_api.cu  —— 该文件已 #include "../nvidia/device_api.cu"
// ⚠️ 需确认/新增 #include "adaptor.h",因为 deviceAdaptor 声明在
//    flagcx/adaptor/include/adaptor.h:61,而本 TU 现有 include 链里没有它
flagcxResult_t launchKernelCommQueriesViaAdaptor(flagcxDevMem_t devMem,
                                                 flagcxDevComm_t devComm,
                                                 int *results,
                                                 flagcxStream_t stream) {
  if (!devMem || !devComm || !results)
    return flagcxInvalidArgument;
  flagcxDevMem dm(*devMem);            // 按值构造,与既有 launcher 完全一致
  flagcxDevComm dc(*devComm);
  void *args[] = {&dm, &dc, &results}; // ← 参数地址数组,正是 cudaLaunchKernel 的模型
  return deviceAdaptor->launchKernel((void *)flagcxIntraTestCommQueriesKernel,
                                     32, 1, 1,   // block(对应老配置 block=32)
                                     1,  1, 1,   // grid (对应老配置 grid =1)
                                     args, 0, stream, nullptr);
}
```

**桥 2(`device_ir.cu` 侧,只导出 kernel 指针)**:
```c
// test/kernel/nvidia/device_ir.cu(kernelCommQueriesS 所在处)
extern "C" void *flagcxIluvatarKernelCommQueriesSPtr(void) {
  return (void *)kernelCommQueriesS;
}
// → 声明进 test/kernel/include/device_ir.h,供 .cpp 使用
// .cpp 侧自行装配:
//   void *args[] = {&devCommPtr, &devResults};
//   deviceAdaptor->launchKernel(ptr, 1,1,1, 1,1,1, args, 0, stream, nullptr);
```
⚠️ `.cpp` 需要 `#include "adaptor.h"` 才能访问 `deviceAdaptor`(参照 `test/unittest/adaptor/test_device_adaptor.cpp:11`)。注意:**公共 API `flagcxDeviceHandle` 里没有 `launchKernel` 字段**(`flagcx.h:143-185`),所以只能走内部 `deviceAdaptor`。

---

**正例 —— 证明"真实 kernel 能被启动"(Jira ①)**

**⭐ 做法:黄金对照(golden reference)。** 同一次运行里,把同一个 kernel 用**两条路**各启动一次,要求**结果逐字节相同**:

```
老路径(已被 T1–T4 验证过的):launchKernelCommQueries(...)          ← <<<>>>
新路径(被测的):            launchKernelCommQueriesViaAdaptor(...)  ← deviceAdaptor->launchKernel
                                    ↓
              同一 devMem/devComm、同样两次 deviceMemset 清零
                                    ↓
              deviceMemcpy 拷回后 memcmp(resultsOld, resultsNew, N) == 0
```

**为什么这个对照最强**:它不只证明"没崩",而是证明**新路径与既有可信路径语义等价**(参数传递、维度、stream 全部正确)。

| 编号 | 载体(符号名) | 断言 |
|---|---|---|
| **A1** | `flagcxIntraTestCommQueriesKernel`(block=32, grid=1) | 两条路 `results[0..5]` **逐字节相同**(`hasWindow`/`intraRank`/`intraSize`/`rank`/`size`/`peerPtrs`) |
| **A2** | `flagcxIntraTestCoopGroupsKernel`(block=256, grid=4) | 两条路结果相同;**兼作转置检测** |
| **A3** | `kernelCommQueriesS`(`<<<1,1>>>`) | 两条路 `results[0..3]` 相同(`rank`/`size`/`intraRank`/`intraSize`) |

**🔴 为什么 A2 必须做 —— 它是唯一能抓"block/grid 转置"的用例:**
- A1/A3 的 kernel **只从 `(blockIdx.x==0 && threadIdx.x==0)` 写结果**(`device_api.cu:1264`、`device_ir.cu:42`),所以**即使转置了,结果仍可能是对的** → 抓不到!
- A2 的 `flagcxIntraTestCoopGroupsKernel` **用 `FLAGCX_BLOCK_DIM_X` 参与 tile 运算**(`nTiles = blockDim.x / FLAGCX_SIMT_WIDTH`)。转置后 `blockDim.x` 从 256 变成 4 → `nTiles` 变 0 → 命中 `CoopTileSpan` 的 `count<=0` → **trap**(见 S3 的 trap 287 分析)。**转置必然暴露,而不是静默错。**

**🔴 一条刚发现的硬约束:A3 不能放在 T1 的 driver 里。**

【代码】`test/unittest/device_api/Makefile:18-19,146-174`:
```make
DEVICE_API_OBJ := $(KERNEL_OBJ_DIR)/device_api.o
DEVICE_IR_OBJ  := $(KERNEL_OBJ_DIR)/device_ir.o
$(BINDIR)/test_device_api_intra: test_device_api_intra.cpp $(DEVICE_API_OBJ) ...   # 只链 device_api.o
$(BINDIR)/test_device_ir_unified_intra: ... $(DEVICE_IR_OBJ) ...                   # 只链 device_ir.o
```
**T1/T2 只链 `device_api.o`,T3/T4(及 legacy IR)只链 `device_ir.o`** —— 两个 kernel 对象在**不同的可执行文件**里。所以:

| 用例 | 必须落在 | 因为 |
|---|---|---|
| A1、A2(`device_api.cu` 的 kernel) | **T1 driver** `test_device_api_intra.cpp` | 只有它链了 `device_api.o` |
| A3(`device_ir.cu` 的 kernel) | **T3 driver** `test_device_ir_unified_intra.cpp`(或 legacy `test_device_ir_intra.cpp`) | 只有它们链了 `device_ir.o` |

⇒ **好消息:S1 的验证会让 T1 和 T3 都获得覆盖。**

---

##### A1 / A2 的具体代码(加进 `test/unittest/device_api/test_device_api_intra.cpp`)

**先加两个桥接 launcher**(放 `test/kernel/iluvatar/device_api.cu`;⚠️ 需新增 `#include "adaptor.h"` 才能访问 `deviceAdaptor`,`adaptor.h:61`):
```c
flagcxResult_t launchKernelCommQueriesViaAdaptor(flagcxDevMem_t devMem,
                                                 flagcxDevComm_t devComm,
                                                 int *results,
                                                 flagcxStream_t stream) {
  if (!devMem || !devComm || !results) return flagcxInvalidArgument;
  flagcxDevMem dm(*devMem);            // 按值构造,与既有 launcher 一致
  flagcxDevComm dc(*devComm);
  void *args[] = {&dm, &dc, &results};
  return deviceAdaptor->launchKernel((void *)flagcxIntraTestCommQueriesKernel,
                                     32, 1, 1,   // block  ← 对应既有 <<<1, 32>>>
                                     1,  1, 1,   // grid
                                     args, 0, stream, nullptr);
}

flagcxResult_t launchKernelCoopGroupsViaAdaptor(int *results,
                                                flagcxStream_t stream) {
  if (!results) return flagcxInvalidArgument;
  void *args[] = {&results};
  return deviceAdaptor->launchKernel((void *)flagcxIntraTestCoopGroupsKernel,
                                     256, 1, 1,  // block  ← 对应既有 <<<4, 256>>>
                                     4,   1, 1,  // grid
                                     args, 0, stream, nullptr);
}
```
并把这两个 launcher 声明进 `test/kernel/include/device_api.h`。

**再加 driver 里的黄金对照**(风格照抄紧邻的 K1/K2 段,`:150-187`):
```c
// ---- A1: device_api.cu 的 flagcxIntraTestCommQueriesKernel,黄金对照 ----
{
  int refResults[6] = {0}, newResults[6] = {0};

  // 老路径 <<<>>>
  FLAGCXCHECK(devHandle->deviceMemset(devResults, 0, 6 * sizeof(int),
                                      flagcxMemDevice, stream));
  FLAGCXCHECK(launchKernelCommQueries(devMem, devComm, devResults, stream));
  FLAGCXCHECK(devHandle->streamSynchronize(stream));
  FLAGCXCHECK(devHandle->deviceMemcpy(refResults, devResults, 6 * sizeof(int),
                                      flagcxMemcpyDeviceToHost, stream));

  // 新路径 deviceAdaptor->launchKernel
  FLAGCXCHECK(devHandle->deviceMemset(devResults, 0, 6 * sizeof(int),
                                      flagcxMemDevice, stream));
  FLAGCXCHECK(launchKernelCommQueriesViaAdaptor(devMem, devComm, devResults, stream));
  FLAGCXCHECK(devHandle->streamSynchronize(stream));
  FLAGCXCHECK(devHandle->deviceMemcpy(newResults, devResults, 6 * sizeof(int),
                                      flagcxMemcpyDeviceToHost, stream));

  // ① 黄金对照:两条路逐字节相同(证明参数/维度/stream 传递正确)
  bool same = (memcmp(refResults, newResults, 6 * sizeof(int)) == 0);
  // ② 绝对期望:两条路都对(防止"两条路错得一样"也通过!)
  bool abs  = (newResults[0] == 1) &&          // hasWindow
              (newResults[1] == proc) &&       // intraRank
              (newResults[2] == totalProcs) && // intraSize
              (newResults[3] == proc) &&       // rank
              (newResults[4] == totalProcs);   // size
  printResult("A1 LaunchKernel::CommQueries", same && abs, proc);
  allPass &= (same && abs);
}

// ---- A2: device_api.cu 的 flagcxIntraTestCoopGroupsKernel,黄金对照 + 转置检测 ----
{
  int refResults[16] = {0}, newResults[16] = {0};

  FLAGCXCHECK(devHandle->deviceMemset(devResults, 0, 16 * sizeof(int),
                                      flagcxMemDevice, stream));
  FLAGCXCHECK(launchKernelCoopGroups(devResults, stream));
  FLAGCXCHECK(devHandle->streamSynchronize(stream));
  FLAGCXCHECK(devHandle->deviceMemcpy(refResults, devResults, 16 * sizeof(int),
                                      flagcxMemcpyDeviceToHost, stream));

  FLAGCXCHECK(devHandle->deviceMemset(devResults, 0, 16 * sizeof(int),
                                      flagcxMemDevice, stream));
  FLAGCXCHECK(launchKernelCoopGroupsViaAdaptor(devResults, stream));
  FLAGCXCHECK(devHandle->streamSynchronize(stream));   // ← 转置会在此 trap
  FLAGCXCHECK(devHandle->deviceMemcpy(newResults, devResults, 16 * sizeof(int),
                                      flagcxMemcpyDeviceToHost, stream));

  bool same = (memcmp(refResults, newResults, 16 * sizeof(int)) == 0);
  bool abs  = (newResults[0] == 1) && (newResults[1] == 1) &&
              (newResults[2] == 1) && (newResults[3] == 1) &&
              (newResults[4] == 1);
  printResult("A2 LaunchKernel::CoopGroups", same && abs, proc);
  allPass &= (same && abs);
}
```
> ⚠️ **`abs` 那一半不能省。** 只比"两条路是否相同"会漏掉**"两条路错得一样"**的情形(例如都用了错的维度)。**必须同时断言绝对值。**

##### A3 的具体代码(加进 `test/unittest/device_api/test_device_ir_unified_intra.cpp`)

**先在 `test/kernel/nvidia/device_ir.cu`(`kernelCommQueriesS` 所在处)导出 kernel 指针**:
```c
extern "C" void *flagcxIluvatarKernelCommQueriesSPtr(void) {
  return (void *)kernelCommQueriesS;
}
// → 声明进 test/kernel/include/device_ir.h
```
**driver 里**(该文件已有 `devCommPtr` / `devResults`,`:126-129` 一带):
```c
// ---- A3: device_ir.cu 的 kernelCommQueriesS,黄金对照 ----
{
  int refResults[4] = {0}, newResults[4] = {0};

  // 老路径 <<<1,1>>>
  FLAGCXCHECK(devHandle->deviceMemset(devResults, 0, 4 * sizeof(int),
                                      flagcxMemDevice, stream));
  launchKernelCommQueriesS(devCommPtr, devResults, stream);
  FLAGCXCHECK(devHandle->streamSynchronize(stream));
  FLAGCXCHECK(devHandle->deviceMemcpy(refResults, devResults, 4 * sizeof(int),
                                      flagcxMemcpyDeviceToHost, stream));

  // 新路径 deviceAdaptor->launchKernel(参数是裸设备指针,.cpp 可自行装配)
  void *func = flagcxIluvatarKernelCommQueriesSPtr();
  if (func == nullptr) { printResult("A3 LaunchKernel::IRCommQueries", false, proc); }
  else {
    FLAGCXCHECK(devHandle->deviceMemset(devResults, 0, 4 * sizeof(int),
                                        flagcxMemDevice, stream));
    void *args[] = {&devCommPtr, &devResults};
    FLAGCXCHECK(deviceAdaptor->launchKernel(func, 1, 1, 1, 1, 1, 1,
                                           args, 0, stream, nullptr));
    FLAGCXCHECK(devHandle->streamSynchronize(stream));
    FLAGCXCHECK(devHandle->deviceMemcpy(newResults, devResults, 4 * sizeof(int),
                                        flagcxMemcpyDeviceToHost, stream));

    bool same = (memcmp(refResults, newResults, 4 * sizeof(int)) == 0);
    bool abs  = (newResults[0] == proc) && (newResults[1] == totalProcs) &&
                (newResults[2] == proc) && (newResults[3] == totalProcs);
    printResult("A3 LaunchKernel::IRCommQueries", same && abs, proc);
    allPass &= (same && abs);
  }
}
```
> ⚠️ driver 需 `#include "adaptor.h"` 才能访问 `deviceAdaptor`(参照 `test/unittest/adaptor/test_device_adaptor.cpp:11`)。注意**公共 `flagcxDeviceHandle` 里没有 `launchKernel` 字段**(`flagcx.h:143-185`),只能走内部 `deviceAdaptor`。

##### B1–B5 的具体代码(负例,可与 A1 同处)

```c
// ---- B1–B5: 启动失败必须返回真实错误(不能 silent no-op)----
// goodFunc 取 A1/A3 已经拿到的**真实 kernel 指针**之一:
//   在 T1 driver 里可用 (void *)flagcxIntraTestCommQueriesKernel 的桥接指针;
//   在 T3 driver 里可用 flagcxIluvatarKernelCommQueriesSPtr()。
{
  void *goodFunc = flagcxIluvatarKernelCommQueriesSPtr();  // ← 按所在 driver 换成对应 kernel
  void *args[] = {&devCommPtr, &devResults};
  flagcxStream_t s = stream;

  // B1: func == NULL
  bool b1 = (deviceAdaptor->launchKernel(nullptr, 1,1,1, 1,1,1, args, 0, s, nullptr)
             == flagcxInvalidArgument);
  // B2: args == NULL
  bool b2 = (deviceAdaptor->launchKernel(goodFunc, 1,1,1, 1,1,1, nullptr, 0, s, nullptr)
             == flagcxInvalidArgument);
  // B3: 任一维度为 0
  bool b3 = (deviceAdaptor->launchKernel(goodFunc, 0,1,1, 1,1,1, args, 0, s, nullptr)
             == flagcxInvalidArgument);
  // B4: block_x=4096 超 CUDA 上限(1024)→ 运行期拒绝,必须是"被如实上报的错误"
  flagcxResult_t r4 = deviceAdaptor->launchKernel(goodFunc, 4096,1,1, 1,1,1,
                                                  args, 0, s, nullptr);
  bool b4 = (r4 != flagcxSuccess);   // 且 WARN 日志里应出现真实错误名
  // B5: 统一断言"不静默成功"(已包含在上面四条里)
  printResult("B LaunchKernel::error paths", b1 && b2 && b3 && b4, proc);
  allPass &= (b1 && b2 && b3 && b4);

  (void)cudaGetLastError();  // ← B4 之后必须清粘性错误,否则污染后续 case
}
```
> ⚠️ B4 之后**必须清掉粘性错误**(`cudaGetLastError()`),否则会污染后续 case(这正是 S2 要解决的问题 —— 可作为 S2 的现场证据)。
> ⚠️ **不要**用"非法 func 指针(如 `(void*)1`)"做负例,可能直接段错误。

#### 5.1.1 ⚠️ `K<n>` 编号体系警告(避免引用歧义)

仓库里存在**三套互不通用**的 `K<n>` 编号:

| 体系 | 位置 | 范围 | 可靠性 |
|---|---|---|---|
| **① `.cu` 的 kernel 标签** | `test/kernel/nvidia/device_api.cu` 的 `// K<n>:` **注释** | 混杂 intra + inter | ❌ **不可靠** |
| **② T1 driver 的用例编号** | `test/unittest/device_api/test_device_api_intra.cpp` | K1–K10 | ✅ 与 IR 的 **S1–S10 对齐**(`:7` 注释明说) |
| **③ T2 driver 的用例编号** | `test/unittest/device_api/test_device_api_inter.cpp` | K1–K15(+K4b) | ✅ `:7-22` 有完整清单 |

**① 为什么不可靠(实证)**:
- **K4 完全缺失**(grep `K4` 零匹配)
- **K11 出现两次**:`:1376` Team、`:1416` WaitSignal+Flush
- **K1 出现两次**:`:1020` Local Pointer、`:1555` DevNetGetFromComm
- **顺序乱**:K8(`:948`)排在 K1(`:1020`)之前
- intra 与 inter 的 kernel 混在同一套编号里

**②③ 之间也不通用**:两个 driver 各自的 K 含义不同(`intra K1` = CommQueries,`inter K1` = DevNetGetFromComm;`intra K10` = IntraAllReduce(composite),`inter K10` = Shadow,且**已注释掉**)。

**⇒ 铁律:引用 kernel 用【符号名 + 文件 + 行号】,引用验收 case 用【driver 文件名 + K 号】,两者不要混。**

**这正好解释了【文档 §13.3.3/§13.3.4】里的 "K10" 和 "K14"** —— 它们用的是**②③ 号体系**:

| 【文档】的说法 | 实际落点 |
|---|---|
| §13.3.3 T1 验收「**K10** IntraAllReduce(composite)」 | `test_device_api_intra.cpp:390` 的 `printResult("K10 IntraAllReduce(composite)", ...)` → kernel 是 `flagcxIntraAllReduceKernel` |
| §13.3.4 T2 验收「**K14** OneSidedAlltoAll」 | `test_device_api_inter.cpp:508` 的 `printResult("K14 OneSidedAlltoAll", ...)` → kernel 是 `flagcxInterOneSidedAlltoAllKernel` |

⚠️ 注意**撞车**:①号体系的 "K10" 是 **Coop Groups**,②号体系的 "K10" 是 **IntraAllReduce(composite)** —— 同一个编号指两个不同东西。**别用错。**

> 🔎 **顺带一个与 S3 互相印证的发现**:T2 driver `:423` 写着
> ```c
> // --- K8: Get --- SKIPPED (get unsupported on vendor path)
> printResult("K8 Get (SKIP)", true, proc);
> ```
> **"远端 Get(读)在 vendor 路径上不受支持"** —— 这与 S3 里标注的 **S19 DevGet**(= 读对端内存)隐患**完全呼应**。此外 `K10 Shadow`、`K15 TwoSided` 也被注释掉了。→ 这两处应在 S3 阶段一并核实:是"平台能力限制"还是"就绪度不足"。

---

**负例 —— 证明"启动失败时返回真实错误"(Jira ②)**

| 编号 | 输入 | 期望返回 | 依据 |
|---|---|---|---|
| **B1** | `func = NULL` | `flagcxInvalidArgument` | Host 侧可判定 |
| **B2** | `args = NULL` | `flagcxInvalidArgument` | Host 侧可判定 |
| **B3** | 任一维度为 `0` | `flagcxInvalidArgument` | Host 侧可判定 |
| **B4** | `block_x = 4096`(超 CUDA 的 1024 上限) | **非 `flagcxSuccess`**(映射为 `flagcxUnhandledDeviceError`)+ `WARN` 日志含真实错误名 | 这是唯一能验证"**厂商运行期的真实错误被如实上报**"的用例 |
| **B5** | 对上四条统一断言 | **返回值 `!= flagcxSuccess`** | 即【文档 §4.5】"**不能 silent no-op**" |

⚠️ **不要**用"传一个明显非法的 func 指针(如 `(void*)1`)"做负例 —— 可能直接段错误,不适合放进自动化测试。

---

**运行方式**
```bash
# 若 A1–A3/B1–B5 加进 T1 的 driver(见下方"建议"),则直接跑 T1 即可:
mpirun --allow-run-as-root -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_VMM_ENABLE=0 \
  -x LD_LIBRARY_PATH=$PWD/build/lib:$LD_LIBRARY_PATH \
  -np 8 test/unittest/device_api/build/bin/test_device_api_intra -b 1K -e 16M -f 2 -R 2

# 编译期符号自检
nm -C build/obj/flagcx/adaptor/device/ixcuda_adaptor.o | grep -i launchkernel
nm -C test/kernel/iluvatar/build/obj/device_api.o | grep -i Viadapter
nm -D --undefined-only build/lib/libflagcx.so | grep -i devapibackend   # 期望无输出
```

> 🎯 **【建议】把 A1–A3/B1–B5 直接加进 T1 的 driver(`test/unittest/device_api/test_device_api_intra.cpp`)**,理由是:
> - **T1 是 Jira 指定的验收程序** → 这样"S1 通过"就有验收程序本身背书,而不是一个游离的附加单测;
> - 同时也解决了"T1–T4 不覆盖 `launchKernel`"这个此前的缺口。
>
> ⚠️ 但这属于**改公共测试源**(【文档 §13.2】把公共 driver 归 FlagCX 项目负责)→ 应作为 **upstream patch** 提出,不混在厂商适配提交里。并且**必须加平台守卫**(`if (deviceAdaptor->launchKernel == nullptr) 跳过并打印说明`),否则 NVIDIA 侧(槽位仍是 NULL)会失败。

#### 5.1.2 目标环境实测档案 + **W2 执行方案**(供审阅)

> 通过 dsh skill `iluvatar-bastion-access` 实测(链路:Windows → JumpServer koko:2222 → 资产 `ae-bj-bd1` → 容器 `dapeng.he-newFlagCX`)。以下全部为**实测值**,非推断。

**环境事实**

| 项 | 实测值 | 影响 |
|---|---|---|
| 容器内仓库 | `/usr/local/corex-5.1.0/FlagCX` | ⚠️ `/usr/local/FlagCX` **不存在**,别用 |
| `/usr/local/corex*` | `/usr/local/corex`(链接)+ `/usr/local/corex-5.1.0` | 与 `iluvatar.mk:8` 默认 `DEVICE_HOME=/usr/local/corex` 吻合 ✅ |
| device 编译器 | `/usr/local/corex-5.1.0/bin/clang` → **clang-22**(22.1.0git, 5.1.0.20260920) | ✅ |
| MPI | **Open MPI 4.0.7**,`/usr/local/openmpi/bin/mpirun` | → mpirun 用 `-x`(不是 `--genv`);且需 `--allow-run-as-root`(容器内是 root) |
| GPU | 3 个设备节点,**但只有 GPU1 / GPU2 可用;GPU0 是坏的**(用户明确告警);`CUDA Version: 10.2` | 🔴 **见下方硬约束** |
| 磁盘 | 343G 可用;`.git` 221M + 工作树 202M | ✅ 冷构建无压力 |
| submodule | `third-party/googletest`、`third-party/json` 均已就位 | ✅ |
| 构建约定 | `USE_ILUVATAR=1 make -j$(nproc)`(见 `test/script/test.sh`) | ⚠️ 它**没带 `COMPILE_KERNEL=1`**,而【文档 §10.3】要求主库带该开关 → 我们的命令要显式加 |
| 现有产物 | `build/lib/libflagcx.so`(20MB,`2026-10-09 11:05` 构建,**对应他们的 WIP `27a5081`**) | ⚠️ W2 会覆盖它 → **先备份** |

**🔴🔴 硬约束:GPU0 是坏的,所有运行必须屏蔽它**

> **用户明确告警:只有 GPU1 和 GPU2 可用,GPU0 是坏的。**
> 这条极易踩坑,因为**测试默认会把 rank 0 绑到设备 0**:

【代码】`test/unittest/device_api/test_device_api_intra.cpp:74-75`:
```c
FLAGCXCHECK(devHandle->getDeviceCount(&nGpu));
FLAGCXCHECK(devHandle->setDevice(worldRank % nGpu));   // → rank 0 绑 setDevice(0)
```
⇒ **不加掩码时 `nGpu=3`,rank 0 → 物理 GPU0 = 坏卡** → 会得到误导性的失败/挂死。

**实测确认掩码有效**(CoreX 的 CUDA runtime 承认 `CUDA_VISIBLE_DEVICES`):

| 掩码 | `cudaGetDeviceCount` | 逻辑 dev0 | 逻辑 dev1 |
|---|---|---|---|
| 无 | 3 | MR-V50(物理 GPU0,**坏**) | MR-V50(GPU1) |
| `CUDA_VISIBLE_DEVICES=1` | **1** | **MR-V50(物理 GPU1)** ✅ | — |
| `CUDA_VISIBLE_DEVICES=1,2` | **2** | **MR-V50(物理 GPU1)** ✅ | **MR-V100(物理 GPU2)** ✅ |

⇒ **`CUDA_VISIBLE_DEVICES=1,2` 时 `nGpu=2`,rank0→逻辑0→物理 GPU1 ✅;掩码是强制的,不是可选的。**

**其它实测结论**:
- FlagCX **自身没有**设备选择环境变量(`grep CUDA_VISIBLE_DEVICES flagcx/` 零命中)→ `CUDA_VISIBLE_DEVICES` 是**唯一**手段。
- **`ivcore11`(`iluvatar.mk:13` 默认)在 MR-V50 与 MR-V100 上都能正常启动 kernel** ✅(探针两卡均 `launch=no error val=42`)。
- 探针用**无掩码**时连 GPU0 也跑通了单个玩具 kernel(`1 block × 1 thread`)—— **但这不与"GPU0 是坏的"矛盾**(玩具 kernel ≠ 真实负载),**我方仍一律屏蔽 GPU0**。

**能跑 / 不能跑(基于"只有 2 张可用卡")**

| 工作 | 能否在容器内完成 | 说明 |
|---|---|---|
| 全量编译 | ✅ | `USE_ILUVATAR=1 COMPILE_KERNEL=1 make -j` |
| A1 / A2 / A3(launchKernel 功能 + 转置检测) | ✅ **`-np 1` 优先** | 见下方"为什么优先单卡" |
| B1–B5(失败返回真实错误) | ✅ `-np 1` | 单卡足够 |
| S3 的 `__syncwarp()`/`__activemask()` 微基准 | ✅ `-np 1` | 单卡 |
| S6 的 IPC 15 用例 | ✅ `-np 2`(该测试本身只需要 2 rank) | `CUDA_VISIBLE_DEVICES=1,2` |
| **`-np 3` 及以上** | ❌ **禁止** | 掩码后只有 2 张;不加掩码会落到坏卡 GPU0 |
| **正式 T1–T4 验收** | ❌ **不行** | 【文档 §13.3.1】要求 **8 ranks × 8 设备**(且需同构);这里只 2 张且**型号异构**(MR-V50 + MR-V100) |

**为什么 A1/A2/A3 优先 `-np 1` 而不是 `-np 2`**:
1. **S1 验的是"`launchKernel` 能否正确启动 kernel"** —— 与 peer 数量无关;单 rank 就能完成黄金对照与转置检测。
2. **完全避开异构 IPC 的不确定性** —— 可用两卡是 `MR-V50` 与 `MR-V100` **不同芯片**,跨型号的 `cudaIpcOpenMemHandle` / peer access 是否可用**未经证实**;而 `-np 2` 会强制走 peer 指针路径。
3. 完全避开坏卡 GPU0。
> 若 `-np 1` 下 devComm/devMem 构造不成立,再退到 `-np 2` + `CUDA_VISIBLE_DEVICES=1,2`,并接受异构 IPC 风险。

⇒ **容器足以完成 S1 的编码 + 功能验证**;正式 T1–T4 必须另找 **8 卡同构**机器。

**基线事实**(已在容器内 `git fetch origin --prune`,只更新 remote refs,未动工作树/分支)

| 项 | 值 |
|---|---|
| 容器 `origin/main` | **`94dc699`**(= plan 基线 `ff5f80c` **+ 1 个 commit `94dc699`**) |
| 该 commit 改动 | 仅 `flagcx/core/flagcx_hetero.cc` + `test/unittest/rma/*`;grep 确认**未动** `ixcuda`/`iluvatar`/`device_adaptor`/`test/kernel` → **对 S1 无影响** |
| plan 结论是否仍成立 | ✅ 成立(S3 依赖的设备侧文件在 6 个 commit 里**全部未动**) |
| 容器当前分支 | `fix/p2p-queue-walk`(`27a5081`,父=`250ee84`) |
| 其他分支 | `fix/p2p-group-deadlock-v2`(`44e43ee`,仅改文档)、`fix/p2p-hang-matrix`(`350f96e`,仅改文档)、`main`(`250ee84`) |
| stash | `stash@{0}: On main: dsh-5fixes-wip-before-main-sync` |
| untracked(5 个) | `plugin/torch/example/TorchAPI_result.log`、`test/perf/host_api/.test_core_sendrecv.cpp.swp`、`test/perf/host_api/perf_logs/`、`test/perf/host_api/test_self_sendrecv.cpp`、`test/script/perf_host_api_2card.sh` |
| untracked 与上游改动同名? | ✅ **无冲突**(已逐条比对) |

> ⚠️ **提前告知(不是我们要解决的问题)**:他们的 `fix/p2p-queue-walk` 改的是 `flagcx/core/proxy.cc`,而上游 6 个 commit **也改了 `proxy.cc`** → 将来 rebase/merge 到新 main 时**必然在这一个文件冲突**(改动量 +32/−28,冲突面不大)。

**W2 执行步骤(在原仓库切新分支 —— 含保护措施)**

```bash
cd /usr/local/corex-5.1.0/FlagCX

# ---- ① 先留退路(把当前状态与产物固化下来)----
git rev-parse HEAD                      # 期望 27a5081...
git branch -vv
git stash list
B=/root/flagcx-wip-backup-$(date +%F_%H%M)
mkdir -p "$B"
cp build/lib/libflagcx.so "$B/libflagcx.so.27a5081"     # 备份他们 WIP 的构建产物
git bundle create "$B/all-refs.bundle" --all           # 备份全部分支+refs(可离线恢复)
echo "backup at $B"; ls -la "$B"

# ---- ② 建新分支(不改动他们的分支/stash/untracked)----
git fetch origin --prune
git checkout -b feat/iluvatar-launchkernel origin/main
git --no-pager log --oneline -1        # 期望 94dc699
git status --short                      # 应只剩那 5 个 untracked
git submodule status

# ---- ③ 基线切换后必须 clean rebuild(【文档 §10.3】)----
make clean
make -C test/kernel clean
make -C test/unittest/device_api clean
```

**风险与回滚**:

| 风险 | 缓解 |
|---|---|
| 覆盖他们的 `libflagcx.so` | 步骤①已备份;必要时拷回 |
| 切走他们的当前分支 | 分支本身仍在(`fix/p2p-queue-walk`),随时 `git checkout` 回去 |
| 丢 stash | 未触碰;且已 `git bundle --all` 兜底 |
| untracked 文件被覆盖 | ✅ 已验证与上游改动无同名,不会被 checkout 覆盖 |

**编译命令(适配容器)**

```bash
cd /usr/local/corex-5.1.0/FlagCX
# 主库(【文档 §10.3】要求 COMPILE_KERNEL=1;他们的 test.sh 没带,我们显式加)
USE_ILUVATAR=1 COMPILE_KERNEL=1 make -j$(nproc)
# 四个验收程序
make -C test/unittest/device_api -j$(nproc) USE_ILUVATAR=1 COMPILE_KERNEL=1 \
     MPI_HOME=/usr/local/openmpi
# ⚠️ 待确认:test/script/test.sh 里写的是 MPI_HOME=/usr/local/mpi,需核实该路径是否存在
```

**验证的容器化调整(2 可用卡 + 屏蔽坏卡)**

> ⚠️ **每条命令都必须带 `-x CUDA_VISIBLE_DEVICES=...`**(mpirun 的 `-x` 把变量传进各 rank)。
> 掩码值:`-np 1` → `CUDA_VISIBLE_DEVICES=1`;`-np 2` → `CUDA_VISIBLE_DEVICES=1,2`。

| 用例 | 容器内命令(功能冒烟) | 正式(T1/T3,8 卡同构) |
|---|---|---|
| **A1/A2/A3/B\*(推荐)** | `mpirun --allow-run-as-root -np 1 -x CUDA_VISIBLE_DEVICES=1 -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_VMM_ENABLE=0 -x LD_LIBRARY_PATH test/unittest/device_api/build/bin/test_device_api_intra` | `-np 8 ... -b 1K -e 16M -f 2 -R 2` |
| A1/A2/A3/B\*(若单卡不成立) | 同上但 `-np 2 -x CUDA_VISIBLE_DEVICES=1,2` | 同上 |
| S6 IPC 15 用例 | `make MPI_NP=2` + `mpirun --allow-run-as-root -np 2 -x CUDA_VISIBLE_DEVICES=1,2 ... --gtest_filter='IpcMemHandleMpiTest.*'` | — |

> ⚠️ **必须明确标注**:容器内的 `-np 1` / `-np 2` 运行是**功能冒烟**,**不是**验收;不得据此填写 §7.2 的结果表。
> ⚠️ **禁止 `-np 3` 及以上** —— 掩码后只有 2 张可用卡。

#### 5.1.3 🔴 容器内实测阻塞项(既有缺陷,阻断全部 T1–T4)

> **实测于 2026-10-10,容器 `dapeng.he-newFlagCX`,基线 `94dc699`。两条均用【完全 pristine 的源码】复现,与本次改动无关。**

**结论:`test/kernel/iluvatar/` 在这个容器里从未能编译过 ⇒ T1–T4 全部无法构建 ⇒ 全部验收不可执行。**
这解释了为什么容器内 `test/unittest/device_api/build/` 目录不存在(从未构建过设备侧测试)。

| # | 阻塞点 | 精确证据 | 阻断 | 性质 |
|---|---|---|---|---|
| **①** | `test/kernel/iluvatar/device_api.cu` → **CoreX `llc` 崩溃** | `IluvatarDAGToDAGISel::Select` → `SelectionDAGISel::CannotYetSelect` → `abort()`;`Running pass 'Iluvatar DAG->DAG Pattern Instruction Selection' on function '@_Z33flagcxInterOneSidedAlltoAllKernel…'`。**pristine 复现;`-O0/-O1/-O2` 全崩** | `device_api.o` → **T1/T2** | **工具链缺陷**:后端缺少某个 IR 构造的指令模式,于是 abort 而非报错 |
| **②** | `test/kernel/nvidia/device_ir.cu:163` → **地址空间隐式转换失败** | `void *localPtr = flagcxGetLocalPointerS(devMemPtr, 0);` → `error: cannot initialize a variable of type 'void *' with an rvalue of type 'FLAGCX_IR_GLOBAL_RETURN_PTR void *'`。根因:`device_utils.h:50` 在 CoreX 设备 pass 下把 `FLAGCX_IR_GLOBAL_RETURN_PTR` 定义为 `__attribute__((address_space(1)))` | `device_ir.o` → **T3/T4** | **上游测试源码缺陷**(全文件仅 1 处) |

**对 S1 的直接影响**:
- **M1/M2(产品改动)✅ 已验证**:`USE_ILUVATAR=1 COMPILE_KERNEL=1 make -j32` 成功;`nm` 确认 `T ixcudaAdaptorLaunchKernel(...)` 已编入;未解析符号自检干净;**并且证实 `cudaLaunchKernel` 在 CoreX 上存在**(编译产物中出现 `U ixLaunchKernel`,即 CoreX 的厂商启动原语 —— 此前的【推断】被编译器证实)。
- **M3/M4/M5/M6 与 A1–A3、B1–B4 ❌ 全部无法构建/运行**,因为它们都需要 `test/kernel/iluvatar/{device_api,device_ir}.o`。
- 因此 **S1 的验收判据(「两个文件编出的 kernel 能被启动」)在本容器内无法完成**,直到 ①② 解决。

**复现材料已保存**:容器内 `/root/lk-llc-crash/`(预处理源 `device_api-f01824.cu` 10.9MB、13 份后端 IR、复现脚本 `device_api-f01824.sh`)。

**建议的处置**:
1. **① 上报 CoreX**(工具链 bug):附上已保存的复现材料;并尝试缩小到"哪个 SDNode 无法选择"。它恰好崩在 T2 的验收对象 `flagcxInterOneSidedAlltoAllKernel` 上。
2. **② 修 1 行**(属公共测试源):但需先确认 CoreX 的地址空间语义(应否用 `FLAGCX_DEVICE_GLOBAL_PTR_CAST` 而非裸 `(void *)`),**不可臆测**。
3. **在 ①② 解决前**,S1 的可交付物只剩 **M1/M2 的编译级验证**;A/B 用例应作为"待可构建后执行"的脚手架提交,并在备注中登记"未通过运行验证"。

#### 三个待定决策(✅ 已拍板)



**最终结论(与用户确认)**:

| # | 决策 | **最终取值** |
|---|---|---|
| **①** | 错误码粒度 | **方案 A** —— Host 侧可判定 → `flagcxInvalidArgument`;厂商错误 → `flagcxUnhandledDeviceError` + `WARN` 带 `cudaGetErrorString` 真实错误名 |
| **②** | 是否消费粘性错误 | **不消费** —— 只读 `cudaLaunchKernel` 返回值,不调 `cudaGetLastError()` |
| **③** | 验证代码落点 | **β:独立 MPI 文件 `test/unittest/adaptor/coll_launch_kernel.cpp`**;且**用【单独 target】**(`adaptor_launch_kernel_tests`),**不并入** `adaptor_mpi_tests` —— 避免把"需要 CoreX 设备编译器"的负担转嫁给现有所有 `coll_*` 测试 |

**附加要求**:验证成功后 **`git push` 到 `https://github.com/lonely-log/FlagCX`**(只推新分支 `feat/iluvatar-launchkernel`,不动上游与其 WIP 分支)。

> **β 的固有代价(已接受)**:新测试需**复制 T1 driver 的 ~60 行 setup**(`test_device_api_intra.cpp:58-112` 的 MPI + `flagcxCommInitRank` + window register + `flagcxDevMemCreate`)。
> **β 的收益**:A1(按值结构体形态)得以保留 → **M3 维持完整 launcher 形态**。

#### 三个待定决策(原始权衡记录,供回查)

> ⚠️ **别和 §8 混**:这里是 **S1 范围内 3 条待你拍板**的实现细节;
> **§8「决策记录 D1–D7」是另一回事** —— 那是我方因 Jira 提交人无时间回复而**已自行拍定**的 7 条(见 `### D1 —` … `### D7 —`)。
> 本表"默认取值"列 = 你不改就按它做。

| # | 决策点 | **默认取值(可直接开工)** | 备选 | 影响 |
|---|---|---|---|---|
| **①** | 错误码映射到多细? | Host 侧可判定 → `flagcxInvalidArgument`;厂商错误 → `flagcxUnhandledDeviceError` + `WARN` 带 `cudaGetErrorString` 原文 | 逐项映射 CUDA launch 错误 | **接口层无法更细** —— `flagcxResult_t` 只有 10 个值(`flagcx.h:21-33`),装不下厂商错误码。逐项映射还会依赖 CoreX 的错误枚举命名 |
| **②** | `launchKernel` 是否消费粘性错误? | **不消费**(只读 `cudaLaunchKernel` 返回值,不调 `cudaGetLastError()`) | 消费(启动失败后立即清) | 默认取值让 launch 与 `getLastError` **职责正交**,与【文档 §4.1】把两者列为两项独立能力一致。⚠️ 反面证据:DU 先例(`test/kernel/du/device_api.cu:289`)是**消费**的 —— 但那是因为 `<<<>>>` 无返回值,只能靠粘性状态 |
| **③** | 验证代码落在哪? | **A1/A2/B\* → T1 driver;A3 → T3 driver** | 独立新单测文件 | 落在验收 driver 里 → "S1 通过"**由验收程序本身背书**,并让 T1/T3 获得覆盖。⚠️ 但属**改公共测试源**(【§13.2】归 FlagCX 项目)→ 应作为 **upstream patch** 提出;且**必须加平台守卫**(`launchKernel == nullptr` 时跳过),否则 NVIDIA 侧(槽位仍 NULL)会失败 |

#### 风险与状态判定
- 真实现后,`launchKernel` 不再是风险项。**⚠️ 但必须诚实记录:它当前仍无生产调用者** —— 上述验证是**测试侧主动调用**槽位完成的,不代表 T1–T4 的原有 case 会经过它。交付备注里要写明这一点。
- `copyArgsInit` / `copyArgsFree` / `launchDeviceFunc` 保持 `NULL`,**必须在备注登记**为未支持(不在【§4.1】必测集内,不阻塞验收)。

---

### 5.2 S2 —— 补齐 `getLastError`

#### Jira 原文
> [P0] 补齐天数 DeviceAdaptor 的 getLastError,让 Host 能读到 kernel 和 runtime 失败,getLastError 现在是 NULL。补上后,kernel 启动失败和 runtime 调用失败都能在 Host 侧取到错误。

#### 【文档】依据
- **§4.1「错误处理」行**:「`getLastError` **或厂商等价机制**」,能力要求:「**能够在 kernel launch 和 runtime 调用失败时返回有效错误**」。
- 【文档 §4.5】:「所有 P0 接口返回值与实际执行结果一致,**不能 silent no-op**」。
- 【文档 §16.5】:「厂商需要确保 DeviceAdaptor **返回真实 IPC 结果**」。

#### 【代码】事实

| 事实 | 证据 |
|---|---|
| 槽位存在,在 `latest` 结构体里,**`v1` 没有** | `flagcx_device_adaptor.h:292` `flagcxResult_t (*getLastError)();` |
| 天数已预留位置,值为 `NULL` | `ixcuda_adaptor.cc:446` |
| 天数用的是 **`latest`** 结构体(有 `hostRegister`/`symPhysAlloc` 等 latest 独有字段) | `ixcuda_adaptor.cc:437-452` |
| **NVIDIA 有参考实现,只有 4 行** | `cuda_adaptor.cc:1106-1109`,注册在 `:1223` |
| 调用点(**已迁移**) | `flagcx/core/p2p_pointer.cc:94-95`(旧 `flagcx_p2p.cc:1641-1642`) |
| 该调用点**丢弃返回值**,用于清粘性错误 | `:1640-1644` |
| CCLAdaptor 有个**同名但完全不同**的函数 | `flagcx_ccl_adaptor.h:48` → 返回 `const char *`,带 `comm` 参数 |

#### 初学者注解:三个必须搞清的点

**① 这是个"取走就没了"的接口。**
CUDA 的 `cudaGetLastError()` 语义是**读取并清除**那个粘性错误。所以 `getLastError()` 不是"随时查询当前有没有错",而是"**把错误领走**"。谁先调谁拿到,第二个人拿到的是 `flagcxSuccess`。**它只能放在错误处理路径上,不能当状态查询反复调。**

**② 它拿不到错误内容,而且有两个同名函数容易混淆。**
签名是 **无参数、返回 `flagcxResult_t`**,所以最多只能知道"出错了,是系统错误",**拿不到错误文本,也不知道是哪个 kernel 挂的**。Jira 写"让 Host 能读到 kernel 和 runtime 失败",严格讲只能得到布尔级别的信息。想要文本要看另一套机制:`flagcx/service/debug.cc:18` 的 `flagcxLastError[1024]` 字符缓冲区。

| | DeviceAdaptor 的 | CCLAdaptor 的 |
|---|---|---|
| 声明 | `flagcx_device_adaptor.h:292` | `flagcx_ccl_adaptor.h:48` |
| 签名 | `flagcxResult_t (*)()` | `const char *(*)(flagcxInnerComm_t)` |
| 参数 | **无** | 有 comm |
| 返回值 | **结果码** | **错误字符串** |
| 天数状态 | **NULL**(要补) | 已实现(`ixnccl_adaptor.cc:33-34`) |

**这两个是完全不同的东西。** 你补的是**第一个**。

**③ 我先前的一个判断要修正。**
我最初以为"唯一调用点丢弃返回值"是个 bug。**核对之后确认:这是正确设计。** 该调用点的目的**就是消费/清除**粘性错误,不需要它的返回值。

**而真正的 bug 是:槽位为 NULL 时,`if (deviceAdaptor->getLastError)` 这个守卫会让清理动作被整体跳过 → 粘性错误残留 → 污染后续 runtime 调用。**

#### 为什么这条必须做(理由链**已因上游变更而改写**)

> ⚠️ **本节结论在 `ff5f80c` 上已变化。** 原分析基线(`250ee84`)里,天数的 `getPointerType` 是 `NotSupported`,于是"用 IPC 导出成功与否反推指针类型"是**唯一的类型判别手段**,`getLastError` 就是那条路径的清道夫 —— 这是当时 S2 最硬的理由。**现在上游给天数实现了权威 `getPointerType`,那条理由不再成立。**

**新的事实**(见 §0.4):
- 逻辑已迁到 **`flagcx/core/p2p_pointer.cc:15-101`** 的 `flagcxP2pDetectPointerType()`
- 天数有权威 `getPointerType`(`ixcuda_adaptor.cc:381-407`,接线 `:475`)→ **`typeKnown = true`**
- **若判定为 HOST,在 `p2p_pointer.cc:64-65` 直接返回,完全不碰 IPC、也不碰 `getLastError`**
- 天数**未**设置 `FLAGCX_DEVICE_ADAPTOR_INTERNAL_IPC_POINTER_INFERENCE`(`:477` 是 `INTERNAL_NONE`)→ `transitionalInference = false`(`:35-38`)

**⇒ 于是 `getLastError` 的清理点收窄到"设备指针 + IPC 导出失败"这一种情况**(`p2p_pointer.cc:83-98`):
```c
const flagcxResult_t getRes = deviceAdaptor->ipcMemHandleGet(handle, ptr);   // :83
if (getRes == flagcxSuccess) {
  ... 导出 handle 作为共享元数据 ...                                          // :84-92
} else {
  if (deviceAdaptor->getLastError)
    deviceAdaptor->getLastError();     // :94-95  ← 天数在这里是 NULL,整个清理被跳过
  if (!typeKnown) *ptrType = FLAGCX_PTR_HOST;
}
```
**注意天数判定为 CUDA 的指针仍会走到 `:83`** —— 因为该函数的注释(`:26-31`)明确说明:"分类"与"IPC 导出"是两个不同问题,**IPC 只是 GPU 结果之上的可选共享元数据**。所以即使类型已知,也要尝试导出 handle;而这一步失败时的粘性错误清理,**正是 NULL 槽位会漏掉的**。

**因此 S2 仍然要做,但理由变成两条(比原来弱,但依然成立)**:
1. 【文档 §4.1 P0-Core】明文要求「错误处理:`getLastError` 或厂商等价机制 / **能够在 kernel launch 和 runtime 调用失败时返回有效错误**」。
2. **真实漏洞**:设备指针的 IPC 导出失败时,`p2p_pointer.cc:94-95` 的粘性错误清理被 `if (deviceAdaptor->getLastError)` 整体跳过 → **粘性错误残留,污染后续 runtime 调用**。这正是【文档 §4.2】"IPC 打开失败时,FlagCX 可以将 data/signal 操作回退到 Net"那个回退场景。

> 🔎 **复核补充(优先级判断依据)**:新的单测夹具 `test/p2p/test_p2p_pointer.cpp:52/64` 虽然设置了 `getLastError` 的 mock,但**没有断言调用次数** → **这个行为目前完全没有测试保护**。同时它仍是【文档 §4.1 P0-Core】的明文要求。⇒ **S2 优先级 = 高**(修复成本仅 4 行,却补齐一个未受保护的错误清理路径)。

**不再成立的说法(已删除)**:~~"S2 是 S6 指针类型判别的前置条件"~~ —— 现在 `getPointerType` 是权威来源,不依赖 IPC 探测,也不依赖 `getLastError`。
> 📌 但仍有一条**新的关联**:`ixcudaAdaptorGetPointerType` 自己会调 `cudaGetLastError()` 清探测错误(`:388`、`:393`)—— 这是 NVIDIA 同款做法【`cuda_adaptor.cc:1118-1120` 注释:"Clear the probe error so it cannot affect a later runtime call."】。也就是说**类型探测这条路的清场由新函数自理**,与 S2 的槽位无关。

#### 要做什么

```c
// ixcuda_adaptor.cc —— 新增函数(参考 cuda_adaptor.cc:1106-1109)
flagcxResult_t ixcudaAdaptorGetLastError() {
  cudaError_t err = cudaGetLastError();
  return err == cudaSuccess ? flagcxSuccess : flagcxSystemError;
}
```
然后把 `:474`(旧 :446)的 `NULL` 换成 `ixcudaAdaptorGetLastError`。

**注意两个设计细节**(照抄 NVIDIA 即可):
- NVIDIA 的实现**故意不用 `DEVCHECK`**。因为 `DEVCHECK` 会 `return flagcxUnhandledDeviceError`,而这里需要区分"没错误(`flagcxSuccess`)"和"有系统错误(`flagcxSystemError`)"。
- 不要试图返回具体错误码:天数的 `DEVCHECK` 已经把 `cudaError_t` 压成了 `flagcxUnhandledDeviceError`,没有更细的映射。

#### 验收方法
```bash
# 1. 编译期:确认符号进得去
nm -C build/obj/flagcx/adaptor/device/ixcuda_adaptor.o | grep -i getlasterror
# 2. 运行期:跑 T1,确认没有因为粘性错误导致的后续失败
# 3. 若要精准验证"粘性错误被清掉",可临时在 p2p_pointer.cc:95 附近加日志
#    仅用于本地诊断,验证完请还原 —— 按 D2 不把上游改动纳入提交
```

#### 风险
- ⚠️ **不建议改 `flagcx_p2p.cc` 去检查返回值**。那里的丢弃是正确设计,改了反而破坏语义。

---

### 5.3 S3 —— PlatformTraits:64 宽 wave、内存序、部分 mask

> ✅ **上游变更复核结论:S3 完全不受影响。** `ff5f80c` 的 5 个 commit **没有改动** `flagcx/adaptor/include/device_api/iluvatar_platform_traits.h`、`flagcx/adaptor/include/device_utils.h`、`bindings/ir/iluvatar/Makefile`(已用 `git diff --name-only 250ee84..HEAD` 逐一验证为空)。**因此本节的全部结论与全部 `file:line` 行号依然有效,无需重映射。**

#### Jira 原文
> [P0] 检查天数 PlatformTraits 的 64 宽 wave、内存序和部分 mask 是否会让验收 kernel 中断,实现在 `.../iluvatar_platform_traits.h`,simtWidth 取自 `.../device_utils.h` 里天数分支的 `FLAGCX_SIMT_WIDTH 64`。部分 lane mask 和 named barrier 现在会 `__builtin_trap`。用 `test/kernel/iluvatar` 的 kernel 确认 atomic 内存序和 coop 的 sync 与芯片一致;验收路径若会走到 trap,就改 `iluvatar_platform_traits.h`。

#### 【文档】依据(这一节是全文最关键的依据)

- **§5.1 P0-Core Intrin** 至少覆盖:`simtWidth`、lane id 与 lane mask、active mask、warp/subgroup sync、popcount、block 或 named barrier、spin backoff、**system-scope fence**。
  > `simtWidth` 不能默认假设为 32。**厂商必须根据真实执行模型实现 mask、tile 和 cooperative group 语义。**
- **§5.2 P0-Core Atomic** 至少提供 `load`/`store`/`fetchAdd`/`fetchSub`/`fetchOr`/`fetchAnd`/`exchange`/`compareExchange`;
  > **必须正确映射**:Relaxed、Acquire、Release、AcqRel、SeqCst;**system/device/block/thread scope**
  > Signal、counter、FIFO 和 barrier 都依赖这些内存序。**仅保证原子性但忽略 acquire/release 可见性,会产生难以复现的跨 rank hang。**
- **§5.3 P0-Core Cooperative 类型**:每种类型至少提供 `int threadRank() const; int size() const; void sync();`
- **§5.4 参考实现**:NVIDIA `nvidia_platform_traits.h`、DU `du_platform_traits.h`。

#### 结论 A:6 个 trap 当前**一个都碰不到**(已逐点追踪为 FACT)

| trap | 位置 | 拒绝做什么 | 验收路径可达? |
|---|---|---|---|
| 69 | `syncwarp()` `:59-70` | 同步真正的部分 lane mask(非全波前、>1 lane) | **否** |
| 79 | `namedBarrierSync()` `:72-80` | 参与者数既不是 1 也不是整块的 named barrier | **否**(且**零调用者**) |
| 287 | `CoopTileSpan` 构造 `:284-288` | `first<0` 或 `count<=0` | **否** |
| 300 | `CoopTileSpan::sync()` `:295-301` | 非"从 tile 0 起的整块"span | **否** |
| 312 | `CoopLanes` 构造 `:307-313` | `mask == 0` | **否** |
| 387 | `CoopAny::sync()` `:375-389` | 分类为 `SyncUnsupported` 的组 | **否** |

**为什么碰不到**:
- `CoopTile<8>` 那段**已经被编译排除**:【代码】`test/kernel/nvidia/device_api.cu:1340` 有 `#if !defined(USE_ILUVATAR_ADAPTOR)`,注释写着"CoreX has no verified partial-wave barrier. Its production implementation traps instead of widening Tile<8> to a full-wave sync."→ **这个坑之前就被发现并用编译宏绕开了。**
- 验收路径上所有 mask 来源都是 `fullMask()`(`device_api.cu:1358`、`device_ir.cu:109`)
- 唯一用到 `TileSpan` 的地方是 `device_ir.cu:76-105`,它按天数分支设 `t0=0, nTiles = blockDim/64 = 2`,于是 `size = 128 == blockDim.x` → 走 `SyncBlock`,合法
- `namedBarrierSync` 在 Device API 里**零调用者**:天数自己的 `CoopTileSpan::sync()`(`:295-301`)根本不用它,而是直接用 `__syncthreads` 或 trap

**所以:Jira 说的"验收路径若会走到 trap,就改 iluvatar_platform_traits.h"这个条件动作,当前不成立。**

> ⚠️ **强烈建议不要为了"消除 trap"而把部分掩码扩宽成全波前。** `iluvatar_platform_traits.h:67-68` 的注释明确警告:"Expanding a partial mask to the whole wave can deadlock."(扩宽会导致死锁)。这正是下面结论 B 说的那个挂死。

#### 结论 B:真正会炸的是**静默扩宽 → 挂死**(【推断】,需真机确认)

【代码】`test/kernel/nvidia/device_ir.cu:1495-1502`:
```c
flagcxUnifiedIrTestCoopActive(flagcxDevCoopKind_t coopKind) {
  if (coopKind == FLAGCX_COOP_THREAD) return FLAGCX_THREAD_IDX_X == 0;
  if (coopKind == FLAGCX_COOP_WARP)   return FLAGCX_THREAD_IDX_X < 32;   // ← 假设 32 lane
  return coopKind == FLAGCX_COOP_BLOCK;
}
```
它同时守卫 signal/put 和 wait 两侧(如 `:1558`、`:1568`)。

而天数:
- `FLAGCX_COOP_WARP = CoopTile<FLAGCX_SIMT_WIDTH> = CoopTile<64>`【`:277`】
- `CoopAny` 把它分类为 **`SyncWarp`**【`:356`,依据是 `N == Intrin::simtWidth`】
- `CoopAny::sync()` 的 `SyncWarp` 分支**硬编码** `syncwarp(fullMask())` → `__syncwarp()`【`:380`】**覆盖全部 64 lane**

**失效链条**:测试只让 lane 0-31 参与 → 但 barrier 是 64 lane 级的 → **lane 32-63 永远到不了 → 挂死**。

**这就是"trap 设计"本要防住的失效模式,却漏了 —— 因为分类依据是 `N == simtWidth`,而不是"实际参与线程数"。** `SyncUnsupported` 只在 `1 < N < 64` 时产生,而 `N == 64` 被无条件当成"完整波前"。

⚠️ **两个必须诚实标注的不确定点:**
- "lane 32-63 不调用 sync()" 是**推断**,需读 kernel 完整体确认。
- **CoreX 的 `__syncwarp()` 到底是全波前 barrier 还是掩码 barrier,本仓库无法确定**(调查标为 UNDETERMINED;`iluvatar_platform_traits.h:67-68` 的注释**自己断言**是"full-wave barrier",但这只是注释)。

→ **所以 P4 阶段第一件事必须是一个 device 微基准**,直接回答这两个问题。

#### 结论 C:两条与 §5.2 **直接冲突**的实现缺陷

**① `scope` 参数被丢弃** —— 【文档 §5.2】要求"必须正确映射 system/device/block/thread scope"。
【代码】
```c
// iluvatar_platform_traits.h:157-160
template <typename T, flagcxDeviceScope_t Scope = flagcxDeviceScopeSystem>
static T load(T *ptr, flagcxDeviceMemoryOrder_t order) {
  (void)Scope;                    // ← scope 被直接丢弃!
// :173-176
static void store(T *ptr, const T &value, flagcxDeviceMemoryOrder_t order) {
  (void)Scope;                    // ← 同样丢弃
```
**所有 scope 都按同一个语义处理。** 这是 §5.2 的明文违规。

**② acquire 没有 fence,而 release 有 —— 不对称** —— 正是 §5.2 警告的"难以复现的跨 rank hang"。
```c
// :161-170  load
case Relaxed:
case Release:   return __atomic_load_n(ptr, __ATOMIC_RELAXED);
case Acquire:
case AcqRel:
case SeqCst:
default:        return *const_cast<volatile T *>(ptr);   // ← 裸 volatile 读,无 fence
// :176-183  store
if (order == Relaxed) { __atomic_store_n(ptr, value, __ATOMIC_RELAXED); return; }
FLAGCX_DEVICE_THREAD_FENCE();                            // ← release 侧有 fence
*const_cast<volatile T *>(ptr) = value;
```
文件注释 `:152-156` 声称这是有意设计("Acquire polling therefore uses volatile loads"),但【文档 §5.2】不承认这种简化。**所有 barrier 自旋循环都吃这条路径**(`default_comm_traits.h:1124-1128`、`:1236-1248`)。

**③ 另外三个已确认的薄弱点**

| 项 | 位置 | 问题 |
|---|---|---|
| `spinBackoff()` | `:82-84` | 函数体是 `asm volatile("")`,**空操作**。【文档 §5.1】把它列为 P0-Core Intrin |
| `activemask()` | `:45-53` | 有 `static_assert(sizeof(__activemask()) == sizeof(flagcxLaneMask_t))` 断言 64 位,但**从未被执行过**(唯一消费者 `flagcxCoopCoalesced()` 无调用者) |
| 64 位 RMW | `:185-239` | 六个 RMW 都有 `static_assert(sizeof(T)==sizeof(uint32_t))` → **64 位是编译错误**。当前无实例化所以能编过;`bindings/ir/iluvatar/Makefile:125-127` 还有额外构建守卫 |

**④ `threadfenceSystem()` 用 device scope 原语实现**
```c
// :86-92
static void threadfenceSystem() { FLAGCX_DEVICE_THREAD_FENCE(); }   // = __threadfence
static void threadfenceDevice() { FLAGCX_DEVICE_THREAD_FENCE(); }   // = 同一个
```
【代码】`device_utils.h:183` 天数把 `FLAGCX_DEVICE_THREAD_FENCE` 定义为 `__threadfence`;而 NVIDIA 用的是 `__threadfence_system`(`device_utils.h:230`)。`device_utils.h:180-182` 的注释解释:"ivcore11 选不了 `llvm.nvvm.membar.sys`,所以降级成 CoreX 的 cache 回写/失效操作"。

**这个降级假设成立与否,是 P2P 可见性的正确性基础,必须实测。** 失败表现是**偶发数据错**(不是报错),最难查。而【代码】`bindings/ir/flagcx_device_unified_ir_impl.h:174-187` 的 `flagcxDevFlush` 每次都无条件走它。

#### 结论 D:trap 设计与【文档 §5.1/§5.3】存在**合规张力**(需你判断)

【文档 §5.1】:"厂商**必须**根据真实执行模型实现 mask、tile 和 cooperative group 语义。"
【文档 §5.3】:每种 cooperative 类型至少要提供可用的 `threadRank/size/sync`。

而天数对 `1 < N < 64` 的 tile **一律 trap**,等于"tile 语义只实现了两个特例(1 和 64)"。这有两种解读:

- **解读一(严格)**:这是 P0-Core 未完成 —— 因为 §5.1 用了"必须",tile/mask 语义是 P0-Core 硬要求。→ 应在交付表填"**部分支持**",备注列出"`1<N<64` 的子波前 tile 与部分掩码 lanes 不支持,返回 trap;回退路径:使用 `CoopBlock` 或 `CoopWarp`"。
- **解读二(务实)**:当前验收 kernel 集不请求子波前 group,所以**不阻塞验收**。trap 是"如实声明不支持"的一种硬形式(比静默 no-op 好),符合 §2"不能把这种实现标记为功能已支持"的精神。

【建议】**采用解读一 + 解读二的组合**:不改 trap(改了会引入挂死),但在交付报告里**显式登记为"部分支持"并列出限制与回退**,同时把"是否要实现真正的 CoreX 部分波前 barrier"作为**待硬件确认后的后续项**。

#### 要做什么(P4 阶段)

**第 1 步(必做,先做):写 device 微基准,回答两个硬件事实。**
```
问题 1:CoreX 的 __syncwarp() 无参形式,是全波前(64 lane)barrier 还是掩码 barrier?
问题 2:CoreX 的 __activemask() 返回的是 64 位有效掩码吗?
```
建议做法:写一个独立的小 kernel(可放在 `test/kernel/iluvatar/` 下新增文件,或临时在同一目录),让 64 个线程进入,只有 lane<32 调用 `__syncwarp()`,另 32 个 lane 记录"我是否被卡住",加超时/看门狗。**这是唯一能给出确定答案的方法。**

**第 2 步(依据第 1 步结论二选一)** —— 按 **D4** 执行:
- **若 `__syncwarp()` 是全波前**:挂死风险成立。**首选当天数侧改法(不碰公共源)**:让 `CoopTile<N>`(`N != simtWidth && N != 1`)分类为 `SyncUnsupported`,从而**大声 trap 而不是静默挂死**。
  ⚠️ 这会**扩大** trap 触发面,必须同时确认没有别的路径会请求子波前 group(否则会把"挂死"换成"trap 失败",问题只是从难查变好查,并未消除)。
  ⚠️ 另一个选项是修【代码】`test/kernel/nvidia/device_ir.cu:1497-1501`、`:1558`、`:1568`(把 `< 32` 改成 `< FLAGCX_SIMT_WIDTH`)。**但这是公共测试源,按【文档 §13.2】属 FlagCX 项目职责,我方不擅自改** —— 如确需,应作为 upstream issue/patch 提出,**不混在厂商适配提交里**。
- **若 `__syncwarp()` 是掩码 barrier**:则天数**可以支持部分掩码** → 把 `:59-70` 的 trap 改成正真的 `__syncwarp((uint32_t)mask)`,并复核 `:65` 的 `popc==1` 捷径。**这是最理想的结果**,能同时解决 trap 69/387 和挂死,且 PlatformTraits 有机会从"部分支持"升级为"基础通过"。

**第 3 步:修 §5.2 的两条明文违规。**
- `scope`:至少对 `Acquire`/`Release` 补上 fence,并让 `flagcxDeviceScopeSystem` 走 system 级 fence(若硬件不支持则注释说明并登记为"部分支持")。
- acquire 补 fence:`:165-170` 的 acquire 分支加 `FLAGCX_DEVICE_THREAD_FENCE()`。
  ⚠️ **性能影响需评估** —— 自旋循环每条都加 fence 会变慢。可考虑"自旋时不 fence、成功后 fence"的折中,但需与文档要求的语义对齐。
- `spinBackoff` 至少加个空转循环(`nanosleep` 类),不要纯空操作。

**第 4 步:改 `test/kernel/iluvatar/device_api.cu` 的两个死 kernel(依赖 S1)。**
给 `flagcxIluvatarAtomicContractKernel` 和 `flagcxIluvatarUnsupportedCoopKernel` 补 launcher + 头文件声明,让 trap 行为与 32 位 RMW 契约变成**有测试支撑的结论**。

#### ⚠️ 改这个文件的硬边界(否则编译直接失败)

【代码】`bindings/ir/iluvatar/Makefile` 里有 6 道构建守卫,改 `iluvatar_platform_traits.h` **很容易踩到**:

| 行 | 守卫 |
|---|---|
| `:113-115` | IR 里出现 `PlatformCoop`/`VTable` → 构建失败 |
| `:116-118` | 出现间接设备调用 → 失败 |
| `:119-121` | lane mask ABI 必须是 `i64` |
| `:122-124` | comm 指针 ABI 必须是 `addrspace(1)` |
| `:125-127` | IR 里出现 **64 位 RMW**(`atomicrmw\|cmpxchg ... i64`)→ 失败 |
| `:128-131` | 不允许未解析外部符号 |

还有一条 ABI 断言不能破:
```c
// iluvatar_platform_traits.h:393-396
static_assert(sizeof(PlatformTraits<IluvatarPlatform>::CoopAny) == 24, ...);
static_assert(alignof(PlatformTraits<IluvatarPlatform>::CoopAny) == 8, ...);
```
→ **不能随便往 `CoopAny` 里加成员变量。** 这些守卫也解释了为什么天数**没有**按【文档 §5.1 模板】写 `using CoopAny = PlatformCoop;`,而是自己写了一套 24 字节 + `SyncKind` 的实现 —— **不是偷懒,是 CoreX bitcode 的限制**。这个偏差**必须在交付备注里登记**。

#### 验收方法
```bash
# 1. 微基准(自行编写):直接回答 __syncwarp() / __activemask() 语义
# 2. 编译期:确认没踩到 6 道守卫
make -C bindings/ir/iluvatar -j8 ...
# 3. 运行期:重点盯 T3/T4 是否超时(挂死的表现)
timeout --signal=TERM --kill-after=60s 30m \
  mpirun --allow-run-as-root -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_VMM_ENABLE=0 \
  -x LD_LIBRARY_PATH=$PWD/build/lib:$LD_LIBRARY_PATH \
  -np 8 test/unittest/device_api/build/bin/test_device_ir_unified_intra -b 1K -e 16M -f 2
```

---

### 5.4 S4 —— 用天数配置编出 `libflagcx.so` 和四个 Device API 验收程序(Default)

#### Jira 原文
> [P0] 用天数配置编出 libflagcx.so 和四个 Device API 验收程序,后端参数选 Default。
> 平台文件是 `FlagCX/makefiles/iluvatar.mk`,其中已设置 `FLAGCX_COMM_TRAITS_DEFAULT`,并链接 `flagcx/adaptor/device_api/default_dev_api_backend.cc`。
> DeviceAPI 绑定在 `FlagCX/flagcx/adaptor/include/device_api/iluvatar_comm_traits.h`。kernel 编译入口是 `FlagCX/test/kernel/iluvatar/Makefile`,四个程序的链接规则在 `FlagCX/test/unittest/device_api/Makefile`。`COMPILE_KERNEL=1`,主库和测试使用同一套参数。用 `ldd` 确认测试程序加载的是这次编出的 `build/lib/libflagcx.so`。

#### 【文档】依据
- **§10.3 统一编译流程**:Device API 验收需同时编译三部分 —— ① `libflagcx.so`(含所选 Host `DevApiBackend`);② `test/kernel/<vendor>` 下的设备 kernel;③ Device API operator 测试和 Unified IR 单测程序。**主库必须设 `COMPILE_KERNEL=1`**,且**三者必须使用相同的平台与后端参数**(「不能用 Default backend 编译主库,再用 CCL `CommTraits` 编译测试 kernel」)。
- **§10.4 编译验收标准**:五项(见下)。
- **§13.3.2~13.3.6**:四条标准运行命令。
- 【文档 §10.2】:"一个构建只能选择一个 `devApiBackend` 和一个 `DeviceAPI` alias。**Host backend 与 Device 侧 CommTraits 必须成对选择。**"

#### 【代码】事实:Jira 的三条描述**全部为真** ✅

| Jira 说法 | 核实结果 |
|---|---|
| `iluvatar.mk` 已设 `FLAGCX_COMM_TRAITS_DEFAULT` | ✅ `makefiles/iluvatar.mk:27` `ADAPTOR_FLAG := -DUSE_ILUVATAR_ADAPTOR -DFLAGCX_COMM_TRAITS_DEFAULT` |
| 已链接 `default_dev_api_backend.cc` | ✅ `makefiles/iluvatar.mk:33` `PLATFORM_EXTRA_SRCS := flagcx/adaptor/device_api/default_dev_api_backend.cc` |
| kernel 入口是 `test/kernel/iluvatar/Makefile` | ✅ 用 `$(DEVICE_COMPILER) -x ivcore --cuda-path=$(DEVICE_HOME) --cuda-gpu-arch=$(ILUVATAR_ARCH) -DFLAGCX_ILUVATAR_DEVICE_COMPILE=1 -DCOMPILE_KERNEL`,产出 `device_api.o` / `device_ir.o`;且**不需要 dlink**(`:35-36`,CoreX 把 device image 嵌在每个 object 里) |
| 四个程序链接规则在 `test/unittest/device_api/Makefile` | ✅ `:146-174` 四条链接规则;`:125-129` 会回过来 `$(MAKE) -C $(KERNEL_DIR)` 编 kernel |
| DeviceAPI 绑定在 `iluvatar_comm_traits.h` | ✅ `iluvatar_comm_traits.h:10` → `using DeviceAPI = CommTraits<DefaultBackend<IluvatarPlatform>>;` |

四个验收程序源码**齐全**:
- T1 `test_device_api_intra.cpp`、T2 `test_device_api_inter.cpp`
- T3 `test_device_ir_unified_intra.cpp`、T4 `test_device_ir_unified_inter.cpp`
- (另有 `test_device_api.cpp`、`test_device_ir_intra.cpp`、`test_device_ir_inter.cpp`、`test_dev_comm_cleanup.cpp`)

#### 🔴 坑 1:`ldd` 这条要求为什么重要,以及那个隐藏的未解析符号

【文档 §10.4】要求"测试程序通过 `ldd` 或平台等价工具加载**本次构建**的 `libflagcx.so`,而不是旧安装目录中的版本",并且 §10.4 的交付模板里 `loaded libflagcx.so path` 必须来自**实际测试可执行文件的动态库解析结果**。

**这不是形式主义** —— 天数的 `.mk` 注释里就记录了一个真实陷阱:

```make
# makefiles/iluvatar.mk:31-32
# device_api/ is not globbed by the Makefile and -shared without --no-undefined
# hides that: flagcx_device.cc's devApiBackend would stay unresolved.
```

**翻译**:`device_api/` 目录**不会被自动扫描**;而链接使用 `-shared` 却**没有加 `--no-undefined`**。所以一旦漏掉某个源文件(比如误删 `:33` 那一行),**编译会成功、但 `devApiBackend` 是未解析符号,直到运行时才炸** —— 症状可能是"什么都没发生"或段错误,**极难定位**。

→ 所以除了 `ldd`,还要加一条自检:
```bash
nm -D --undefined-only build/lib/libflagcx.so | grep -i devapibackend
# 期望:无输出
```

#### 🔴 坑 2:验收命令**必须手工敲**,不能用 `make run`

| `make` 目标 | Makefile 实际传参 | 【文档 §13.3】要求 | 一致? |
|---|---|---|---|
| `run-mpi-intra`(`:212`) | `-n 8 test_device_api_intra` —— **无任何尺寸参数** | `-b 1K -e 16M -f 2 -R 2` | ❌ |
| `run-mpi-inter`(`:232`) | `-b 1M -e 4M -f 2 -R 2` | `-b 1K -e 16M -f 2 -R 2` | ❌ 尺寸范围错 |
| `run-mpi-inter`(`:186`) | 额外设 `FLAGCX_P2P_DISABLE=1` | 【§13.3.2】基线只有 3 个变量,**不含此项** | ❌ 把 P2P 关了,而 T2 要测 one-sided Put/Get |
| `run-mpi-unified-intra`(`:243-244`) | `-n 8 ... -b 1K -e 16M -f 2` | 同 | ✅ |
| `run-mpi-unified-inter`(`:253-257`) | 4+4 两逻辑节点 | 同 | ✅(但缺 rank wrapper,见下) |

**结论:T1/T2 必须手工敲命令;T3/T4 可以用 `make` 目标,但仍建议手工敲以便完整记录命令(§13.3.9 要求提交完整运行命令)。**

#### 🔴 坑 3:T2/T4 需要一个 **rank wrapper**,而它现在不存在

【文档 §13.3.1】规定 T2/T4 的拓扑:
- 8 个 MPI ranks,**两个逻辑节点,每节点 4 ranks / 4 设备**
- 逻辑节点 0:ranks 0–3;逻辑节点 1:ranks 4–7
- **两组 rank 必须具有不同的 `FLAGCX_HOSTID`,并绑定各自可用的设备和 NIC**

【文档 §13.3.1】还给了 wrapper 的伪代码,关键要求:
```bash
if 0 <= rank <= 3; then
  export <VENDOR_VISIBLE_DEVICES>=0,1,2,3
  export FLAGCX_HOSTID=node0
  export FLAGCX_IB_HCA=<node0-nics>
elif 4 <= rank <= 7; then
  export <VENDOR_VISIBLE_DEVICES>=4,5,6,7
  export FLAGCX_HOSTID=node1
  export FLAGCX_IB_HCA=<node1-nics>
else
  echo "unexpected MPI rank: ${rank}" >&2; exit 1
fi
exec "$@"
```
⚠️ 【文档】特别提醒:"上述条件表示**闭区间** ranks 0–3 和 ranks 4–7,**不是只选择 rank 0、3、4、7**";GNU Bash 要用 `[[ $rank -ge 0 && $rank -le 3 ]]`。另外"如果厂商通信库也使用 Host ID、visible devices 或 NIC 变量,wrapper 中应同步设置厂商 CCL 对应变量"。

【代码】`test/unittest/device_api/Makefile:196-199` 只设了 `FLAGCX_HOSTID`/`NCCL_HOSTID`,**没有设置 per-rank 的 visible devices / HCA**。→ **这是一个缺失的交付物**,需要你写一个天数版 wrapper(设备可见性变量名可能是 `CUDA_VISIBLE_DEVICES` 或天数自己的变量,需确认)。

#### 要做什么(P1 阶段)

```bash
# ---- 前置:确认工具链 ----
ls /usr/local/corex/bin/clang          # iluvatar.mk:14 COREX_CLANG
ls /usr/local/corex/include/nccl.h     # CCL_INCLUDE
# ILUVATAR_ARCH 默认 ivcore11(iluvatar.mk:13)

# ---- 1. 编译主库(【文档 §10.3】第 1 步)----
make clean                              # 【§10.3】切换配置必须 clean rebuild
make -j8 COMPILE_KERNEL=1 USE_ILUVATAR=1 \
     DEVICE_HOME=/usr/local/corex CCL_HOME=/usr/local/corex \
     MPI_HOME=<mpi-prefix>

# ---- 2. 编译四个验收程序(【§10.3】第 2 步)----
make -C test/unittest/device_api -j8 \
     COMPILE_KERNEL=1 USE_ILUVATAR=1 \
     DEVICE_HOME=/usr/local/corex CCL_HOME=/usr/local/corex \
     MPI_HOME=<mpi-prefix>
# 注意:同样要带 CUDA_VISIBLE_DEVICES 自己换

# ---- 3. 【§10.4】编译验收标准自检 ----
ls -l build/lib/libflagcx.so
ldd test/unittest/device_api/build/bin/test_device_api_intra | grep flagcx
#   ↑ 期望指向本仓库的 build/lib/libflagcx.so,不是 /usr/local/lib 里的旧版本
nm -D --undefined-only build/lib/libflagcx.so | grep -i devapibackend
#   ↑ 期望无输出
```

**若主库/device object/测试程序来自不同配置,必须 clean rebuild**(【文档 §10.3】明确要求四步 clean:主库、`test/kernel`、`test/unittest/device_api`、`test/perf/device_api`)。

#### 验收方法(【文档 §10.4】五项)**全部达成**
1. ☐ 主库、Device API kernel 和四个标准正确性测试程序全部编译成功
2. ☐ 编译过程中没有 **unresolved device symbol**、**重复 `DeviceAPI` alias** 或 **backend vtable 冲突**
3. ☐ 测试程序通过 `ldd` 加载**本次构建**的 `libflagcx.so`
4. ☐ Device compiler、Host compiler、CCL/SHMEM headers 和运行时库**版本一致**
5. ☐ 记录完整编译命令、SDK/CCL/SHMEM/MPI 版本和 **commit ID**

---

### 5.5 S5 —— 确定 signal/barrier 用 IPC 还是 host-mapped,并让接口与配置一致

#### Jira 原文
> [P0][default] 确定天数 signal/barrier 用 IPC 还是 host-mapped,并让对应接口与配置一致,Default 路径要做。
> `ixcuda_adaptor.cc` 里 `hostGetDevicePointer` 是 NULL,`hostRegister`/`hostUnregister` 直接返回 `flagcxNotSupported`。验收走 IPC 时保持这个返回值,并在平台默认配置里关闭 host-mapped。要启用 host-mapped,就在这个文件里实现这三个接口。

#### 【文档】依据
- **§4.2 P0-Default** 表格里列了三组能力:`gdrMemAlloc/Free`、五个 `ipcMemHandle*`、**`hostGetDevicePointer`**("启用 host-mapped signal/barrier 路径时将 Host VA 转换为 Device VA")。
- **§4.3 条件 P0:Host signal/barrier 路径**:
  > 启用 host-mapped signal/barrier 时需要:`hostRegister`、`hostUnregister`、`hostGetDevicePointer`。
  > **如果厂商不支持该路径,应明确返回 `flagcxNotSupported`,并在平台默认配置中关闭对应模式。**
- **§4.5 验收要点**:
  > - 所有 P0 接口返回值与实际执行结果一致,**不能 silent no-op**
  > - **Host 与 Device 指针类型不能混用**
- 【文档 §9】Default 后端数据路径:
  > 同节点:data/signal/barrier → **Device IPC 或 host-mapped memory**
  > 跨节点或 IPC 不可用:Put/Get/Signal → Device FIFO → Host proxy → FlagCX Net

#### 决策答案:**走 IPC** ✅(三条证据)

**证据 1** —— 【代码】`flagcx/service/utils.cc:119`:
```c
FLAGCX_PARAM(SignalHostEnable, "SIGNAL_HOST_ENABLE", 0);   // 默认值 0
```

**证据 2** —— 【代码】`default_dev_api_backend.cc:179` 的分支:
```c
if (flagcxParamSignalHostEnable() == 0) {
  // ── IPC device memory path (default) ──
  deviceMalloc(...);                                  // 分配设备内存
  buildIpcPeerPointers(comm, ..., barrierSize);        // 建 IPC peer 指针
  devComm->barrierPeers = ...; devComm->nBarriers = FLAGCX_DEVICE_CTA_COUNT;
} else {
  // ── flagcxShm + hipHostRegister path (FLAGCX_SIGNAL_HOST_ENABLE=1) ──
  flagcxShmOpen(...); shmHostRegister(...);
  deviceAdaptor->hostGetDevicePointer(...);            // :276
}
```
**IPC 路径完全不碰 `hostRegister` / `hostGetDevicePointer`。**

**证据 3** —— 【代码】验收环境(`test/unittest/device_api/Makefile:183-199`)**没有设置** `FLAGCX_SIGNAL_HOST_ENABLE`;【文档 §13.3.2】的基线环境也只有 3 个变量,同样不含它。→ **验收走 IPC 分支。**

#### ✅ `hostRegister`/`hostUnregister` 的桩是**正确的**,保持原样

```c
// ixcuda_adaptor.cc:341-346
flagcxResult_t ixcudaAdaptorHostRegister(void *, size_t) { return flagcxNotSupported; }
flagcxResult_t ixcudaAdaptorHostUnregister(void *)        { return flagcxNotSupported; }
```
这**正好符合**【文档 §4.3】"应明确返回 `flagcxNotSupported`",也符合 §2"可以提供返回 `flagcxNotSupported` 的实现"。**Jira 说"保持这个返回值"是对的。**

#### 🔴 但 `hostGetDevicePointer` 的 `NULL` **不能保持** —— 3 处无条件裸调用

| 调用点 | NULL 检查 | 返回值检查 | 天数上后果 |
|---|---|---|---|
| `flagcx/core/launch_kernel.cc:48` | ❌ | ❌ **连返回值都丢弃** | **SIGSEGV** |
| `flagcx/runner/uni_runner_impl.cc:1903` | ❌ | ✅(`:1905` `if (result != flagcxSuccess) goto fail;`) | **SIGSEGV**(检查前就崩) |
| `flagcx/core/proxy.cc:2094` | ❌ | ✅(`FLAGCXCHECKGOTO`) | **SIGSEGV** |

**三处都不在 `FLAGCX_SIGNAL_HOST_ENABLE` 分支里,是常规路径。**

**而且这不是假设,是已经发生过的崩溃。** 你仓库里 `dsh-fixes/README.md` 的既有记录:

> **`perf_allreduce` + `FLAGCX_USE_HETERO_COMM=1` 崩溃(exit=139)**:已定位为 **ixcuda 适配器 `hostGetDevicePointer` 为 NULL 却被无条件调用**(`ixcuda_adaptor.cc` 该槽位显式 `NULL` —— 新 HEAD 位于 `:415`,旧 HEAD 为 `:387`;调用点 `uni_runner_impl.cc` 无检查);`hip` 适配器同样缺失。

**exit=139 = SIGSEGV,与代码分析完全吻合 —— 两个独立来源互相印证。**

注意:`hip_adaptor.cc:375` 也是 `NULL`,说明这个坑**不只天数一家**。但【文档 §4.5】"不能 silent no-op"和"指针类型不能混用"两条,都把天数的 `NULL` 判为**不合规**。

#### 要做什么 —— 已拍定为 **D3:真实现 + 成套修改**(见 §8 D3)

> ⚠️ **我先前建议的"桩化"方案已被推翻。** 原文此处推荐把 `NULL` 换成返回 `flagcxNotSupported` 的桩 —— 核对 8 家厂商的参照实现后确认**那是错的**,因为它会让三处常规路径**静默拿到无效指针**(比崩溃更隐蔽),而且漏掉了根因(缺 `Mapped` 标志)。以下是修正后的方案。

**改动 1:`hostGetDevicePointer` 真实现**(替换 `:415`(旧 :387)的 `NULL`)
```c
// ixcuda_adaptor.cc —— 新增(参照 cuda_adaptor.cc:126-132)
flagcxResult_t ixcudaAdaptorHostGetDevicePointer(void **pDevice, void *pHost) {
  if (pDevice == NULL || pHost == NULL) {
    return flagcxInvalidArgument;
  }
  DEVCHECK(cudaHostGetDevicePointer(pDevice, pHost, 0));
  return flagcxSuccess;
}
```

**改动 2(根因,必须一起做):`flagcxMemHost` 分配加 `Mapped` 标志**
```c
// ixcuda_adaptor.cc:49-50 —— 现在
if (type == flagcxMemHost) {
  DEVCHECK(cudaMallocHost(ptr, size));            // ❌ 不带 Mapped,拿不到 device 别名
}
// 应改为(参照 cuda_adaptor.cc:76-77)
if (type == flagcxMemHost) {
  DEVCHECK(cudaHostAlloc(ptr, size, cudaHostAllocMapped));   // ✅ 映射进设备地址空间
}
```
**为什么这条不能省**:`cudaMallocHost` 只做 page-locked,**不映射**;`cudaHostGetDevicePointer` 对未映射的内存拿不到有效别名。所以**只补改动 1 是无效的**,两者必须成套。这也正好解释【文档 §4.3】为什么把三个接口列成一组。

**改动 3:`hostRegister` / `hostUnregister` 保持 `flagcxNotSupported` 不变**(`:341-346`)
理由见 §8 D3 的"理由 4":它们的输入是 `/dev/shm` mmap 内存(只服务 `SIGNAL_HOST_ENABLE=1` 的 shm barrier 路径,默认关闭),而 `hostGetDevicePointer` 服务的是**无条件调用**的常规路径。**这个不对称是本质的**,也正是 Jira 那句"验收走 IPC 时保持这个返回值"的适用对象。

**改动 4:给 `test/kernel/iluvatar/device_api.cu` 的两个死 kernel 补 launcher**(依赖 D1)。

**✅ 单测不用改。**
【代码】`test/unittest/adaptor/test_device_adaptor.cpp:760-779` 期望 `flagcxSuccess`(`:770`)—— **真实现后该测试原样通过**。我先前担心的"桩化会让它失败"的问题随 D3 消失。这也是"真实现才是设计意图"的有力旁证:它是**通用**测试,期望的是**成功**。

**⚠️ 唯一前提与回退**:天数 CoreX runtime 必须支持 `cudaHostAlloc(Mapped)` + `cudaHostGetDevicePointer`。→ **P2 第一步先跑探针**(见 §8 D3,20 行程序,10 分钟定论)。若探针失败,按 §8 D3 的四步回退执行(含**向上游提 patch**)。

#### 关于"在平台默认配置里关闭 host-mapped"

**实质已满足**:全局默认就是 0,【代码】`makefiles/iluvatar.mk`(33 行)**没有任何** `SIGNAL_HOST_ENABLE` 覆盖。

**这里要精确区分"关闭什么"**(D3 的核心结论):
- ✅ **关闭的是** `/dev/shm` + `hostRegister` 那条 **shm barrier 路径**(`default_dev_api_backend.cc:211` 的 else 分支)—— 它由 `SIGNAL_HOST_ENABLE=1` 控制,默认关闭。
- ❌ **不能关闭** `hostGetDevicePointer` 本身 —— 三处常规路径**与 `SIGNAL_HOST_ENABLE` 无关**,无条件调用它。

**【建议】在验收命令里显式写 `FLAGCX_SIGNAL_HOST_ENABLE=0`** 以保证确定性(避免有人 shell 里残留该变量导致走另一条路),并把"shm host-mapped barrier 路径未实现、已在默认配置关闭;回退路径 = IPC"写进交付说明的"运行限制"一节【§15 要求部分支持必须列出回退路径】。

#### 验收方法
```bash
# 1. 编译期
nm -C build/obj/flagcx/adaptor/device/ixcuda_adaptor.o | grep -i hostgetdevicepointer
# 2. 单元测试(真实现后应原样通过,无需改动)
cd test/unittest/adaptor && make && ./build/bin/adaptor_unit_tests \
    --gtest_filter='DeviceAdaptorTest.HostGetDevicePointer'
#    期望 PASS:该测试期望 flagcxSuccess(见 test_device_adaptor.cpp:770)
# 3. 回归验证"不再段错误":跑原崩溃场景
mpirun --allow-run-as-root -x FLAGCX_USE_HETERO_COMM=1 -x LD_LIBRARY_PATH=$PWD/build/lib:$LD_LIBRARY_PATH \
  -np 8 test/perf/device_api/build/bin/perf_allreduce_intranode
echo "exit=$?"   # 期望:非 139
```

---

### 5.6 S6 —— 单机验证天数 IPC 与 GDR 是否符合 Default 路径的预期

#### Jira 原文
> [P0][default] 单机验证天数 IPC 和 GDR 是否符合 Default 路径的预期,Default 路径要做。`gdrMemAlloc` / `gdrMemFree` 和五个 `ipcMemHandle*` 已在 `ixcuda_adaptor.cc` 实现,文件末尾还有 `FLAGCX_GDR_WRITE_REQUIRES_FLUSH`。在同一台 8 卡机器上用多进程交换、打开、关闭 IPC handle,确认 GDR 内存可注册、指针类型不混用,free、close、handle free 的顺序不交叉。结果不符时改这个文件。

#### 【文档】依据
- **§4.2 P0-Default**:`gdrMemAlloc/gdrMemFree`("分配 Device API signal buffer 以及异构路径的注册内存")、五个 `ipcMemHandle*`("建立单节点 peer data、signal 和 barrier 映射")。
- **§4.5 验收要点**(与 Jira 几乎逐字对应):
  > - IPC handle 可跨**同一节点的进程**交换、打开和关闭
  > - **`deviceFree`、IPC `close` 和 `handle free` 生命周期不交叉**
  > - **Host 与 Device 指针类型不能混用**
- 【文档 §4.2】补充:"IPC 打开失败时,FlagCX 可以将 data/signal 操作回退到 Net,但这不意味着 IPC 接口可以完全缺失。"
- 【文档 §16.5】:"厂商需要确保 DeviceAdaptor **返回真实 IPC 结果**,并保证回退所需 Net 和 proxy 能工作。"

#### 【代码】事实:实现齐全,契约核对通过

| 接口 | 位置 | 关键契约 |
|---|---|---|
| `gdrMemAlloc` | `:99-111` | `cudaMalloc` + `cudaPointerGetAttributes` + `cuPointerSetAttribute(SYNC_MEMOPS, 1)`;`ptr==NULL` → `flagcxInvalidArgument` |
| `gdrMemFree` | `:113-119` | `ptr==NULL` → 直接 `flagcxSuccess`(幂等) |
| `ipcMemHandleCreate` | `:239-246` | `flagcxCalloc` 分配容器,`*size = sizeof(cudaIpcMemHandle_t)` |
| `ipcMemHandleGet` | `:248-255` | 参数校验 + `cudaIpcGetMemHandle(&handle->base, devPtr)` |
| `ipcMemHandleOpen` | `:257-265` | **要求 `*devPtr` 预先为 `NULL`**(`:259`),否则 `flagcxInvalidArgument` |
| `ipcMemHandleClose` | `:267-273` | `NULL` → `flagcxInvalidArgument` |
| `ipcMemHandleFree` | `:275-280` | 纯 `free(handle)`,**容错 `NULL`** |
| GDR flush 策略 | `:480`(旧 :452) | `FLAGCX_GDR_WRITE_REQUIRES_FLUSH`(**只有 WRITE**) |

**顺序契约的真相(初学者容易误解):** `Free` 只释放**字节容器**,`Close` 只解**映射**,两者在实现里**零耦合**。所以"顺序不交叉"**不是代码保证的,完全靠调用方** —— 这正是要用测试压的原因。

#### ✅ 好消息:验证所需的测试**已经存在**,不用新写

【代码】`test/unittest/adaptor/coll_ipc_mem_handle.cpp` 的 `IpcMemHandleMpiTest` 类,**15 个用例**:

| 测试 | 行 | 对应【文档 §4.5】哪条 |
|---|---|---|
| `CrossProcessLifecycle` | 470 | IPC handle 跨进程交换/打开/关闭 |
| `CrossGpuImportedMappingRead` / `Write` | 588 / 592 | 打开后可读/可写 |
| `CrossGpuImportedGdrMappingRead{Sync,Async}` | 596 / 602 | GDR 内存可读 |
| `CrossGpuDeviceToImportedGdrWrite{Sync,Async}` | 608 / 614 | **指针类型不混用**(device ↔ imported GDR) |
| `CrossGpuGdrToImportedDeviceWrite*` | 620 / 626 | 反向混用 |
| `CrossGpuGdrToImportedGdrWrite*` | 632 / 638 | GDR ↔ GDR |
| `CrossGpuReverseGdrToImportedGdrWrite*` | 644 / 650 | 反向 GDR |
| `CrossGpuBidirectionalGdrMappingsWriteAsync` | 656 | 双向 |
| **`CrossGpuLargeGdrSubrangesWriteAsync`** | **660** | **大 GDR 分配的子区间** ← 见下面的隐患 ① |

另外 `test/perf/host_api/test_ipc_sendrecv.cpp:63-171` 是完整的跨进程 IPC 收发例程,它的收尾顺序恰好就是【文档 §4.5】"顺序不交叉"的**范本**:
```c
devHandle->ipcMemHandleClose(peerbuff);      // :169 先关映射
devHandle->ipcMemHandleFree(myIpcHandle);    // :170 再释放本端容器
devHandle->ipcMemHandleFree(peerIpcHandle);  // :171 再释放对端容器
```
⚠️ 但它的构建规则只在 **`test/perf/Makefile.old`** 里(`:88-90`、`:145`)—— **可能已不在活跃构建中,需确认**,不要把它当主验证手段。

#### 🔴 隐患 ①:`getAddressRange` 缺失 → "分配基址探测"被静默关闭

【代码】`ixcuda_adaptor.cc:476`(旧 :448)→ `getAddressRange` = `flagcxDeviceAdaptorGetAddressRangeNotSupported`(定义在 `flagcx_device_adaptor.h:378-385`,是个返回 `flagcxNotSupported` 的**非 NULL 桩**)。

> ✅ **好消息(复核 §0.4 结论 ②)**:上游新增的 `test/unittest/adaptor/test_device_adaptor.cpp:277` `GetAddressRangeForInteriorGdrPointer` 在 `:290-292` 有显式的 `flagcxNotSupported → GTEST_SKIP`,所以**天数的桩会导致 SKIP,不会导致测试失败**。也就是说**上游没有强制我们实现 `getAddressRange`**。
> ⚠️ **但这不消除隐患本身**:下面的"基址探测被静默关闭"是**运行时正确性问题**,与测试是否 SKIP 无关 —— 它由 `CrossGpuLargeGdrSubrangesWriteAsync` 这类真实用例暴露。

顺着 `flagcxGetIpcExportRange`(`flagcx/adaptor/flagcx_device.cc:406-447`)看:
```c
*exportBase = const_cast<void *>(buff);   // :414  先假设"传进来的就是基址"
*allocationSize = size;  *userOffset = 0;
if (deviceAdaptor->getAddressRange == nullptr) return flagcxSuccess;   // :417 不会走(桩非 NULL)
res = deviceAdaptor->getAddressRange(buff, &allocationBase, &queriedSize);
if (res == flagcxNotSupported) return flagcxSuccess;                   // :424-425 ← 天数走这里
```
**后果:天数上"找出 allocation 基址"这个能力被静默关闭,`exportBase` 就等于调用者传进来的指针。**

而 IPC 的硬性要求是"`ipcMemHandleGet` 必须传 **allocation base**,不能传子区间"(否则 handle 指向错误基址)。**这个责任 100% 落在调用方,没有任何机制兜底、也没有任何告警。**

→ **`CrossGpuLargeGdrSubrangesWriteAsync`(`:660`)正是压这个场景的,必须重点看它。** 如果它失败,第一嫌疑就是这里。

#### ✅ 隐患 ②:已由上游解决 —— 天数现在有权威 `getPointerType`

> **原分析此项为"缺失,类型判别无兜底"。上游 `ff5f80c` 已为天数实现(commit `7826ec8 #640`),本项作废。**

【代码】新增 `ixcudaAdaptorGetPointerType`(`ixcuda_adaptor.cc:381-407`),已接线到 `:475`(替换了原来的 `flagcxDeviceAdaptorGetPointerTypeNotSupported`)。实现照抄 NVIDIA:用 `cudaPointerGetAttributes` 分类,两个错误分支内部都调 `cudaGetLastError()` 清探测错误(`:388`、`:393`)。

**这带来的净收益**:【文档 §4.5】"Host 与 Device 指针类型不能混用"这条**现在有了权威判定来源**,不再依赖 IPC 导出成功与否的间接推断(新逻辑 `flagcx/core/p2p_pointer.cc:32-65`,判定 HOST 时 `:64-65` 直接返回)。

**但引入了一项新的验证义务(替代原来的隐患)**:上游同时给该测试加了**内部指针(interior pointer)断言** —— `test/unittest/adaptor/test_device_adaptor.cpp:209` 的 `GetPointerType` 现在会检查 `hostPtr + 64`、`devicePtr + 64`、`managedPtr + 64` 的分类结果,`GetAddressRangeForInteriorGdrPointer:277` 也新增了对 interior GDR 指针的 `getPointerType` 断言(`:300-302`、`:318-322`)。

⚠️ **这是【推断】层的风险**:NVIDIA 依靠 `cudaPointerGetAttributes` 对"分配基址 + 偏移"的行为(host 返回 `cudaErrorInvalidValue` → HOST;device 正常返回 → CUDA)。**CoreX 是否完全一致必须实测**。这也顺带说明为什么天数**需要 `getAddressRange`**(见隐患 ①)—— 因为 interior 指针的"基址 + 偏移"解析在 NVIDIA 侧靠它,虽然天数目前是 `NotSupported` 桩且相关测试会 SKIP(见 §0.4 结论 ②)。

**验收要点(P3 阶段加一条)**:
```bash
cd test/unittest/adaptor && make && ./build/bin/adaptor_unit_tests \
    --gtest_filter='DeviceAdaptorTest.GetPointerType:DeviceAdaptorTest.GetAddressRangeForInteriorGdrPointer'
# GetPointerType 期望 PASS(含 interior pointer);GetAddressRange... 期望 SKIP(天数 getAddressRange 为 NotSupported)
```

#### 🔴 隐患 ③:三个"裸声明",无代码支撑

**① GDR flush 策略是单边的。**
```c
// 天数  ixcuda_adaptor.cc:480(旧 :452)
FLAGCX_GDR_WRITE_REQUIRES_FLUSH,
// NVIDIA cuda_adaptor.cc:1231(DU、maca 同)
FLAGCX_GDR_READ_REQUIRES_FLUSH | FLAGCX_GDR_WRITE_REQUIRES_FLUSH,
```
天数等于声明"**RDMA READ 进这块内存不需要 flush**"。这是一个强策略断言。
【代码】`docs/environment_variables.md:168` 有覆盖开关:
> `FLAGCX_GDR_WRITE_REQUIRES_FLUSH`:`-1` 用平台默认,`0` 禁用该要求,`1` 强制。**禁用平台要求的 flush 是专家级不安全操作**。

**要验证什么**:RDMA READ 到这块内存后,不加 flush 能不能读到正确数据。若不能,就必须加上 `FLAGCX_GDR_READ_REQUIRES_FLUSH`(即改 `:480`)。

> 📌 **上游变更补充(`ff5f80c`)**:这套策略现在由**新子系统**统一裁决 —— `flagcx/core/include/gdr_visibility.h` + `gdr_visibility.cc` + `gdr_visibility_topology.cc`。要点:
> - `flagcxResolveGdrVisibilityPolicy`(`gdr_visibility.h:71-73`)按**每个 device/NIC 连接**求解,`gdrDeviceFamily` 与 `deviceArchitecture` 是输入(`:43-56`)。
> - **fail-closed**:未建模/未知拓扑**保留设备默认值**(`:82-85` 原文 "Unknown or partially modelled topology is fail-closed and retains the device default")→ 天数的 `UNKNOWN` + `NULL` 是**保守安全**的。
> - override 语义(`:66-70`)与 `FLAGCX_GDR_{READ,WRITE}_REQUIRES_FLUSH` 一致:`-1` 自动 / `0` 清除 / `1` 强制。
> - `flagcxGdrVisibilityReason_t`(`:27-38`)里有一条与 S3 相关的设计约束:**"NIC-specific coherence exemptions must not remove that peer-GPU acquire"**(若信号可能由对端 GPU 通过 IPC/D2D 发布,则 NIC 侧的豁免不得移除那个 peer-GPU acquire)—— 这与 S3 的 acquire fence 问题同源,值得一并验证。
>
> **⇒ S6 的验证方式要改**:不再只看 `ixcuda_adaptor.cc:480` 这一个声明,还要确认新 resolver 对 `deviceFamily=UNKNOWN` 的裁决结果符合预期(可用新增的 `test/unittest/core/test_gdr_visibility.cpp` 与 `test/unittest/rma/coll_gdr_visibility.cpp` 作为载体)。

**② `gdrMemAlloc` 没有任何 GPUDirect/RDMA 能力查询。**
```c
// ixcuda_adaptor.cc:99-111 —— 只做了三件事
cudaMalloc(ptr, size);                       // 分配
cudaPointerGetAttributes(&attrs, *ptr);      // 查属性
cuPointerSetAttribute(SYNC_MEMOPS, ...);     // 设同步属性
```
对比 NVIDIA 的 `cuda_adaptor.cc:134-217`:VMM 路径下会 query `CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_WITH_CUDA_VMM_SUPPORTED` 来决定 `gpuDirectRDMACapable`。**天数没有任何等价检查。** → 所以"**GDR 内存可被 NetAdaptor 注册成 MR**"这条**没有任何代码保证**,必须实测。

**③ `hostShareMemAlloc/Free`、`gdrPtrMmap/Munmap`、`memHandleInit/Destroy` 全是 NULL**(`:389-397`)。这些属 P1/P2,不阻塞本次,但**要在交付备注登记**。

#### 要做什么(P3 阶段)

```bash
# ---- 1. 编译 MPI 测试(⚠️ 注意 rank 数!)----
cd test/unittest/adaptor
make MPI_NP=2        # ← 关键!见下面的警告
# 产物:test/unittest/adaptor/build/bin/adaptor_mpi_tests

# ---- 2. 跑 15 个 IPC 用例 ----
mpirun --allow-run-as-root -np 2 \
  -x LD_LIBRARY_PATH=$PWD/../../../build/lib:$LD_LIBRARY_PATH \
  ./build/bin/adaptor_mpi_tests --gtest_filter='IpcMemHandleMpiTest.*'

# ---- 3. 重点单跑那个子区间用例 ----
mpirun --allow-run-as-root -np 2 -x LD_LIBRARY_PATH=... \
  ./build/bin/adaptor_mpi_tests \
  --gtest_filter='IpcMemHandleMpiTest.CrossGpuLargeGdrSubrangesWriteAsync'
```

⚠️ **警告:`test/unittest/adaptor/Makefile:13` 的默认是 `MPI_NP ?= 8`,而该测试的注释明确要求 2 个 rank。** 直接跑 `make run-mpi` 会用 8 rank,你会得到一堆**假失败**。**必须显式 `MPI_NP=2`。**

#### 必须新增的验证(现有测试覆盖不到的三项 + 上游新增的两项)

现成测试覆盖了"交换/打开/关闭/顺序/指针组合",但【文档 §4.2+§4.5】还有三件事**没有现成测试**:

**① GDR 内存能否被 NetAdaptor 注册成 MR。**
→ 需要写一个小程序:用 `gdrMemAlloc` 分配内存,然后走 FlagCX 的注册路径(或直接调 NetAdaptor 的 regMr),确认成功。这是【文档 §4.2】"异构路径的注册内存"的核心。

**② `FLAGCX_GDR_WRITE_REQUIRES_FLUSH` 但无 READ 的策略是否正确。**
→ 需要跨节点写一个"RDMA READ 后立即读数据、不加 flush"的对照实验。
→ 📌 **上游变更后新增要求**:还要验证新 resolver(`gdr_visibility.h:71-73,86-90`)在 `deviceFamily=UNKNOWN` + `deviceArchitecture=NULL` 下对天数给出的裁决是否合理(应为"保留设备默认 = 仅 WRITE")。用新增的 `test/unittest/core/test_gdr_visibility.cpp` 与 `test/unittest/rma/coll_gdr_visibility.cpp` 做载体。

**③ 生命周期"不交叉"的负向验证。**
→ 现成测试走的是**正确顺序**。建议补"故意错序"的负向测试(如先 `Free` 再 `Close`、用已 `Free` 的 handle 再 `Get`),确认**得到的是干净错误而不是静默成功或崩溃**。这直接对应【文档 §4.5】"不能 silent no-op"。

**④(上游新增)interior pointer 的 `getPointerType` 在 CoreX 上的行为。**
→ 上游给 `test_device_adaptor.cpp:209` 的 `GetPointerType` 加了 `ptr + 64` 的断言,`:277` 的 `GetAddressRangeForInteriorGdrPointer` 也加了。天数新实现的 `getPointerType`(`ixcuda_adaptor.cc:381-407`)依赖 `cudaPointerGetAttributes` 对"基址+偏移"的行为与 NVIDIA 一致 —— **必须实测**。
```bash
cd test/unittest/adaptor && make && ./build/bin/adaptor_unit_tests \
    --gtest_filter='DeviceAdaptorTest.GetPointerType:DeviceAdaptorTest.GetAddressRangeForInteriorGdrPointer'
# 期望:GetPointerType PASS(含 +64 偏移);GetAddressRange... SKIP(getAddressRange 为 NotSupported)
```

**⑤(上游新增)新增 P2P Engine / 共享传输测试套件。**
→ `ff5f80c` 引入了一整批新测试:`test/unittest/p2p/*`(8 个新/改文件,含 `test_p2p_engine_write.cpp`、`coll_p2p_engine_write.cpp`、`test_p2p_pointer.cpp`)、`test/unittest/adaptor/test_net_transport.cpp`、`test/unittest/core/test_kernel_proxy_transport.cpp`。其中 **`test_p2p_pointer.cpp` 直接覆盖 S6 关心的指针语义**,`test/make.inc` 与 `test/kernel/Makefile` 也有改动。
→ 【建议】**先确认这些套件在 `USE_ILUVATAR_ADAPTOR` 下是否参与构建**;若参与,需把它们纳入回归范围(至少确认不因天数的 `NotSupported` 桩而 FAIL)。

#### 状态判定
- 若 15 个用例全过 + GDR 可注册 → 可填"**基础通过**"(【§15】)。
- 若 `hostGetDevicePointer` 仍是 `NULL` 或子区间用例失败 → 必须填"**部分支持**",备注列出限制。

---

## 6. 验收:四条标准命令与判定规则(【文档 §13.3】)

### 6.1 统一拓扑(【文档 §13.3.1】)

| 测试 | 拓扑 | ranks | 设备 | HOSTID |
|---|---|---|---|---|
| T1、T3 | 单节点 Intra | 8 | 8,每 rank 绑一个 | 所有 rank **相同** |
| T2、T4 | Inter(2 逻辑节点) | 8 | 节点0: ranks 0–3 / 节点1: ranks 4–7 | **两组必须不同** |

### 6.2 公共环境基线(【文档 §13.3.2】)

```bash
FLAGCX_USE_HETERO_COMM=1
FLAGCX_VMM_ENABLE=0
LD_LIBRARY_PATH=<FlagCX build/lib>:<MPI lib>:<device runtime libs>:<CCL/SHMEM libs>
```
⚠️ 【文档】明确规定:"厂商可补充 GID、HCA 和 runtime 变量,**但不能通过关闭测试功能、缩小 message size 或跳过失败 case 来获得通过结果**。"

**【建议】额外显式加 `FLAGCX_SIGNAL_HOST_ENABLE=0`** 以保证走 IPC 分支的确定性(见 S5)。

### 6.3 四条标准命令(【文档 §13.3.3~13.3.6】,已套用 OpenMPI 的 `-x`)

```bash
# ---- T1: IntraAllReduce ----
mpirun -np 8 \
  -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_VMM_ENABLE=0 -x LD_LIBRARY_PATH \
  test/unittest/device_api/build/bin/test_device_api_intra \
  -b 1K -e 16M -f 2 -R 2

# ---- T2: InterAlltoAll ----
mpirun -np 8 \
  -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_VMM_ENABLE=0 -x LD_LIBRARY_PATH \
  <天数-rank-wrapper> \
  test/unittest/device_api/build/bin/test_device_api_inter \
  -b 1K -e 16M -f 2 -R 2

# ---- T3: Unified IR Intra ----
mpirun -np 8 \
  -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_VMM_ENABLE=0 -x LD_LIBRARY_PATH \
  test/unittest/device_api/build/bin/test_device_ir_unified_intra \
  -b 1K -e 16M -f 2

# ---- T4: Unified IR Inter ----
mpirun -np 8 \
  -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_VMM_ENABLE=0 -x LD_LIBRARY_PATH \
  <天数-rank-wrapper> \
  test/unittest/device_api/build/bin/test_device_ir_unified_inter \
  -b 1K -e 16M -f 2
```
> 【文档 §13.3.7】:MPICH/Hydra 用 `--genv`;OpenMPI 用 `-x`。**MPI 实现不同不能改变 rank 数、拓扑、size 范围和必测 case。**
> 注意:**T1/T2 带 `-R 2`**(symmetric window 注册模式),**T3/T4 不带**(Unified IR 程序内部固定注册所需 window)。

### 6.4 必测 case(【文档 §13.3.3~13.3.6】)

| 测试 | 必须看到的 | 覆盖能力 |
|---|---|---|
| T1 | `K10 IntraAllReduce(composite)` + `=== Overall: PASS ===`,退出码 **0** | Intra team/peer window、Intra barrier、cooperative kernel、AllReduce sum、1K→16M 连续 size round |
| T2 | `K14 OneSidedAlltoAll` + `=== Overall: PASS ===` | Inter peer/world-rank 映射、Put/remote signal/wait/flush、每源到每宿数据校验、多 size round 下 signal/counter 复用 |
| T3 | S16–S25 全部 PASS,最终 `=== Final Result: ALL PASS ===` | INTRA + WORLD teams |
| T4 | S16–S25 全部 PASS,`ALL PASS`,退出码 0 | INTER + WORLD teams、每 rank 完成 kernel 和 stream 同步 |

**S16–S25 明细(【文档 §13.3.5】):**

| Case | 能力 | | Case | 能力 |
|---|---|---|---|---|
| S16 | DevBarrier | | S21 | DevPutSignalWait |
| S17 | DevTeamResolution | | S22 | DevPut remote signal |
| S18 | DevPut + PutValue | | S23 | DevPutCounter |
| S19 | **DevGet** | | S24 | DevPutValue remote signal |
| S20 | DevSignalStandalone | | S25 | DevSignalShadowFlush |

> 🔎 **S19 DevGet 值得特别留意**:【代码】`test/unittest/device_api/Makefile:87-101` 针对昆仑芯有先例 —— "S19 (DevGet) is compiled out of both unified suites: **reading a peer's memory traps on P800**, so there is no implementation to exercise." **如果天数也有"读对端内存"的限制,S19 会是最先出问题的一个,而且可能需要像 KLX 那样走正式的豁免流程。** 结合 S3 的 coop 分析,这是 T3/T4 最大的不确定点。

### 6.5 超时与失败判定(【文档 §13.3.8】)

```bash
timeout --signal=TERM --kill-after=60s 30m <完整 mpirun 命令>
```
**判为 FAIL 的情形**:`timeout`、`hang`、`MPI abort`、进程被 signal 终止、**任一 rank 返回非零**。

【文档】强调:
- MPI launcher **必须传播任一 rank 的失败**;测试程序**必须汇总所有 rank 的结果**,并在全局失败时返回非零
- **不能只根据局部 PASS 日志、rank 0 的输出或部分 message size 判断成功**
- 每次运行**必须保存完整 stdout/stderr、FlagCX 日志、MPI launcher 日志和厂商 device runtime/CCL/SHMEM 日志**,CI 中作为**失败时仍可下载的 artifacts**
- 建议记录:测试名称、运行序号、开始/结束时间、完整命令、launcher 退出码、日志文件路径(便于回溯偶发 hang)

### 6.6 三次重复(【文档 §13.3.9】)

> 单次通过后**至少重复运行 3 次**,确认没有偶发 hang。

表格里的 `PASS` **仅在三次运行都满足 §13.3.8 的全局成功条件时才能填写**;若失败,要在备注记录:**失败的 run、message size、case、rank、退出码和对应日志位置**。

---

## 7. 交付物清单(【文档 §10.4 / §13.3.9 / §14】)

### 7.1 可复现编译环境模板(【文档 §10.4】,逐字段填,**无对应组件填 `N/A`,不能删除字段**)

```
FlagCX:
  commit:                     <full-commit-id>
  branch/tag:                 <branch-or-tag>
  backend:                    Default
  build arguments:            <完整 make 参数>
Host:
  OS:                         <发行版与版本>
  kernel:                     <内核版本>
  CPU architecture:           <x86_64|aarch64>
  Host compiler:              <名称与版本>
Device:
  vendor/model:               天数智芯 / <芯片型号>
  device count/topology:      8 / <互联方式>
  Device SDK:                 <CoreX 名称与版本>
  driver:                     <版本>
  firmware:                   <版本或 N/A>
  Device compiler:            <clang 版本,ivcore11>
  device architecture:        ivcore11
Communication runtime:
  MPI:                        <实现与版本>
  CCL:                        <名称与版本 或 N/A>
  SHMEM:                      N/A
Artifacts:
  complete build commands:    <命令或附日志>
  libflagcx.so path:          <绝对路径>
  loaded libflagcx.so path:   <ldd 实测结果>   ← 必须来自实际测试可执行文件
```

### 7.2 统一结果表(【文档 §13.3.9】)

| 测试 | 必测 case | 拓扑 | 消息范围 | 连续运行 | 结果 | 日志/备注 |
|---|---|---|---|---|---|---|
| T1 IntraAllReduce | K10 IntraAllReduce | 1×8 | 1K–16M, factor 2 | 3 次 | PASS/FAIL | |
| T2 InterAlltoAll | K14 OneSidedAlltoAll | 2×4 | 1K–16M, factor 2 | 3 次 | PASS/FAIL | |
| T3 Unified IR Intra | S16–S25 | 1×8 | 1K–16M, factor 2 | 3 次 | PASS/FAIL | |
| T4 Unified IR Inter | S16–S25 | 2×4 | 1K–16M, factor 2 | 3 次 | PASS/FAIL | |

其它必交材料(【文档 §13.3.9】):
- FlagCX **commit ID**
- 平台、Device API backend 和**注册模式**(symmetric window / `-R 2`)
- Device SDK、CCL/SHMEM、MPI 和**驱动版本**
- **完整编译命令和四条运行命令**
- 四个测试的**完整日志**
- T1/T2 每个 size 的 PASS 结果;T3/T4 的 `ALL PASS` 总结

### 7.3 天数状态盘点表填写建议(【文档 §15】)

| 项 | 建议填写 | 备注(【§15】要求"部分支持"必须列出缺失接口、运行限制和回退路径) |
|---|---|---|
| 主后端 | Default | |
| DeviceAdaptor | **部分支持** | `launchKernel` 槽位 NULL(若走做法 A)/ `getLastError` 已实现 / `hostGetDevicePointer` 返回 `NotSupported`(host-mapped 模式未实现,已在默认配置关闭,回退:IPC 路径)/ `hostShareMem*`、`gdrPtrMmap`、`memHandleInit` 为 NULL(P1/P2)/ `getPointerType`、`getAddressRange` 未实现(回退:IPC 探测推断类型、调用方保证传 allocation base) |
| PlatformTraits | **部分支持** | `1<N<64` 的子波前 tile 与部分掩码 lanes 返回 trap(CoreX 无已验证的任意掩码 barrier);`scope` 参数未区分;64 位 RMW 为编译错误;`CoopAny` 未使用 `PlatformCoop`(CoreX bitcode 限制) |
| P0 | 待 T1–T4 结果 | |
| T1/T2/T3/T4 | 待填 | |
| 备注 | 必须写 | IPC 路径已选,host-mapped 已关闭;GDR flush 策略为仅 WRITE |

**交付物清单汇总:**
1. ☐ 可复现编译环境模板(§10.4,字段齐全)
2. ☐ 统一结果表(§13.3.9,3 次运行)
3. ☐ 完整日志 artifacts(stdout/stderr + FlagCX + MPI + runtime,失败时也可下载)
4. ☐ 状态盘点表(§15,含备注)
5. ☐ 天数 rank wrapper 脚本(§13.3.1,T2/T4 需要)
6. ☐ `ldd` 实测输出(§10.4)
7. ☐ 挂死风险的排查结论(S3 微基准结果)

---

## 8. 决策记录(提交人不可用,由我方自行拍定)

> **背景**:Jira 提交人没有时间回答澄清问题。因此**以 `FlagCX厂商适配（v3）.pdf` 为唯一权威**,以下 6 条由我方拍定,并写明**理由、前提、回退路径**。
>
> **决策原则(按优先级)**:
> 1. 【文档】明文规定的,照做;
> 2. 【文档】允许等效方案的(如 §4.1 的"或平台对应的 launcher"),选**成本低且能消除风险**的那条;
> 3. 【文档】明确禁止的(§4.5 静默 no-op、§13.3.2 缩小测试范围/跳过失败 case),一律不做;
> 4. 不擅自改上游公共代码(§13.2 把公共测试驱动与 ABI 归 FlagCX 项目负责)。

### 决策速查表

| # | 原问题 | **决策** | 前提条件 | 若不成立则回退 |
|---|---|---|---|---|
| **D1** | S1 `launchKernel` 走哪条路 | **真实现**;不打通公共 API;顺手补两个死 kernel 的 launcher | 无 | — |
| **D2** | S2 是否改上游 | **不改上游**;仅实现 `getLastError` | — | — |
| **D3** | S5 `hostGetDevicePointer` 桩化还是真实现 | **真实现**,并**成套**改 `deviceMalloc(flagcxMemHost)`;`hostRegister/hostUnregister` **保持 NotSupported** | 天数 runtime 支持 `cudaHostAlloc(Mapped)` + `cudaHostGetDevicePointer`(需 10 分钟探针) | 桩化 + 改单测 + **必须给上游提 patch** |
| **D4** | S3 的 trap 算不算不合规 | **不改 trap**;填"部分支持";但 **acquire fence 必须补** | `__syncwarp()` 微基准结果 | 若 `__syncwarp()` 是掩码 barrier → 反而可**支持**部分掩码 |
| **D5** | T3/T4 的 `ALL PASS` 能否打折 | **目标不打折**;仅凭实测证据走正式豁免 | 有失败实测记录 | — |
| **D6** | rank wrapper 谁提供 | **我们自己写**,作为交付物 | — | — |
| **D7** | 上游新增的 GDR 可见性机制怎么应对 | **接受 `UNKNOWN`(不请求新枚举)**;但**必须补两项覆盖**:N1(N1 interior pointer 实测)、N2(把 `IXCUDA` 纳入 flush 默认值断言) | N1/N2 实测结果 | 若 N1 失败 → 与上游讨论是改测试还是改断言 |

---

### D1 — S1:`launchKernel` **真实现**,不打通公共 API

**决策**:在 `ixcuda_adaptor.cc` 实现 `ixcudaAdaptorLaunchKernel(...)` 并填入 `:413`;同时给 `test/kernel/iluvatar/device_api.cu` 的两个死 kernel 补 launcher 与头文件声明。**不**给 `flagcxDeviceHandle` 加字段(不做做法 C)。

**理由**:
1. 【文档 §4.1】把 kernel launch 列在 **P0-Core**(「任意 Device API 后端都需要的公共能力」)。虽然措辞允许"或平台对应的 kernel launcher",但**实现成本极低**(转交 `cudaLaunchKernel`),选实现可同时满足两种解读,不留解释空间。
2. **消除未来崩溃风险**:`NULL` 槽位一旦被上游调用就是空指针崩溃;返回 `flagcxNotSupported` 是"如实不支持";而**真实现**是能力完整。
3. 不打通公共 API 的理由:【文档 §13.2】明确把"公共 Device API intra/inter 测试""保留四个统一测试案例的 Host driver、命令行参数和 PASS/FAIL 语义"归 **FlagCX 项目负责**。厂商改公共 ABI 越界。
4. 补两个死 kernel 的 launcher —— **这是 S3 验证的前提**(它们是目前唯一能覆盖 trap 69/387 与 32 位 RMW 契约的测试)。

**实现要点(照抄时最易错的三处)**:
- ⚠️ **block/grid 顺序相反**:FlagCX 签名是 `(block_x,y,z, grid_x,y,z)`,而 `cudaLaunchKernel(func, dim3 gridDim, dim3 blockDim, args, sharedMem, stream)` 是 **grid 在前**。必须写 `dim3 grid(grid_x,grid_y,grid_z), block(block_x,block_y,block_z);`。**位置照抄会转置,而 36×512 转置后依然"启动成功",只是结果错/挂死。**
- ⚠️ **`void *stream` 是 `flagcxStream_t`**:用 `((flagcxStream_t)stream)->base`;`NULL` 表示 legacy default stream。
- ⚠️ **不要用 `DEVCHECK`**:【文档 §4.1】要求"在 kernel launch 和 runtime 调用失败时返回**有效错误**",而【代码】`iluvatar_adaptor.h:26-31` 的 `DEVCHECK` 会把 `cudaError_t` 一律压成 `flagcxUnhandledDeviceError`。应直接读 `cudaLaunchKernel` 的返回值并映射。
- `memHandle` 全仓库无人使用 → `(void)memHandle;`。
- 参数校验:`func == NULL` / `args == NULL` / 维度为 0 → `flagcxInvalidArgument`(与 `ixcudaAdaptorGdrMemAlloc:101-103` 的风格一致)。

**`copyArgsInit` / `copyArgsFree` / `launchDeviceFunc`**:保持 `NULL`。理由:三者**都不在**【文档 §4.1 P0-Core】的必测清单里;`launchDeviceFunc` 只在 `FLAGCX_DEVICE_FUNC_PATH` 被设置时由 `flagcx/core/group.cc:403-413` 使用,不属 Default 验收路径。→ **在交付备注登记**(§15 要求)。

---

### D2 — S2:`getLastError` **实现**;**不改上游**

**决策**:新增 `ixcudaAdaptorGetLastError()`,照抄【代码】`cuda_adaptor.cc:1106-1109` 的 4 行,填入 `:446`。**不改** `flagcx/core/flagcx_p2p.cc`。

**理由**:
1. 成本 4 行,收益是消除"粘性错误残留"这一真实缺陷(见 §5.2)。
2. `p2p_pointer.cc:94-95`(旧 `flagcx_p2p.cc:1641-1642`)**丢弃返回值是正确设计**(它就是来消费/清除粘性错误的),**改它反而破坏语义**。
3. `flagcx/core/launch_kernel.cc:48` 丢弃返回值确实是**上游缺陷**,但**不靠改上游解决** —— 见 D3:真实现 `hostGetDevicePointer` 后该处自然成功赋值,缺陷不再被触发。

**注意**:实现时必须**不用 `DEVCHECK`**(它无法区分"无错误"与"系统错误"),且只返回 `flagcxSuccess` / `flagcxSystemError` 两个值 —— 天数没有更细的错误映射。

---

### D3 — S5:`hostGetDevicePointer` **真实现 + 成套修改**;`hostRegister/hostUnregister` 保持 `NotSupported`

> **这是本次 6 条决策中最重要的一条,它修正了我先前的一个错误判断。**

**先纠正我之前的结论**:我先前建议"把 `hostGetDevicePointer` 从 `NULL` 改成返回 `flagcxNotSupported` 的桩"。**核对参照实现后确认这个方案是错的**,理由如下。

**决策**:
1. `hostGetDevicePointer` → **真实现**(`cudaHostGetDevicePointer`),填入 `:387`。
2. `hostRegister` / `hostUnregister` → **保持 `flagcxNotSupported` 不变**(`:341-346`)。
3. **同时**把 `ixcudaAdaptorDeviceMalloc` 的 `flagcxMemHost` 分支从 `cudaMallocHost` 改成 **`cudaHostAlloc(ptr, size, cudaHostAllocMapped)`**。

**理由 1:实现是主力做法,桩是少数派。**
【代码】8 家厂商**完整实现了三件套**:`cuda_adaptor.cc:126,612,620`、`ducuda_adaptor.cc:172,581,589`、`kunlunxin_adaptor.cc:116,419,428`、`maca_adaptor.cc:117,535,542`、`ptpu_adaptor.cc:90,369,375`、`ppu_cuda_adaptor.cc:112,551,559`、`tops_adaptor.cc:103,388,396`、`tsmicro_adaptor.cc:95,364,367`。只有天数、`hip`、`cann`、`mlu`、`musa` 是桩。→ **实现是有充分先例的主流选择。**

**理由 2:桩化会造成"更隐蔽的失败",比崩溃更糟。**
【代码】三处调用**与 `SIGNAL_HOST_ENABLE` 完全无关**,是常规路径:
- `flagcx/core/launch_kernel.cc:43-48`:`deviceMalloc(&signalsPool, ..., flagcxMemHost, ...)` 之后**立刻**取 device 别名 `dSignalsPool`,**返回值被丢弃**
- `flagcx/core/proxy.cc:2088-2097`:FIFO buffer 取 device 别名
- `flagcx/runner/uni_runner_impl.cc:1901-1904`:同上

桩化后:这三处会**拿到无效指针继续跑**。`launch_kernel.cc:48` 尤其糟 —— `dSignalsPool` 保持构造时的 `nullptr`(`:10`、`:15`),而 `:70` 的 `getDevicePtr()` 返回 `(char*)nullptr + offset` → **SIGSEGV 变成"向 kernel 传伪造的小指针"**。

**理由 3(根因,之前完全漏掉了):天数少了一个 `Mapped` 标志。**
```
NVIDIA  cuda_adaptor.cc:76-77   cudaHostAlloc(ptr, size, cudaHostAllocMapped)   ← 带 Mapped
天数    ixcuda_adaptor.cc:49-50  cudaMallocHost(ptr, size)                       ← 不带 Mapped
```
**`cudaHostAlloc(..., cudaHostAllocMapped)` 才会把这块 host 内存映射进设备地址空间,`cudaHostGetDevicePointer` 才拿得到有效别名。`cudaMallocHost` 只是 page-locked,不映射。**

⇒ **所以"只补 `hostGetDevicePointer`"是无效的** —— 必须**成套改**:分配侧加 `cudaHostAllocMapped`,获取侧才有意义。这正好解释【文档 §4.3】为什么把三个接口列成一组(它们是一个成套能力)。

**理由 4:为什么 `hostRegister/hostUnregister` 可以保持桩,而 `hostGetDevicePointer` 不行 —— 这个不对称是本质的。**

| 接口 | 它的输入内存从哪来 | 服务于谁 | 该路径默认状态 |
|---|---|---|---|
| `hostGetDevicePointer` | `deviceMalloc(flagcxMemHost)`(即 `cudaHostAlloc`) | **常规路径**(launch_kernel / proxy / uni_runner **无条件**调用) | **总是走** |
| `hostRegister` / `hostUnregister` | 外部 mmap 的内存(`/dev/shm` barrier,`flagcxShmOpen`) | **只有** `SIGNAL_HOST_ENABLE=1` 的 shm barrier 路径(`default_dev_api_backend.cc:211` 的 else 分支) | **默认关闭**(`utils.cc:119` 默认 0) |

【文档 §4.3】的规则正是针对后者的:「**如果厂商不支持该路径**,应明确返回 `flagcxNotSupported`,并在平台默认配置中关闭对应模式。」
→ **Jira 那句"验收走 IPC 时保持这个返回值",指的就是 `hostRegister/hostUnregister` 这两个**;`hostGetDevicePointer` **不在**那句话的范围内。

**理由 5:副产品 —— 单测原样通过,不用改。**
【代码】`test/unittest/adaptor/test_device_adaptor.cpp:760-779` 期望 `flagcxSuccess`(`:770`)。真实现后**这个测试直接通过**,无需改动。这也是判断"真实现才是设计意图"的有力旁证 —— 因为该测试是**通用**测试,而它期望的是**成功**。

**⚠️ 唯一前提:天数 CoreX runtime 必须支持这三个 API。**
必须实测:`cudaHostAlloc(..., cudaHostAllocMapped)`、`cudaHostGetDevicePointer`、`cudaFreeHost`。

**P2 阶段第一步:写一个 20 行探针程序,10 分钟内定论。**
```c
// 探针:验证 host-mapped 能力(放在一个临时 main 里编译运行)
void *h = NULL; void *d = NULL;
cudaError_t e1 = cudaHostAlloc(&h, 4096, cudaHostAllocMapped);
cudaError_t e2 = e1 == cudaSuccess ? cudaHostGetDevicePointer(&d, h, 0)
                                   : cudaSuccess;
printf("hostAllocMapped=%d hostGetDevicePointer=%d devPtr=%p\n", e1, e2, d);
if (h) cudaFreeHost(h);
```

**回退路径(若探针失败)**:
1. `hostGetDevicePointer` 改为返回 `flagcxNotSupported` 的桩(消除崩溃);
2. 同步修改 `test/unittest/adaptor/test_device_adaptor.cpp:770` 使其接受 `flagcxNotSupported`(与【文档 §2】精神一致);
3. **必须向上游 FlagCX 提 patch**,修 `flagcx/core/launch_kernel.cc:48` 的返回值未检查问题 —— 因为该缺陷会让**所有**不实现 host-mapped 的厂商(hip/cann/mlu/musa + 天数)都静默拿到 nullptr;
4. 在交付备注登记:"host-mapped 能力不可用,已在平台默认配置关闭;回退路径 = IPC"【§15】。

---

### D4 — S3:**不改 trap**;填"部分支持";但 **acquire fence 必须补**

**决策**:
1. **6 个 trap 保持原样**。不改 `iluvatar_platform_traits.h` 的 trap 逻辑。
2. **必须补 acquire fence**(`:165-170`)。
3. **`scope` 走"实测 + 登记"**:先验证 `__threadfence` 是否真能达到 system 可见性;达到则加注释说明"名义未区分、实际对齐"并在备注登记;达不到则必须修。
4. 交付表 **PlatformTraits 填"部分支持"**,备注列出限制与回退。
5. **先做微基准,再动这个文件。**

**理由**:
- **不改 trap**:两个独立调查已逐点证实(标为 FACT)6 个 trap **当前验收 kernel 集一个都碰不到**;而【代码】`iluvatar_platform_traits.h:67-68` 的注释自己警告"扩宽部分掩码会导致死锁"。**为了"消除 trap"而扩宽掩码,正是引入挂死的做法。**
- **但 trap 的存在要如实登记**。【文档 §5.1】用了"必须"("厂商**必须**根据真实执行模型实现 mask、tile 和 cooperative group 语义"),而天数对 `1<N<64` 一律 trap,等于 tile 语义只实现了 `1` 和 `64` 两个特例。→ 按【文档 §15】填"**部分支持**",备注写:「`1<N<64` 的子波前 tile 与部分掩码 lanes 不支持,返回 trap;回退路径 = 使用 `CoopBlock` 或 `CoopWarp`」。**注意:返回 trap 至少是"明确的失败",好过静默 no-op,符合 §2/§4.5 的精神。**
- **acquire fence 必须补,没有商量余地**:【文档 §5.2】原文点名"**仅保证原子性但忽略 acquire/release 可见性,会产生难以复现的跨 rank hang**"。而【代码】`:165-170` 的 acquire/acq_rel/seq_cst 是**裸 volatile 读、无 fence**,release 侧(`:181`)却有 fence —— **不对称,且逐字命中文档警告**。所有 barrier 自旋循环都吃这条路径(`default_comm_traits.h:1124-1128`、`:1236-1248`)。
  ⚠️ 性能需评估:每条自旋都 fence 会变慢。可考虑"自旋中不 fence、成功退出后 fence"的折中,但**必须保证文档要求的 acquire 语义**。
- **`scope` 的取证方式**:【代码】`threadfenceSystem()==threadfenceDevice()==__threadfence`。`device_utils.h:180-182` 注释声称 ivcore11 选不了 `llvm.nvvm.membar.sys`,故降级为 CoreX 的 cache 回写/失效。**这个降级在硬件上是否真的达到 system 可见性,必须用对照实验证明**(跨 rank 写后读),不能凭注释采信。
- **微基准必须先做**:它同时决定两件事 ——(a)`__syncwarp()` 是全波前还是掩码 barrier(决定挂死修复方向);(b)`__activemask()` 是否真是 64 位(`:49-51` 的 `static_assert` 从未执行过)。
  → **若结论是"掩码 barrier"**:则反弹为**可以支持部分掩码**,把 `:59-70` 的 trap 改成真正的 `__syncwarp((uint32_t)mask)`,**一次同时解决 trap 69/387 和挂死**,PlatformTraits 也可从"部分支持"升级。

---

### D5 — T3/T4 的 `ALL PASS`:**目标不打折,但预设豁免预案**

**决策**:
1. **验收目标不变**:S16–S25 全部通过,`=== Final Result: ALL PASS ===`,退出码 0,连续 3 次。
2. **禁止预先打折**。【文档 §13.3.2】明确:"厂商可补充 GID、HCA 和 runtime 变量,**但不能通过关闭测试功能、缩小 message size 或跳过失败 case 来获得通过结果**。"
3. **只在拿到实测证据后**才考虑豁免。若实测证明某项受硬件限制(最可疑的是 **S19 DevGet** —— "读对端内存"),则按昆仑芯先例走正式豁免:【代码】`test/unittest/device_api/Makefile:87-101` 的 `FLAGCX_TEST_NO_REMOTE_READ` / `FLAGCX_TEST_NO_REMOTE_THREAD_PUT`。
4. 豁免**必须在交付说明登记**"缺失能力 + 运行限制 + 回退路径"【§15】。

**理由**:昆仑芯的先例证明"全部通过"在真实硬件上不一定可达,但**那是实测后的正式豁免,不是预先声明**。两者的区别是"有证据的让步"和"无证据的放弃"。→ 先跑,再说。

---

### D6 — rank wrapper:**我们自己写**,作为交付物

**决策**:新增 `test/script/iluvatar_rank_wrapper.sh`(与既有 `test/script/*.sh` 一致),按【文档 §13.3.1】实现。**变量名已全部确认**:

| 变量 | 用途 | 依据 |
|---|---|---|
| `CUDA_VISIBLE_DEVICES` | 按 rank 组设可见设备 | 【代码】仓库通用约定:`docs/user_guide.md:116`、`test/script/torch_api_test.sh:4`;且 **`dsh-fixes/README.md:32` 在天数/FlagCX 场景实际用过 `-x CUDA_VISIBLE_DEVICES=1,2`** |
| `FLAGCX_HOSTID` | 逻辑节点标识 | 【代码】`test/unittest/device_api/Makefile:196-199` 已在用 |
| `FLAGCX_IB_HCA` | 指定 IB HCA | 【代码】`docs/environment_variables.md:148`;消费方 `ucx_adaptor.cc:355`、`ibuc_adaptor.cc:402`、`ibrc_adaptor.cc:626`。**天数示例值见 `docs/paddle/iluvatar.md:81`:`FLAGCX_IB_HCA=mlx5_101`** |
| `FLAGCX_IB_GID_INDEX` | 可选,GID 索引 | 【代码】`docs/environment_variables.md:149`,默认 `-1` 自动 |

**实现必须遵守【文档 §13.3.1】的三个细节**(文档专门用一段强调):
1. 条件是**闭区间**:`ranks 0–3` 和 `ranks 4–7`,**不是"只选择 rank 0、3、4、7"**;
2. shell 要用数值比较语法,如 Bash 的 `[[ $rank -ge 0 && $rank -le 3 ]]`;
3. 非法 rank 必须**报错退出**:`echo "unexpected MPI rank: ${rank}" >&2; exit 1`。

另外【文档 §13.3.1】提示:"如果厂商通信库也使用 Host ID、visible devices 或 NIC 变量,wrapper 中应同步设置厂商 CCL 对应变量。" → 天数侧需确认是否需要同步设 CCL 变量(如 `NCCL_HOSTID`,【代码】Makefile 里已在用)。

---

### D7 — 应对上游新增的 GDR 可见性机制:**接受 `UNKNOWN`,但补齐两项覆盖**

**决策**:
1. **不请求**上游为 `flagcxGdrDeviceFamily_t` 添加天数条目。天数保持 `FLAGCX_GDR_DEVICE_UNKNOWN` + `getDeviceArchitecture = NULL`(零初始化)。
2. **必须补** N1(interior pointer 实测)与 N2(把 `IXCUDA` 纳入 flush 默认值断言)。
3. **暂不补** N3(新 GDR 可见性 kernel 覆盖),作为**后续项**登记;若补,则 N4 必须一起修。

**理由**:
- **`UNKNOWN` 是安全且正确的默认**。复核确认:resolver 不会因 UNKNOWN 报错或告警(`gdr_visibility.cc:22-24` 接受它),CUDA 专属豁免分支 `:82-84` 要求 `deviceFamily == FLAGCX_GDR_DEVICE_CUDA`,天数永不进入;唯一后果是 reason 位 `FLAGCX_GDR_VISIBILITY_REASON_DEVICE_DEFAULT`(`:72-74`)。也就是说**天数会如实保留"仅 WRITE 需要 flush"的设备默认,不会被误加/误减任何豁免**。
- **不请求新枚举**的理由:枚举属于 `flagcx_device_adaptor.h` 这个**冻结的 ABI 面**,为一个厂商加成员需要上游评审;而按上面的分析,加了也不会改变天数的实际策略(仍无豁免可得,因为 Hopper/DataDirect-C2C 条件是天数硬件不满足的)。**收益为零、成本是越界改 ABI** → 不做。若将来天数需要差异化策略,应由上游主动扩展。
- **N1 必须实测**:它把 `GetPointerType` 从"跳过"变成"真跑",是**本次上游变更中对天数最可能造成新失败的单一项**。依赖 CoreX 的 `cudaPointerGetAttributes` 对"基址+偏移"的行为。**这是 P3 阶段的第一优先验证项。**
- **N2 必须补**:天数"只声明 WRITE"这个关键策略决定,目前**没有任何测试断言**(`test_device_adaptor.cpp:112` 因 `IXCUDA` 不在支持列表而 SKIP)。而其他平台的期望是 `READ|WRITE` —— 所以天数需要**在 `:100-105` 的保守列表旁增加一个独立分支**,断言 `IXCUDA` 的默认是 `FLAGCX_GDR_WRITE_REQUIRES_FLUSH`。这符合【文档 §15】"以真实测试结果为准,不应根据存在同名函数就标记为已支持"的精神。
  ⚠️ 注意:这属于**改公共测试源**,按 D2/D4 的一贯口径,应作为 **upstream patch** 提出,不混在厂商适配提交里。
- **N3 暂缓**:天数当前没有任何 GDR 可见性 conformance 覆盖,但对等覆盖需要 4 处联动改动(含 `#elif defined(USE_ILUVATAR_ADAPTOR)` 与新 shim `.cu`)。**它不阻塞 T1–T4 验收**,而且 N4 的潜伏构建断点与之绑定。→ 登记为后续项,在交付说明里如实写明"GDR 可见性 conformance 覆盖未接入(回退:使用 FlagCX 统一回归)"。

**N1 的验收命令(P3 第一件事)**:
```bash
cd test/unittest/adaptor && make && ./build/bin/adaptor_unit_tests \
    --gtest_filter='DeviceAdaptorTest.GetPointerType'
# 期望 PASS。若失败,记录是哪一段(host/device/managed 的 +64 偏移),
# 因为那决定是"CoreX runtime 行为差异"还是"我们的实现要补基址解析"。
```
**N2 的验收命令**(需先打 upstream patch):
```bash
./build/bin/adaptor_unit_tests --gtest_filter='GdrFlushPolicyTest.*'
# 期望 ActivePlatformUsesDocumentedDefault 不再对 IxCUDA SKIP
```

---

## 9. 风险清单(按严重度排序)

| 级别 | 风险 | 证据 | 影响 | 缓解 |
|---|---|---|---|---|
| 🔴 **P0** | `hostGetDevicePointer` 为 NULL → 3 处裸调用 **SIGSEGV** | `ixcuda_adaptor.cc:415`(旧 :387);`launch_kernel.cc:48`、`uni_runner_impl.cc:1903`、`proxy.cc:2094`;**`dsh-fixes/README.md` 已记录 exit=139 实例** | 测试直接崩,无法验收 | **D3:真实现**(不做桩化) |
| 🔴 **P0** | **根因**:`flagcxMemHost` 用 `cudaMallocHost`(**无 `Mapped`**)→ `cudaHostGetDevicePointer` 必然拿不到别名 | `ixcuda_adaptor.cc:49-50` vs `cuda_adaptor.cc:76-77`(`cudaHostAlloc(..., cudaHostAllocMapped)`) | host-mapped 常规路径全部失效;**只补 `hostGetDevicePointer` 无效,必须成套改** | **D3 改动 2** |
| 🔴 **P0** | 32/64 波前扩宽 → **静默挂死** | `device_ir.cu:1497-1501,1558,1568` vs `iluvatar_platform_traits.h:277,356,380` | T3/T4 30 分钟超时失败,且排查方向被误导 | S3 微基准先行;据结论改守卫或改分类 |
| 🔴 **P0** | `scope` 丢弃 + acquire 无 fence | `iluvatar_platform_traits.h:160,176,165-170`;**【文档 §5.2】明文警告"难以复现的跨 rank hang"** | 偶发数据错 / 偶发 hang,最难查 | S3 第 3 步 |
| 🟠 **P1** | `getAddressRange` 缺失 → 基址探测静默关闭 | `ixcuda_adaptor.cc:476`(旧 :448);`flagcx_device.cc:414,424-425` | 子区间 IPC handle 指向错误基址(静默错) | S6 重点跑 `CrossGpuLargeGdrSubrangesWriteAsync`(单测因 `NotSupported` 会 SKIP,**不能靠它兜底**) |
| 🟠 **P1** | **新**:`getPointerType` 的 **interior pointer** 行为未在 CoreX 上验证 | `test_device_adaptor.cpp:209`(`GetPointerType`)、`:277`(`GetAddressRangeForInteriorGdrPointer`)新增 `+64` 偏移断言 | 若 CoreX 的 `cudaPointerGetAttributes` 对偏移指针行为与 NVIDIA 不同 → 新单测失败 | P3 阶段单跑该测试;`GetAddressRange...` 预期 SKIP |
| 🟡 **P2** | **新**:GDR 可见性策略改由 `gdr_visibility` 子系统裁决;天数落为 `deviceFamily=UNKNOWN` + `getDeviceArchitecture=NULL` | `gdr_visibility.h:43-56,82-85`;`ixcuda_adaptor.cc:352` 零初始化 | 若某路径要求已知 family/arch 才能得出正确 flush 需求,天数会走"保留默认"分支 | 用 `test/unittest/core/test_gdr_visibility.cpp`、`test/unittest/rma/coll_gdr_visibility.cpp` 验证;必要时**显式**填 `gdrDeviceFamily`(但需先确认上层是否需要) |
| 🟠 **P1** | GDR flush 仅声明 WRITE,无 READ | `ixcuda_adaptor.cc:452` vs `cuda_adaptor.cc:1231` | RDMA READ 可见性可能不足 | S6 对照实验 |
| 🟠 **P1** | `gdrMemAlloc` 无 RDMA 能力查询 | `ixcuda_adaptor.cc:99-111` | GDR 内存可能无法注册成 MR | S6 新增注册验证 |
| 🟠 **P1** | `launchKernel` NULL + 无调用者 | 全仓库零调用点 | 若上游将来调用 → 崩溃 | **D1:真实现** |
| 🟡 **P2** | 验收命令 ≠ Makefile run 目标 | `test/unittest/device_api/Makefile:186,212,232` | **跑错条件却以为通过** | 手工敲【§13.3.3~6】 |
| 🟡 **P2** | `MPI_NP` 默认 8 vs IPC 测试需要 2 | `test/unittest/adaptor/Makefile:13` | 一堆假失败 | 显式 `MPI_NP=2` |
| 🟡 **P2** | 隐藏的未解析符号 | `makefiles/iluvatar.mk:31-32` 注释 | 编译成功、运行时才炸 | `nm -D --undefined-only` 自检 |
| 🟡 **P2** | `__syncwarp()`/`__activemask()` 语义未知 | 调查标记 UNDETERMINED | 结论不可靠 | S3 微基准 |
| ⚪ **P3** | 缺 rank wrapper / 无 iluvatar CI | `.github/configs/` 无 `iluvatar.yml` | T2/T4 跑不起来;问题不可回归 | **D6:自己写 `test/script/iluvatar_rank_wrapper.sh`**;建议加 CI |
| ⚪ **P3** | 两个天数 kernel 是死代码 | `test/kernel/iluvatar/device_api.cu:7,29` | trap 行为无测试支撑 | S1 补 launcher |

---

## 10. 【文档】关键节号索引(方便你回查原文)

| 节号 | 内容 | 对应本文 |
|---|---|---|
| §1 | Device API 适配架构(三层抽象职责) | 1.1 |
| §2 | 优先级定义(P0-Core/Default/CCL/SHMEM、P1、P2)+ 槽位≠支持 | 1.2、1.3 |
| §3 | 后端选型(Default/CCL/SHMEM)+ 推荐顺序 | 0.1 |
| §4.1 | P0-Core 基础设备运行时(**含 kernel launch、错误处理**) | **S1、S2** |
| §4.2 | P0-Default:IPC 和 GDR | **S5、S6** |
| §4.3 | 条件 P0:Host signal/barrier 路径 | **S5** |
| §4.4 | P1:VMM 对称映射和 Multicast(不阻塞) | 2.1 |
| **§4.5** | **DeviceAdaptor 验收要点(6 条)** | **S2、S5、S6** |
| §5.1 | P0-Core Intrin(simtWidth 不假设 32) | **S3** |
| **§5.2** | **P0-Core Atomic(5 种内存序 + 4 种 scope)** | **S3** |
| §5.3 | P0-Core Cooperative 类型 | **S3** |
| §5.4 | 参考实现(NVIDIA/DU) | S3 |
| §6.1~6.4 | CommTraits(Default 复用 FlagCX 实现) | 1.1 |
| §7 | CCLAdaptor(**与天数的 Default 路径无关**) | — |
| §8 | SHMEMAdaptor(无关) | — |
| §9 | Default 后端适配 + 数据路径 | S5 |
| §10.1~10.2 | 平台 `.mk` 模板、后端选择 | S4 |
| **§10.3** | **统一编译流程(三部分同参数)** | **S4** |
| **§10.4** | **编译验收标准 + 交付环境模板** | **S4、7.1** |
| §11 | 推荐代码布局 | — |
| §12 | 适配步骤(四阶段) | 0.1、3.2 |
| §13.1/13.2 | 验收责任边界(厂商 / FlagCX) | 8、9 |
| **§13.3.1** | 统一拓扑(含 rank wrapper) | 6.1、S4 坑 3 |
| **§13.3.2** | 公共运行环境(3 变量 + `-R 2`) | 6.2、S4 坑 2 |
| §13.3.3~6 | T1/T2/T3/T4 命令 | 6.3 |
| §13.3.5 | S16–S25 明细 | 6.4 |
| §13.3.7 | MPI 参数兼容(`--genv` vs `-x`) | 6.3 |
| **§13.3.8** | **超时、失败判定与日志** | **6.5** |
| **§13.3.9** | **结果提交(3 次重复 + 结果表)** | **6.6、7.2** |
| §14 | 厂商交付检查表 | 2.1 |
| **§15** | 厂商适配状态盘点(5 个状态词) | **0.3、7.3** |
| §16.1~16.6 | 常见问题 | 1.1、1.3 |

---

## 附录 A:天数 vs NVIDIA 关键差异对照

| 维度 | NVIDIA | 天数智芯 | 依据 |
|---|---|---|---|
| `FLAGCX_SIMT_WIDTH` | **32** | **64** | `device_utils.h:238` / `:189` |
| block 线程数(兜底默认) | 512 | 512 | `flagcx_kernel_core.h:182-183` |
| block 数(兜底默认) | 36 | 36 | `flagcx_kernel_core.h:179-180` |
| `FLAGCX_DEVICE_THREAD_FENCE` | `__threadfence_system` | **`__threadfence`**(device scope) | `device_utils.h:230` / `:183` |
| `FLAGCX_DEVICE_SYNC_THREADS` | `__syncthreads` | `__syncthreads` | `device_utils.h:231` / `:184` |
| `fullMask()` | `0xffffffff` | `~uint64_t{0}` | `nvidia_platform_traits.h:32-34` / `iluvatar:26-28` |
| `namedBarrierSync` | `__barrier_sync_count(id, n)`(**用 id**) | 只用 `n`;`n=blockDim`→`__syncthreads`;`n=1`→空;否则 **trap** | `nvidia:70-73` / `iluvatar:72-80` |
| 部分掩码 `syncwarp` | ✅ 支持任意掩码 | ❌ **trap** | `nvidia:59-63` / `iluvatar:59-70` |
| `CoopAny` | 真实 `PlatformCoop` 虚表 | 自写 24 字节 + `SyncKind`(bitcode 限制) | `nvidia:401` / `iluvatar:329-396` |
| 64 位 RMW | 支持 | **编译错误**(`static_assert`) | `iluvatar:188` 等 |
| `getLastError` | ✅ 已实现(4 行) | ❌ **NULL** | `cuda_adaptor.cc:1106-1109` / `ixcuda:474`(旧 :446) |
| `launchKernel` | ❌ NULL | ❌ NULL | `cuda_adaptor.cc:1188` / `ixcuda:441`(旧 :413) |
| `launchDeviceFunc` | ✅ 已实现 | ❌ NULL | `cuda_adaptor.cc:1194,512-518` / `ixcuda:447` |
| `hostGetDevicePointer` | ✅ 已实现 | ❌ **NULL** | `ixcuda:415`(旧 :387) |
| `getPointerType` | ✅ 已实现 | ✅ **上游已实现**(`7826ec8 #640`) | `cuda_adaptor.cc:1111-1141` / `ixcuda:381-407`,接线 `:475` |
| `getAddressRange` | ✅ 已实现 | ❌ NotSupported(相关测试会 SKIP) | `ixcuda:476`(旧 :448) |
| `gdrDeviceFamily`(新增) | CUDA | **UNKNOWN(零初始化)** → 保守,无 CUDA 豁免 | `flagcx_device_adaptor.h:26-30` / `ixcuda:352` |
| GDR flush 策略 | `READ \| WRITE` | **只有 WRITE** | `cuda_adaptor.cc:1231` / `ixcuda:480`(旧 :452) |
| device-link | 需要 `-dlink` | **不需要** | `iluvatar.mk:20` |
| 设备文件扩展名 | `.cu`(nvcc) | `.cu` 但用 `clang -x ivcore` | `iluvatar.mk:16-17` |
| CI | `.github/configs/cuda.yml` | **无** | `.github/configs/` |
| dev api backend 源文件 | `nccl_dev_api_backend.cc` | `default_dev_api_backend.cc` | `iluvatar.mk:33` |

---

## 附录 B:本文引用的关键 `文件:行号` 清单(便于快速核对)

> ⚠️ **行号已对齐基线 `ff5f80c`。** 引用 `ixcuda_adaptor.cc` / `flagcx_device_adaptor.h` / `p2p_pointer.cc` / `test_device_adaptor.cpp` 时,完整的新旧对照见 **§0.4 的行号重映射表**。

**Adaptor / 结构体定义**(基线 `ff5f80c`)
- `flagcx/adaptor/include/flagcx_device_adaptor.h`:`:26-31`(新 `flagcxGdrDeviceFamily_t`)、`:69`(新 `IPC_POINTER_INFERENCE` 位)、`:79-80`(GDR READ/WRITE 标志)、`:150-155`(v1 `launchKernel`)、`:236-241`(latest `launchKernel`)、`:306`(`getLastError`)、`:348`(`gdrFlushRequirements`)、`:352`(**新** `gdrDeviceFamily`)、`:357`(**新,末位** `getDeviceArchitecture`)、`:371-375`/`:378-385`(两个 NotSupported 桩)、`:399`(`UpgradeV1`)
- `flagcx/adaptor/device/ixcuda_adaptor.cc`:`:49-50`(**`flagcxMemHost` 用 `cudaMallocHost`,缺 `Mapped`**)、`:99-111` `gdrMemAlloc`、`:113-119` `gdrMemFree`、`:239-280`(5×IPC)、`:341-346`(hostRegister/Unregister 桩)、**`:381-407`(新 `ixcudaAdaptorGetPointerType`)**、`:409-481`(结构体初始化)、`:415`(`hostGetDevicePointer` = NULL)、`:441`(`launchKernel` = NULL)、`:445-447`(`copyArgs*`/`launchDeviceFunc` = NULL)、`:465`/`:467`、**`:474`(`getLastError` = NULL)**、`:475`(`getPointerType` 接线)、`:476`(`getAddressRange` NotSupported)、`:477`(`internalFlags` = NONE)、`:480`(GDR flush 仅 WRITE)
- `flagcx/adaptor/device/cuda_adaptor.cc:1106-1109`(`getLastError` 参考实现)、`:512-518`(`launchDeviceFunc`)、`:1188`、`:1231`、`:76-77`(`cudaHostAlloc(..., Mapped)` 参照)
- `flagcx/core/include/gdr_visibility.h`:**新子系统** `:43-56`(`Context`,含 `deviceFamily`/`deviceArchitecture`)、`:58-61`(`Decision`)、`:71-73`(`flagcxResolveGdrVisibilityPolicy`)、`:77-80`(`flagcxClassifyGdrTopology`)、`:86-90`(`...ForConnection`,fail-closed 说明 `:82-85`)、override 语义 `:66-70`
- `flagcx/adaptor/kernel/iluvatar/device_api_host_helpers.cu:11-30`

**PlatformTraits**
- `flagcx/adaptor/include/device_api/iluvatar_platform_traits.h`: `:24` simtWidth、`:26-28` fullMask、`:30-33` validateMask(no-op)、`:45-53` activemask+static_assert、`:59-70` syncwarp/trap 69、`:72-80` namedBarrierSync/trap 79、`:82-84` spinBackoff、`:86-92` threadfence*、`:157-171` Atomic::load、`:173-183` Atomic::store、`:185-239` RMW、`:257-274` CoopTile、`:277` CoopWarp、`:279-302` CoopTileSpan/traps 287,300、`:304-324` CoopLanes/trap 312、`:329-390` CoopAny/trap 387、`:393-396` ABI static_assert
- `flagcx/adaptor/include/device_utils.h`: `:132`(KLX trap)、`:166`、`:183`、`:184`、`:189`、`:230`、`:238`
- `flagcx/adaptor/include/device_api/du_platform_traits.h:64-67`(**静默 no-op 反例**)
- `bindings/ir/iluvatar/Makefile:113-131`(6 道构建守卫)

**IPC / GDR / 配置**
- `flagcx/service/utils.cc:119`(`SIGNAL_HOST_ENABLE` 默认 0)
- `flagcx/adaptor/device_api/default_dev_api_backend.cc:48-59`、`:179`、`:211`、`:276`
- `flagcx/adaptor/flagcx_device.cc:406-447`(尤其 `:414`、`:417`、`:424-425`)
- `flagcx/core/p2p_pointer.cc:15-101`(**新的指针类型判定**,`flagcxP2pDetectPointerType`;`:32-38` 两个开关、`:40-50` 权威 `getPointerType` 优先、`:52-53` 无来源则 `NotSupported`、`:64-65` 判定 HOST 提前返回、`:83` IPC 导出、**`:94-95` `getLastError()` 调用点**)
- `flagcx/core/launch_kernel.cc:10,15,43-48,70`
- `flagcx/runner/uni_runner_impl.cc:1903-1906`
- `flagcx/core/proxy.cc:2093-2097`
- `flagcx/include/flagcx_kernel_core.h:179-183`;`flagcx/adaptor/include/device_api/flagcx_device_core.h:821-825`
- `docs/environment_variables.md:168`
- `flagcx/adaptor/device/device_plugin_load.cc:66-71`

**构建 / 测试**
- `makefiles/iluvatar.mk:8,13,14,20,21,27,29,30,31-32,33`
- `test/kernel/iluvatar/Makefile:12-14,26-27,32-33,35-36`
- `test/unittest/device_api/Makefile:18-19,62,79-84,125-129,146-174,183-199,200,204-265`
- `test/unittest/adaptor/Makefile:9,11,13`
- `test/unittest/adaptor/coll_ipc_mem_handle.cpp:470,588-660`
- `test/unittest/adaptor/test_device_adaptor.cpp:760-779`
- `test/perf/host_api/test_ipc_sendrecv.cpp:63-171`;`test/perf/Makefile.old:88-90,145`
- `test/kernel/iluvatar/device_api.cu:3,7,25-32`;`test/kernel/nvidia/device_ir.cu:76-105,131,1497-1501,1558,1568`;`test/kernel/nvidia/device_api.cu:1340,1358`

**既有分析笔记(本仓库根目录,可交叉参考,但注意其个别内容已过时)**
- `天数智芯-七个任务入门详解.md`(1065 行,最佳入门读物)
- `天数智芯-三个后端是什么.md`、`天数智芯-七任务关系说明.md`、`天数智芯-七任务验证方法.md`
- `FlagCX厂商适配（v3）-摘要.md`
- ⚠️ `天数智芯-CCL路径三件套作用说明.md` 里描述的 `CommTraits<IluvatarCclBackend>` **在代码中不存在**(实际是 `CommTraits<DefaultBackend<IluvatarPlatform>>`,见 `iluvatar_comm_traits.h:10`),且它说 `iluvatar_platform_traits.h` "需要从零编写"的说法**已过时**(该文件现为 398 行)。**不要把它当作 ground truth。**
- `dsh-fixes/README.md:161` 记录了 `hostGetDevicePointer` NULL 导致的 exit=139 崩溃(与 S5 吻合)

---

## 附录 C:关于本次分析的中间产物

为抽取 PDF 文本,我在工作区内创建了临时目录 `.dsh-pdf/`:

| 文件 | 说明 |
|---|---|
| `.dsh-pdf/pdf-text.txt` | **PDF 的完整文本抽取结果(29 页)**。建议保留 —— 你可以直接 grep 节号,比翻 PDF 快 |
| `.dsh-pdf/extract.mjs` | 抽取脚本(pdfjs) |
| `.dsh-pdf/node_modules/`、`.dsh-pdf/npm-cache/` | 依赖与缓存,**可安全删除**(约数十 MB)。删除后若要重跑抽取,需先 `npm install pdfjs-dist --cache ./.dsh-pdf/npm-cache` |

这些都不属于 FlagCX 仓库内容,`.gitignore` 未覆盖,提交前请注意排除。

---

## 下一步建议(按顺序)

> 6 条决策已在 §8 拍定(D1–D6),**不再需要等提交人**。以下是可直接开工的顺序。

1. **P1:上 Linux 把构建打通**(S4)。**在构建打通之前,任何代码改动都无法验证** —— 这是唯一真正的硬前置。
2. **P2:一次做完三处代码改动** —— 它们都在 `ixcuda_adaptor.cc`,互不冲突,一起改一起验:
   - **D3**:`hostGetDevicePointer` 真实现 + `deviceMalloc(flagcxMemHost)` 改 `cudaHostAllocMapped`(**先跑 20 行探针**,10 分钟定论;失败则走 §8 D3 的四步回退)
   - **D2**:`getLastError` 4 行实现
   - **D1**:`launchKernel` 真实现(⚠️ block/grid 顺序 + `stream->base` + 不用 `DEVCHECK`)+ 两个死 kernel 补 launcher
   - **D1 的验收硬要求**(Jira 原文):必须证明 **`device_api.cu` 与 `device_ir.cu` 编出的真实 kernel 能被启动**,且**启动失败时返回真实错误**。→ 用两条桥接 + 黄金对照 + 4 类负例,详见 §5.1「验收方法」(A1–A3 / B1–B5)。**不要用自造小 kernel 替代真实 kernel。**
3. **P3:先跑上游新增的 `GetPointerType` 单测(N1,高优先),再跑 S6 的 15 个 IPC 用例**(记住 `MPI_NP=2`,不是默认的 8)。
   - **N1 是本次上游变更中最可能出现新失败的单一项**:`GetPointerType` 对天数**从"跳过"变成"真跑"**,且断言 `ptr + 64` 的 interior pointer 分类(`test_device_adaptor.cpp:209-275`)。
   ```bash
   cd test/unittest/adaptor && make && ./build/bin/adaptor_unit_tests \
       --gtest_filter='DeviceAdaptorTest.GetPointerType'
   ```
   - 然后跑 IPC 15 用例,重点看 `CrossGpuLargeGdrSubrangesWriteAsync`。
   - 顺带确认 `GetAddressRangeForInteriorGdrPointer` 与 `HostRegisterUnregister` 是 **SKIP 而非 FAIL**(天数的桩会触发 SKIP,见 §0.4 结论 ②)。
4. **P4:先写 `__syncwarp()` 微基准,再碰 `iluvatar_platform_traits.h`。**
   - **绝对不要**为了"消除 trap"而扩宽掩码 —— `:67-68` 的注释自己警告会死锁。
   - **但 acquire fence 必须补**(【文档 §5.2】点名"难以复现的跨 rank hang")。
   - 若微基准显示 `__syncwarp()` 是**掩码 barrier**,则反弹为利好:可支持部分掩码,PlatformTraits 有机会从"部分支持"升级。
5. **P5:写 D6 的 rank wrapper**(变量名已全部确认:`CUDA_VISIBLE_DEVICES` / `FLAGCX_HOSTID` / `FLAGCX_IB_HCA`)。
6. **P6:按第 6 节跑 T1–T4 ×3 次,按第 7 节出交付物。** D5 原则:**目标不打折,仅在拿到实测证据后走正式豁免**。
