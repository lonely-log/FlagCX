# FlagCX host API `send/recv` 挂死：分析与修复报告

> **基线**：上游 `main` = `250ee84`（2026-10-04，`[CRL] Unify remote visibility flush semantics (#639)`）
> **结论先行**：这个 hang 在**最新上游上依然存在**；本报告给出根因、判决性实验、修复（分支 `fix/p2p-group-deadlock-v2`）与完整验证。

---

## 0. 摘要

| 项 | 内容 |
|---|---|
| 问题 | group 内对**同一 peer、同一方向**提交 ≥2 个 op 时挂死（`flagcxSend`/`flagcxRecv`） |
| 上游是否已修 | **没有**。`3dcea55 → 250ee84` 共 18 个新 commit、其中 8 个碰过相关文件，但 5 个缺陷点逐一仍在；且**没有任何 commit message 提及 deadlock/hang** |
| 修复 | `fix/p2p-group-deadlock-v2`（基于 `250ee84`，2 个 commit，4 文件 +86/−34） |
| 验证 | 7/7 p2p pattern 通过；`perf_core_sendrecv`（128K→4G）通过；完整 perf sweep **PASS=13 FAIL=2 与基线逐项一致**、4G 吞吐无差异 |
| 环境 | 单节点 Iluvatar Corex 5.1.0（MR-V50 ×2 + MR-V100 ×1），容器内路径 `/usr/local/corex-5.1.0/FlagCX`，`np=2` |

---

## 1. 现象与复现

**触发条件**：某一 `(peer, 方向)` 的 proxy 队列里同一轮放下了 **≥2 个 op**。
典型形态是「2 send + 2 recv 到同一 peer」——包括 size 相同与不同、包括 self 与 remote。

**挂死现场**（gdb）：
- 主线程停在 `ixcudaAdaptorStreamSynchronize`
- `cpuAsyncKernel` 线程停在 `flagcxHostSemaphore::wait()`（`launch_kernel.h:117` 的 `sched_yield()` 自旋）
- proxy 进度线程**仍在活跃空转**（`progressOps` 循环里）

三者同时成立 → 不是"等锁"，而是**环形等待**。

**复现命令**（`test/perf/host_api/test_self_sendrecv.cpp`，本仓库该文件由本次工作新增）：

```bash
cd test/perf/host_api
export PATH=/usr/local/openmpi/bin:$PATH
export LD_LIBRARY_PATH=$PWD/../../build/lib:/usr/local/corex/lib64:$LD_LIBRARY_PATH
PATTERN=remote2 timeout 40 mpirun --allow-run-as-root -np 2 -x PATTERN \
  -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_MEM_ENABLE=1 -x FLAGCX_VMM_ENABLE=0 \
  -x CUDA_VISIBLE_DEVICES=1,2 ./build/bin/perf_self_sendrecv \
  -b 8 -e 64K -f 2 -w 1 -n 2 -R 1
```

**pattern 矩阵在 `250ee84` 上的结果**：

| pattern | 结构（S=send, R=recv） | 结果 |
|---|---|---|
| `ring` | 1S+1R → next | ✅ |
| `self1` | 1S+1R → self | ✅ |
| `self2` | 2S(100,200) + 2R(200,100) → self | ❌ HANG |
| `remote2` | 2S + 2R → next，size 交叉 | ❌ HANG |
| `remote2same` | 2S + 2R → next，**同 size** | ❌ HANG |
| `self2same` | 2S + 2R → self，**同 size** | ❌ HANG |
| `mixed` | ring pair + 2 self pair | ❌ HANG |
| `self3` / `self4` | 2S+1R / 1S+2R → self | ⚠️"通过"但是**假通过**（见 §7.1） |
| `remote3` / `remote4` | 2S+1R / 1S+2R → next | ❌ HANG（消息数不配对的非法程序） |

---

## 2. 上游最新版是否修了？—— 逐点核对：没有

`3dcea55 → 250ee84` 的 18 个新 commit 中，有 8 个动过我们关注的文件（`ded1f98`、`3c2679a`、`06f83f6`、`e72b3fd`、`c93438d`、`b73752d`、`e808b43`、`d8b9a63`、`648a6c4`）。它们在 `250ee84` 上的现状：

| 缺陷 | `250ee84` 上的位置 | 现状 |
|---|---|---|
| ① 组级 `pollEnd()` 退役门控 | `flagcx/core/proxy.cc:545-548`（send）、`575-581`（recv） | **原样存在** |
| ③ 每轮只推队头 | `flagcx/core/proxy.cc:524-526`（send）、`554-556`（recv） | **原样存在** |
| ④ 槽位 `opHash % 16` 退化 | `flagcx/core/p2p.cc:108` | **原样存在**（无 `mixKey`） |
| ⑤ `step` 用全局 `roundSendStep/roundRecvStep` | `flagcx/core/group.cc:270-278`、`310-315` | **原样存在**（无按 key 分键） |
| ② semaphore 生命周期 | `flagcx/core/launch_kernel.cc` / `.h` | **一行未改**（`cpuAsyncKernel` 仍只收裸指针） |

**容易误判的一点**：`proxy.cc:487` 有 `while (!flagcxIntruQueueEmpty(queue)) { … }`，看着像"遍历整队列"，但那是**失败清理**助手（循环 `flagcxProxyRetireFailedOp`）。`progressOps` 本体仍只取队头。

**上游这几个 commit 实际在做什么**（与我们的 hang 无关的方向）：

- `e72b3fd`/`c93438d`/`06f83f6`/`3c2679a`/`ded1f98`：transport 抽象层重构（op 进度改为 `op->connection->tcomm->progressProxyOp(...)` 分派）、多通道 `channelId` 贯通、失败队列统一回收、错误传播到 `rmaProxy->rmaError`
- `e808b43`/`250ee84`：RMA GET 数据可见性与 flush 语义
- `d8b9a63`/`648a6c4`/`8b80740`/`ce52733`：SHCA verbs ABI、CI、打包

> 注意：`c93438d` 给 Round-0 的 self 配对条件**加了 `channelId`**，并把硬编码的 `op->channelId = 0` 换成真实通道号 —— 这是多通道正确性修复，不是我们的死锁。

---

## 3. 五个独立缺陷

### ① op 退役判据是"组级"的（`proxy.cc`）

```c
// 每轮每队列只取队头
struct flagcxProxyOp *op = flagcxIntruQueueHead(queue);
...progressProxyOp...
if (op != NULL && op->args.done == 1 && op->args.semaphore->pollEnd()) {  // ← 门控
  op->args.semaphore.reset();
  flagcxIntruQueueDelete(queue, op);
  free(op);
}
```

`flagcxHostSemaphore::pollEnd()` 返回 `counter == 0`，而 `counter` **只在 `wait()` 结尾归零**，`wait()` 又要求**组内每个 op 都已 `subCounter`**：

> 队头即使 `done == 1`，只要同组还有任何 op 未完成 → `pollEnd()` 为假 → 队头不退役 → 第 2 个 op 永不被推进 → 它永不 `subCounter` → `wait()` 永不返回。

**判据：任一 `(peer, 方向)` 队列内 op 数 ≥ 2 → 必挂。**

### ② semaphore 生命周期（`group.cc` + `launch_kernel.cc`）

`op->args.semaphore` 是 `shared_ptr`，而 **op 是唯一持有者**：`groupLaunch` 的局部 `shared_ptr` 在函数返回时销毁，`launchHostFunc` 只拿到裸指针 `semaphore.get()`。
只做 ① 的修复（op 完成即 `reset()`）会让 semaphore 及其 event 在 `cpuAsyncKernel::wait()` 仍在执行时被析构 → **use-after-free**。

> 这正是"只修 ① 会把 `ring`（原本通过的用例）弄挂"的原因。

### ③ 每轮只推队头（`proxy.cc`）

即使 ① 修好，proxy 仍每轮每队列只推进队头。当两个 rank 的 op 顺序不一致（如一个先发 100 后发 200、另一个先收 200 后收 100），两边队头互相等待对方队列**非队头**的 op → 跨 rank 队头互锁。

### ④ P2P 同步槽位分配退化（`p2p.cc:108`）

```c
*opHash  = key + opHashCounter;
*slotIdx = (*opHash) % (FLAGCX_P2P_MAX_OPS / 2);   // MAX_OPS/2 = 16
```

`makeKey()` 把每个字段都左移 ≥ 4 位（`reservedBits = 4`）→ **任何 key 的低 4 位恒为 0 → `key % 16 == 0` 恒成立** → `slotIdx` 实际只等于 `opHashCounter % 16`；而 `p2pOpHashMap` 里**每个新 key 的计数器都从 1 开始** → 所有"新 (rank, peer, size, dtype) 组合"的首次同向 op **全部落在槽 1（send）/ 17（recv）**。

槽位是共享 shm 里的会合点（双方 map 同一段内存），冲突的 op 互相覆盖 `opHash`，对端匹配不上 → 永不再 `subCounter`。4 个 op 时形成 4 环：
`Send(100)[槽1] → Recv(100)[槽17] → Recv(200)[槽17] → Send(200)[槽1] → 回到起点`。

> 约束：槽位/opHash **没有任何跨机传输**（只在 `p2p.cc` 内本进程使用），两端各自用镜像 key 推导 → 修复必须保持"纯函数、与 op 到达顺序无关"。

### ⑤ `step` 跟随本地顺序，而匹配按 `(bytes, dtype)`（`group.cc`）

```c
op->args.opId = ... : (p2pScheduleDisable ? defaultOpId : -roundOpId);   // 同方向全部重复
op->args.step = ... : (p2pScheduleDisable ? defaultStep : roundRecvStep); // 全局按方向递增
```

`pollStart(opId, step)` 要求 `curStep >= step` → 共享同一 `opId` 的 op 被 step **串成流水线**；而 `step` 来自**本地 op 顺序**，op 却按 **(bytes, dtype)** 匹配。size 交叉时（发 100→200 / 收 200→100）匹配对拿到不同 step：

```
r0.Send(100)[s0] → r1.Recv(100)[s1] → r1.Recv(200)[s0] → r0.Send(200)[s1] → 回到起点
```

---

## 4. 修复（`fix/p2p-group-deadlock-v2`，基于 `250ee84`）

```
f1f7690 test(perf): add self2same/remote2same patterns for p2p hang triage
9a7aceb fix(core): resolve host API p2p group send/recv deadlocks (ported onto 250ee84)
250ee84 [CRL] Unify remote visibility flush semantics (#639)

 flagcx/core/group.cc                      |  24 ++++-
 flagcx/core/launch_kernel.cc              |   6 +-
 flagcx/core/p2p.cc                        |  20 +++-
 flagcx/core/proxy.cc                      |  70 ++++++-----
 test/perf/host_api/test_self_sendrecv.cpp | 145 ++++++++++++++
 5 files changed, 231 insertions(+), 34 deletions(-)
```

| # | 位置 | 改法 |
|---|---|---|
| ① | `proxy.cc` `progressOps` 两处退役判据 | 去掉 `&& op->args.semaphore->pollEnd()`，改为**本 op 完成即退役** |
| ② | `group.cc` `launchHostFunc` 调用点<br>`launch_kernel.cc` `cpuAsyncKernel` | 用 `new std::shared_ptr<flagcxSemaphore>(semaphore)` 把一份引用**移交给 host 回调**，回调用 `unique_ptr` 接管，保证 semaphore 活到 `wait()` 返回 |
| ③ | `proxy.cc` `progressOps` 两个队列块 | 由"只取队头"改为 **`while (op != NULL)` 遍历整队列**（先存 `nextOp` 再删除）；失败路径（`RetireFailedQueue`/`FailProgressQueue` 会清空整队列）后立刻 `break`，避免用到已释放的 `nextOp` |
| ④ | `p2p.cc` | 新增 `mixKey()`（murmur 风格 finalizer），`*slotIdx = (mixKey(key) + opHashCounter) % 16` |
| ⑤ | `group.cc` | `step` 改为按 **(bytes, dtype) 分键**计数（`sendStepByKey` / `recvStepByKey`），两端天然一致；对每轮 slice size 相同的集合通信与原逻辑**等价** |

**移植说明**：④⑤② 三处的代码在 `250ee84` 上位置未变，补丁原样复用；①③ 因 `progressOps` 改成经 `tcomm->progressProxyOp` 分派而**需要重写**（语义相同，落点不同）。

---

## 5. 验证（全部在移植版上实测）

**A) p2p pattern 矩阵 —— 7/7 通过**

```
ring ✅  self1 ✅  self2 ✅  remote2 ✅  remote2same ✅  self2same ✅  mixed ✅
```

**B) `perf_core_sendrecv`（你的 sweep 脚本里原本被注释掉的那项）**

`128K → 4G`、`w5 n20`：**exit=0，16 个尺寸全部出结果**。

**C) 完整 `test/script/perf_host_api_2card.sh 2`**

| | 修复前基线 | 移植后 |
|---|---|---|
| 结果 | PASS=13 **FAIL=2** | **PASS=13 FAIL=2（逐项一致）** |
| FAIL 项 | `p2p_engine`、`one_side_register` | **相同**（既有问题） |
| allgather 4G | 30.471 GB/s | 30.469 GB/s |
| allreduce 4G | 15.854 GB/s | 15.857 GB/s |
| alltoall 4G | 31.871 GB/s | 31.871 GB/s |
| sendrecv 4G | 15.939 GB/s | 15.938 GB/s |
| reduce / broadcast / gather / scatter / reducescatter 4G | — | 均与基线一致 |

> 附带：`ipc_sendrecv` 4G 从基线 `0.3379s / 12.71 GB/s` 变为 `0.0770s / 55.79 GB/s` —— 这是**上游 transport 重构**带来的提速，与本次修复无关。

---

## 6. 判决性实验（每个结论的来源）

| 结论 | 实验 | 结果 |
|---|---|---|
| ① 是主因 | gdb 抓挂死现场 | `wait()` 自旋 + proxy 空转 + 主线程等流，三者同时成立 |
| ② 存在 | 只应用 ① 的修复重跑 | `ring`/`self1` **由通过变挂死**；补上所有权移交后恢复 |
| ④ 存在且可达 | 新增 `remote2same`（同 size）对比 `remote2`（不同 size） | `remote2same` 通过 / `remote2` 挂；且 mix 后槽位实测 `14/6`、`30/22`、`10/0`、`26/16` 四组均不冲突 |
| ⑤ 存在 | `remote2` + `FLAGCX_P2P_SCHEDULE_DISABLE=1`（令 opId 唯一、step 全 0） | **exit=0 通过**（已确认作业真启动：日志含 `pattern: remote2`） |
| ⑤ 的修法安全 | 关掉流水线后跑 10 个集合通信 | 10/10 通过 → 流水线非必需、可按 key 改造 |
| 上游未修 | 在 `250ee84` 上跑同一矩阵 | 7 项里 5 项复现挂死 |
| 无回归 | 完整 sweep 对比基线 | PASS/FAIL 逐项一致、吞吐无差异 |
| `perf_allreduce` segv **非本次引入** | `git checkout` 恢复 4 个核心文件重建后复测 | pristine 上同样 segv（且只在该用例带 `FLAGCX_USE_HETERO_COMM=1` 时出现） |

---

## 7. 遗留问题（都不影响上面的结论）

1. **`self3`/`self4` 是"假通过"**：Round-0 self 配对只按 `(bytes, dtype)`（`250ee84` 上还加了 `channelId`）配对，**配不上的任务被塞回 `peers[comm->rank]` 队列，而 Round 1+ 只遍历 `p2pSchedule` 的对端（永不含 self）→ 这些 op 被静默丢弃**，数据实际未传输。
2. **消息数不配对的非法 pattern（`self3`/`self4`/`remote3`/`remote4`）会挂死而非报错**，建议显式校验并返回错误，否则测试矩阵给不出可信信号。
3. **`proxyInfo.events[]` / `args.subs[]` 按"每 op 从 0 起算"的计数器索引** —— 同一连接上并发的多个 op 会复用同一 event 槽。缺陷 ③ 的修复让并发变多后更接近可达（本次 sweep 未暴露异常，但建议单独收口）。
4. **`p2pOpHashMap` 计数器永不重置、map 无限增长** → 槽位分配依赖历史调用次数。
5. **`perf_allreduce` + `FLAGCX_USE_HETERO_COMM=1` 崩溃（exit=139）**：已定位为 **ixcuda 适配器 `hostGetDevicePointer` 为 NULL 却被无条件调用**（`ixcuda_adaptor.cc:387` 显式 `NULL`；`uni_runner_impl.cc:1816` 调用点无检查）。`hip` 适配器有同样问题。注意这与常规 sweep 无关（sweep 的集合通信不带该 env）。上游新 CI 脚本也把 heterogeneous perf 的集合通信限定为 alltoall/alltoallv/sendrecv/allgather/broadcast/gather/scatter，理由写着 *"uniRunner does not implement reduction collectives"*。
6. 上游 `511cdac` 遗留的 `work->isBarrierOp_ = true`（注释写着 "hanging issue"）—— 本次修复后可能可以去掉，值得复测。

---

## 8. 应用 / 回滚 / 产物

```bash
# 应用（在 250ee84 上）
git checkout -b fix/p2p-group-deadlock-v2 250ee84
git apply <fix-core-v2.patch>          # 4 个核心文件
git apply <fix-test-v2.patch>          # 新增 test_self_sendrecv.cpp

# 回滚
git checkout -- flagcx/core/proxy.cc flagcx/core/p2p.cc \
                flagcx/core/group.cc flagcx/core/launch_kernel.cc
```

- 分支：`fix/p2p-group-deadlock-v2`（基于 `250ee84`）
- 补丁：`fix-core-v2.patch`（10.7KB）、`fix-test-v2.patch`（6.8KB）、`test_self_sendrecv-v2.cpp`
- 构建：`make USE_ILUVATAR=1 MPI_HOME=/usr/local/openmpi -j$(nproc)`，再 `make -C test/perf/host_api USE_ILUVATAR=1 MPI_HOME=/usr/local/openmpi -j$(nproc)`

### 建议的 commit 拆分（便于二分与上游 review）

1. `fix(proxy): retire p2p ops per-op and keep the semaphore alive`（缺陷 ①+②）
2. `fix(proxy): advance the whole p2p queue, not just the head`（缺陷 ③）
3. `fix(p2p): mix the op key before deriving the sync slot index`（缺陷 ④）
4. `fix(group): key the p2p step by (bytes, dtype) so both ranks agree`（缺陷 ⑤）
5. `test(perf): add same-size p2p patterns for hang triage`
