# FlagCX host API `send/recv` 挂死修复报告

> 目标问题：group 内对**同一 peer·同一方向**提交 ≥2 个 op 时挂死（你矩阵里标记为 "suspected shape" 的那类）。
> `remote2`（2 send + 2 recv，size 100/200，发/收顺序交叉）是最后一个被修好的用例。

| | 修复前 | 修复后 |
|---|---|---|
| p2p pattern 矩阵 | 4/9 通过（`ring`,`self1`,`self3`,`self4` 是**假通过**） | **7/7 通过** |
| 完整 perf sweep | PASS=13 FAIL=2 | **PASS=13 FAIL=2（逐项一致）** |
| 吞吐 | allgather 30.471 / allreduce 15.854 GB/s | 30.468 / 15.851 GB/s（**无可测差异**） |

**改动范围**：4 个核心文件（`flagcx/core/{proxy,p2p,group,launch_kernel}.cc`）+ 1 个测试文件，共 5 处独立修复。

---

## 1. 环境与复现

环境：单节点 Iluvatar Corex 5.1.0（MR-V50 ×2 + MR-V100 ×1，另有 1× RTX 2060），容器内路径 `/usr/local/corex-5.1.0/FlagCX`，`np=2` 测试。

```bash
# 构建
cd /usr/local/corex-5.1.0/FlagCX
make USE_ILUVATAR=1 MPI_HOME=/usr/local/openmpi -j$(nproc)
make -C test/perf/host_api USE_ILUVATAR=1 MPI_HOME=/usr/local/openmpi -j$(nproc)

# 单个 pattern（perf_self_sendrecv 由 test/perf/host_api/test_self_sendrecv.cpp 构建）
cd test/perf/host_api
export PATH=/usr/local/openmpi/bin:$PATH
export LD_LIBRARY_PATH=$PWD/../../build/lib:/usr/local/corex/lib64:$LD_LIBRARY_PATH
PATTERN=remote2 timeout 40 mpirun --allow-run-as-root -np 2 -x PATTERN \
  -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_MEM_ENABLE=1 -x FLAGCX_VMM_ENABLE=0 \
  -x CUDA_VISIBLE_DEVICES=1,2 ./build/bin/perf_self_sendrecv \
  -b 8 -e 64K -f 2 -w 1 -n 2 -R 1
```

**尺寸**：`SelfBufs` 里 `n1 = 100`、`n2 = 200` 字节，供各 pattern 组合。

---

## 2. 修复前的完整现象

| pattern | 结构 | 修复前 |
|---|---|---|
| `ring` | 1S+1R → next | ✅ |
| `self1` | 1S+1R → self | ✅ |
| `self2` | 2S(100,200)+2R(200,100) → self | ❌ HANG |
| `self3` | 2S+1R → self | ⚠️ 通过但是**假通过**（见 §6） |
| `self4` | 1S+2R → self | ⚠️ 同上 |
| `remote2` | 2S+2R → next | ❌ HANG |
| `remote3` | 2S+1R → next | ❌ HANG（非法：消息数不配对） |
| `remote4` | 1S+2R → next | ❌ HANG（非法） |
| `mixed` | ring pair + 2 self pair | ❌ HANG |
| `remote2same` / `self2same` | 2S+2R **同 size**（本次新增） | ❌ HANG |

---

## 3. 五个缺陷

### 缺陷 1 — 退役判据是「组级」的（`proxy.cc` ×4）

```c
// 每轮每队列只取队头
struct flagcxProxyOp *op = flagcxIntruQueueHead(queue);
flagcxProxyXxx(resources, ...);
if (op->args.done == 1 && op->args.semaphore->pollEnd()) {  // ← 退役条件
    op->args.semaphore.reset(); flagcxIntruQueueDelete(queue, op); free(op);
}
```

`flagcxHostSemaphore::pollEnd()` 返回 `counter == 0`，而 `counter` **只在 `wait()` 结尾归零**，`wait()` 又要求**组内每个 op 都已 `subCounter`**。于是形成环形等待：

> 队头即使 `done == 1`，只要同组还有任何 op 未完成 → `pollEnd()` 为假 → 队头不退役 → 队头堵着 → 第 2 个 op 永不执行 → 它永不会 `subCounter` → `wait()` 永不返回。

**判据：任意 (peer, 方向) 队列内 op 数 ≥ 2 → 必挂。** 10/10 数据点吻合。

**证据**：gdb 抓到两个 rank 的主线程停在 `ixcudaAdaptorStreamSynchronize`，`cpuAsyncKernel` 线程停在 `flagcxHostSemaphore::wait`（`launch_kernel.h:117` 的 `sched_yield()` 自旋），而 proxy 进度线程仍在 `progressOps` 里活跃空转（`proxy.cc:314`）。

**修复**：改为按 op 自身完成退役 —— `if (op->args.done == 1)`。semaphore 的生命周期交给 `shared_ptr` 引用计数（`proxy.cc:359/370` 的原注释 "update refcount and delete semaphore when refcount = 0" 正是这个本意）。

### 缺陷 2 — semaphore 生命周期（`group.cc` + `launch_kernel.cc`）

`op->args.semaphore` 是 `shared_ptr`，而 **op 是唯一持有者**（`groupLaunch` 里的局部 `shared_ptr` 在函数返回时销毁，`launchHostFunc` 只拿到裸指针 `semaphore.get()`）。缺陷 1 修好后 op 立即释放引用 → 引用计数归零 → **semaphore（及其全部 event）在 `cpuAsyncKernel::wait()` 仍在执行时被析构** → use-after-free。

> 这是**第一版仅修缺陷 1 会把 `ring`（原本通过的用例）弄挂**的原因。

**修复**：把一份 `shared_ptr` 的所有权移交给 host 回调（`new std::shared_ptr<flagcxSemaphore>(semaphore)` → `cpuAsyncKernel` 用 `unique_ptr` 接管），保证 semaphore 活到 `wait()` 返回。

### 缺陷 3 — 每轮只推队头（`proxy.cc`）

即使缺陷 1 修好，proxy 仍每轮每队列只推进队头。当两个 rank 的 op 顺序不一致时（例如一个先发 100 后发 200、另一个先收 200 后收 100），两边的队头互相等待对方队列**非队头**的 op → 跨 rank 队头互锁。

**修复**：`while (op != NULL)` 遍历整个队列（先存 `nextOp` 再删除/释放）。

### 缺陷 4 — P2P 同步槽位分配退化（`p2p.cc`）

```c
*opHash  = key + opHashCounter;
*slotIdx = (*opHash) % (FLAGCX_P2P_MAX_OPS / 2);   // MAX_OPS/2 = 16
```

`makeKey()` 把每个字段都左移 ≥ 4 位（`reservedBits = 4`）→ **任何 key 的低 4 位恒为 0 → `key % 16 == 0` 恒成立** → `slotIdx` 实际只等于 `opHashCounter % 16`，而 `p2pOpHashMap` 里**每个新 key 的计数器都从 1 开始** → 所有「新 (rank, peer, size, dtype) 组合」的首次同向 op **全部落在槽 1（send）/ 17（recv）**。

槽位是共享 shm 里的会合点（`flagcxShmAllocateShareableBuffer`，双方 map 同一段内存），冲突的 op 会互相覆盖 `opHash`，对端匹配不上 → proxy 不再 `subCounter`。4 个 op 时形成 4 环等待：
`Send(100)[槽1] → Recv(100)[槽17] → Recv(200)[槽17] → Send(200)[槽1] → 回到起点`。

> 注意：**槽位/opHash 没有任何跨机传输**（只在 `p2p.cc` 内本进程使用），两端各自用镜像 key 推导，所以修复必须保持「纯函数、与 op 顺序无关」。

**修复**：新增 `mixKey()`（murmur 风格 finalizer），`*slotIdx = (mixKey(key) + opHashCounter) % 16`。实测 100/200 四组槽位 `14/6`、`30/22`、`10/0`、`26/16` 全不冲突，16 个槽桶分布均匀。

### 缺陷 5 — `step` 跟随本地顺序，而匹配按 (bytes, dtype)（`group.cc`）

```c
op->args.opId = ... : (p2pScheduleDisable ? defaultOpId : -roundOpId);   // 同方向全部重复
op->args.step = ... : (p2pScheduleDisable ? defaultStep : roundRecvStep); // 全局按方向递增
```

`pollStart(opId, step)` 返回 `curStep >= step`，于是共享同一 `opId` 的 op 被 step **串成流水线**。而 `step` 来自**本地 op 顺序**，op 却按 **(bytes, dtype)** 匹配。`remote2` 的 size 顺序是交叉的：

- rank0：`Send(100)`=step0、`Send(200)`=step1；`Recv(200)`=step0、`Recv(100)`=step1
- 配对关系：rank0 的 `Send(100)` ↔ rank1 的 `Recv(100)`，而后者是 **step1**

→ 四环等待：`r0.Send(100)[s0] → r1.Recv(100)[s1] → r1.Recv(200)[s0] → r0.Send(200)[s1] → r0.Send(100)[s0]`

**证据**：`FLAGCX_P2P_SCHEDULE_DISABLE=1`（使 `opId` 唯一、`step` 全 0，消除流水线）后 `remote2` **通过**；`remote2same`（同 size）因 step 天然对齐本就通过。

**修复**：`step` 改为按 **(bytes, dtype) 分键**的计数器（`sendStepByKey` / `recvStepByKey`）：
- 两端由同一个键推导 → **天然一致**，与 op 到达顺序无关；
- 对集合通信（每轮 slice size 相同）→ per-key 计数 `0,1,2…` 与原来的全局计数**完全等价**，行为不变；
- 对用户级 p2p 不同 size → 每个键都是 step 0 → 无跨 op 排序 → 环消失。

---

## 4. 判决性实验（每个结论的来源）

| 结论 | 实验 | 结果 |
|---|---|---|
| 缺陷 1 是主因 | gdb 抓挂死现场 | `wait()` 自旋 + proxy 空转 + 主线程等流，三者同时成立 |
| 缺陷 2 存在 | 只应用缺陷 1 的修复重跑 | `ring`/`self1` **从通过变成挂死**；补上所有权移交后恢复 |
| 缺陷 4 存在且可达 | 新增 `remote2same`（同 size）对比 `remote2`（不同 size） | `remote2same` 通过 / `remote2` 挂 → 且 mix 后槽位确实不冲突；两个 pattern 是一对**互相排除**的判据 |
| 缺陷 5 存在 | `remote2 + FLAGCX_P2P_SCHEDULE_DISABLE=1` | **exit=0 通过**（已确认作业真的启动：日志有 `pattern: remote2`） |
| 缺陷 5 的修法安全 | 关掉流水线后跑 10 个集合通信 | 10/10 通过 → 流水线非必需，可按 key 改造 |
| 改动无回归 | 完整 sweep 对比基线 | PASS=13 FAIL=2 逐项一致，吞吐无差异 |
| `perf_allreduce` segv **非本次引入** | 用 `git checkout` 恢复 4 个核心文件重建后复测 | pristine 同样 segv（且只在该用例带 `FLAGCX_USE_HETERO_COMM=1` 时出现） |

---

## 5. 验证结果

**A) p2p pattern 矩阵 —— 7/7 通过**

```
ring ✅  self1 ✅  self2 ✅  remote2 ✅  remote2same ✅  self2same ✅  mixed ✅
```

**B) 完整 perf sweep（`test/script/perf_host_api_2card.sh 2`，尺寸到 4G，20 iters）**

| | 基线 `20260922-135805` | 修复后 `20260924-152640` |
|---|---|---|
| 结果 | PASS=13 FAIL=2 | **PASS=13 FAIL=2** |
| FAIL 项 | `p2p_engine`, `one_side_register` | **相同** |
| allgather 4G | 30.471 GB/s | 30.468 GB/s |
| allreduce 4G | 15.854 GB/s | 15.851 GB/s |
| alltoall 4G | 31.871 GB/s | 31.872 GB/s |
| sendrecv 4G | 15.939 GB/s | 15.939 GB/s |

（`p2p_engine` / `one_side_register` 的 FAIL 与修复无关，你的 sweep 脚本注释里也记了 RMA 的限制。）

---

## 6. 遗留问题（都不影响上面的结论）

1. **`self3`/`self4` 是假通过**。`group.cc` Round-0 self 配对只按 (bytes, dtype) 配对，**配不上的任务被塞回 `peers[comm->rank]` 队列（`group.cc:233–236`），而 Round 1+ 只遍历 `p2pSchedule` 的对端（永不含 self）→ 这些 op 被静默丢弃**，数据根本没传却因为「无异常」被判通过。建议显式报错。
2. **`self3/self4/remote3/remote4` 这类消息数不配对的非法 pattern 仍会挂**，而非报错。建议显式校验并返回错误，否则测试矩阵给不出可信信号。
3. **`proxyInfo.events[]` / `args.subs[]` 按「每 op 从 0 起算」的计数器索引** —— 同一连接上并发的多个 op 会复用同一个 event 槽。缺陷 3 让并发变多后更接近可达；本次 sweep 未暴露异常，但值得单独收口（同类「索引空间错误」问题）。
4. **`p2pOpHashMap` 计数器永不重置、map 无限增长** → 槽位分配依赖历史调用次数。
5. **`perf_allreduce` + `FLAGCX_USE_HETERO_COMM=1` 崩溃**（exit=139）：已定位为 **ixcuda 适配器 `hostGetDevicePointer` 为 NULL 却被无条件调用**（`ixcuda_adaptor.cc:387` 显式 `NULL`；`uni_runner_impl.cc:1816` 调用点无检查）。注意这与你的 sweep 无关（sweep 的集合通信不带该 env，一直 PASS）。`hip` 适配器有同样问题。
6. 上游 `511cdac` 遗留的 `work->isBarrierOp_ = true`（注释写着 "hanging issue"）—— 本次修复后可能可以去掉，值得复测。

---

## 7. 应用与回滚

```bash
# 应用核心修复（在仓库根目录；对 3dcea55 干净树校验通过 git apply --check）
git apply dsh-fixes/core-fix.patch

# 测试 pattern（test_self_sendrecv.cpp 为未跟踪文件，需已存在于工作区）
git apply dsh-fixes/test-patterns.patch

# 一键回滚
git checkout -- flagcx/core/proxy.cc flagcx/core/p2p.cc \
                flagcx/core/group.cc flagcx/core/launch_kernel.cc
```

远端容器内的等价副本与备份：

| 内容 | 路径 |
|---|---|
| 核心 patch | `/tmp/fix-core.patch` |
| 测试 patch | `/tmp/fix-test.patch` |
| 分步补丁脚本 | `/tmp/patch_proxy.py`、`/tmp/patch_lifetime.py`、`/tmp/patch_slot.py`、`/tmp/patch_qw4.py`、`/tmp/patch_stepkeys2.py` |
| 原始备份 | `/tmp/proxy.cc.bak-20260924-142055`、`/tmp/p2p.cc.bak-*`、`/tmp/group.cc.bak-*`、`/tmp/launch_kernel.cc.bak-*`、`/tmp/test_self_sendrecv.cpp.bak-20260924-141350` |
| 证据日志 | `/tmp/dsh-*.log`、`/tmp/f2-*.log`、`/tmp/f2c-*.log`、`/tmp/base-*.log` |

## 8. 建议的 commit 拆分

5 处修复彼此独立，建议分开提交以便二分与上游 review：

1. `test(perf): add same-size 2-send/2-recv patterns for p2p hang triage`
2. `fix(proxy): retire p2p ops per-op and keep the semaphore alive`（缺陷 1 + 2）
3. `fix(proxy): advance the whole p2p queue, not just the head`（缺陷 3）
4. `fix(p2p): mix the op key before deriving the sync slot index`（缺陷 4）
5. `fix(group): key the p2p step by (bytes, dtype) so both ranks agree`（缺陷 5）
