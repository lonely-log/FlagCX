# FlagCX host API `send/recv` 挂死：分析与最小修复报告

> **基线**：上游 `main` = `250ee84`（2026-10-04，`[CRL] Unify remote visibility flush semantics (#639)`）
> **结论**：该 hang 在最新上游**依然存在**；**最小充分修复 = 3 处改动**（队列遍历 + 槽位散列 + step 分键），不是 5 处。

---

## 0. 摘要

| 项 | 内容 |
|---|---|
| 问题 | group 内对**同一 peer、同一方向**提交 ≥2 个 op 时挂死（`flagcxSend`/`flagcxRecv`） |
| 上游是否已修 | **没有**。`3dcea55 → 250ee84` 的 18 个新 commit 中有 8 个碰过相关文件，但缺陷点逐一仍在，且**没有任何 commit message 提及 deadlock/hang** |
| 最小充分修复 | **3 处**：`proxy.cc`（遍历整队列）、`p2p.cc`（槽位散列）、`group.cc`（step 按 size/dtype 分键） |
| 分支 | `fix/p2p-hang-matrix`（基于 `250ee84`，4 个 commit） |
| 验证 | 7/7 p2p pattern 通过；`perf_core_sendrecv`（128K→4G）通过；完整 perf sweep **PASS=13 FAIL=2 与基线逐项一致**、4G 吞吐无差异 |
| 环境 | 单节点 Iluvatar Corex 5.1.0（MR-V50 ×2 + MR-V100 ×1），`np=2` |

---

## 1. 现象与复现

**触发条件**：某一 `(peer, 方向)` 的 proxy 队列里同一轮放下了 **≥2 个 op**。
最典型形态是「2 send + 2 recv 到同一 peer」（size 相同或不同都算，self 与 remote 都算）。

**挂死现场**（gdb，三处同时成立 → 不是等锁，而是环形等待）：
- 主线程停在 `ixcudaAdaptorStreamSynchronize`
- `cpuAsyncKernel` 线程停在 `flagcxHostSemaphore::wait()`（`launch_kernel.h:117` 自旋）
- proxy 进度线程**仍在 `progressOps` 循环里活跃空转**

**复现命令**（`test/perf/host_api/test_self_sendrecv.cpp`，本工作新增）：

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
| `remote2` | 2S(100,200) + 2R(200,100) → next | ❌ HANG |
| `remote2same` | 2S + 2R → next，**同 size** | ❌ HANG |
| `self2same` | 2S + 2R → self，**同 size** | ❌ HANG |
| `mixed` | ring pair + 2 self pair | ❌ HANG |
| `self3` / `self4` | 2S+1R / 1S+2R → self | ⚠️"通过"但是**假通过**（见 §7.1） |
| `remote3` / `remote4` | 2S+1R / 1S+2R → next | ❌ HANG（消息数不配对的非法程序） |

`remote2` 的关键细节：每个 rank 依次提交 `Send(100) → Send(200) → Recv(200) → Recv(100)` —— **收的顺序与发的顺序相反**，于是本端第 1 个 op 要对上对端第 4 个 op（顺序交叉）。

---

## 2. 上游最新版是否修了？—— 逐点核对：没有

| 缺陷 | `250ee84` 上的位置 | 现状 |
|---|---|---|
| 退役判据用组级 `pollEnd()` | `flagcx/core/proxy.cc:545-548`（send）、`575-581`（recv） | **原样存在** |
| 每轮只推队头 | `flagcx/core/proxy.cc:524-526`（send）、`554-556`（recv） | **原样存在** |
| 槽位 `opHash % 16` 退化 | `flagcx/core/p2p.cc:108` | **原样存在**（无 `mixKey`） |
| `step` 用全局 `roundSendStep/roundRecvStep` | `flagcx/core/group.cc:270-278`、`310-315` | **原样存在** |

**容易误判的一点**：`proxy.cc:487` 有 `while (!flagcxIntruQueueEmpty(queue)) { … }`，看着像"遍历整队列"，但那是**失败清理**助手（循环 `flagcxProxyRetireFailedOp`）。`progressOps` 本体仍只取队头。

**那 8 个 commit 实际在做什么**（与我们的 hang 无关的方向）：transport 抽象层重构（op 进度改为 `op->connection->tcomm->progressProxyOp(...)` 分派）、多通道 `channelId` 贯通、失败队列统一回收、错误传播到 `rmaProxy->rmaError`、RMA GET 可见性/flush、SHCA verbs ABI、CI/打包。其中 `c93438d` 给 Round-0 的 self 配对条件**加了 `channelId`** 并把硬编码的 `op->channelId = 0` 换成真实通道号 —— 多通道正确性，不是我们的死锁。

---

## 3. 三处必要修复

### ③ 遍历整队列（`proxy.cc`）—— 解开环形等待的关键

`progressOps` 每轮只取队头交给 `progressProxyOp`；而退役判据是 `op->args.done == 1 && op->args.semaphore->pollEnd()`，其中 `pollEnd()` 是**组级**判据（`counter == 0`），而 `counter` 只在 `wait()` 结尾归零、`wait()` 又要求**组内每个 op 都已 `subCounter`**：

> 队头即使 `done == 1`，只要同组还有任何 op 未完成 → `pollEnd()` 为假 → 队头不退役 → 第 2 个 op **连 `progressProxyOp` 都不会被调用** → 它永不 `subCounter` → `wait()` 永不返回。

**修法**：把两个队列块改成 `while (op != NULL)` 遍历整个队列（先存 `nextOp` 再删除；失败路径 `RetireFailedQueue`/`FailProgressQueue` 会清空整队列，之后立即 `break` 以免用到已释放的 `nextOp`）。
**为什么这比"摘掉门控"更好**：所有 op 每轮都被推进 → 全部完成 → 全部 `subCounter` → `counter == 0` → **`pollEnd()` 自然成立**，门控不再构成环；而门控保留着，semaphore 只在全组完成后释放，**生命周期本来就安全**（这就是为什么不需要配套的所有权改动，见 §3.4）。

### ④ 槽位散列（`p2p.cc`）—— 只有 ③ 到位后才"可达"

```c
*opHash  = key + opHashCounter;
*slotIdx = (*opHash) % (FLAGCX_P2P_MAX_OPS / 2);   // 16
```

`makeKey()` 把每个字段都左移 ≥ 4 位（`reservedBits = 4`）→ **任何 key 的低 4 位恒为 0 → `key % 16 == 0` 恒成立** → `slotIdx` 实际只等于 `opHashCounter % 16`；而 `p2pOpHashMap` 里**每个新 key 的计数器都从 1 开始** → 所有「新 (rank, peer, size, dtype) 组合」的首次同向 op **全部落在槽 1（send）/ 17（recv）**。槽位是共享 shm 里的会合点，冲突的 op 互相覆盖 `opHash`，对端匹配不上。

**为什么它在上游"潜伏"了这么久**：上游只推队头 → 同一连接上的多个 op 被**串行化** → 始终只有一个 op 在飞 → 槽位用完即释放 → **冲突不会发生**。加上 ③ 之后多个 op 才真正并发推进，此时不同 size 的两个 send 会同时算到槽 1 → 才暴露。（实测「仅 ④」= 与不修完全一样，2/7。）

**修法**：新增 `mixKey()`（murmur 风格 finalizer），`*slotIdx = (mixKey(key) + opHashCounter) % 16`。
**约束**：槽位/opHash **没有任何跨机传输**（只在 `p2p.cc` 内本进程使用），两端各自用镜像 key 推导 → 修复必须保持"纯函数、与 op 到达顺序无关"，`mixKey(key)` 满足这一点。

### ⑤ step 按 size/dtype 分键（`group.cc`）

```c
op->args.opId = ... : (p2pScheduleDisable ? defaultOpId : -roundOpId);   // 同方向全部重复
op->args.step = ... : (p2pScheduleDisable ? defaultStep : roundRecvStep); // 全局按方向递增
```

`pollStart(opId, step)` 要求 `curStep >= step` → 共享同一 `opId` 的 op 被 step **串成流水线**；而 `step` 来自**本地 op 顺序**，op 却按 **(bytes, dtype)** 匹配。`remote2` 的 size 顺序交叉时：

```
r0.Send(100)[s0] → r1.Recv(100)[s1] → r1.Recv(200)[s0] → r0.Send(200)[s1] → 回到起点
```

**修法**：`step` 改为按 **(bytes, dtype)** 分键计数（`sendStepByKey` / `recvStepByKey`），两端由同一个键推导 → 天然一致；对每轮 slice size 相同的集合通信与原全局计数器**完全等价**。

### 3.4 两条"等价但不必需"的路径（①②）

排查过程中还确认了另外两处**也能修好同一类问题**、但属于冗余的改动：

| 编号 | 内容 | 为何不进最小集 |
|---|---|---|
| ① | 去掉退役判据里的 `&& op->args.semaphore->pollEnd()`，改成"本 op 完成即退役" | ③ 已经让 `pollEnd()` 自然成立，① 变冗余；且摘掉门控会改变原有语义 |
| ② | `group.cc` + `launch_kernel.cc`：让 host 回调自己持有一份 semaphore `shared_ptr` | ② 只是 ① 的**配套**（① 会让 op 提前 `reset()` 引用，导致 `wait()` 期间 UAF）。不做 ① 就不需要 ②，因此最小集**完全不碰 `launch_kernel.{h,cc}`** |

> 实测：只做 ①（不做 ②）会把原本通过的 `ring` 弄挂 —— 这正是 ① 必须配 ② 的原因，也是它比 ③ 更"重"的原因。
> 完整 5 处（①②③④⑤）的版本在分支 `fix/p2p-group-deadlock-v2` 上，功能同样正确，作为备选保留。

---

## 4. 二分矩阵（全部在 `250ee84` 上实测）

| 修复集合 | ring | self1 | self2 | **remote2** | remote2same | self2same | mixed | 合计 |
|---|---|---|---|---|---|---|---|---|
| 无（pristine） | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | 2/7 |
| 仅 ④ | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | 2/7 |
| **仅 ③** | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | 6/7 |
| ③⑤ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | 6/7 |
| ①②⑤ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | 6/7 |
| ①②④⑤ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | 6/7 |
| ①②③⑤ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | 6/7 |
| ①②③④ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | 6/7 |
| **③④⑤** | ✅ | ✅ | ✅ | **✅** | ✅ | ✅ | ✅ | **7/7** ✅ |
| ①②③④⑤ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 7/7 |

**读法**：`remote2` 是唯一有区分力的 pattern；其余 6 个只要 ③（或 ①②）到位就全过。任意 **4 个**的组合都在 `remote2` 上失败 → **③④⑤ 恰好是最小充分集**。

---

## 5. 验证（`fix/p2p-hang-matrix`，③④⑤）

**A) p2p pattern 矩阵 —— 7/7**

```
ring ✅  self1 ✅  self2 ✅  remote2 ✅  remote2same ✅  self2same ✅  mixed ✅
```

**B) `perf_core_sendrecv`**（你的 sweep 脚本里原本被注释掉的那项）：`128K → 4G`、`w5 n20` → **exit=0，16 个尺寸全部出结果**。

**C) 完整 `test/script/perf_host_api_2card.sh 2`**

| | 修复前基线 | ③④⑤ |
|---|---|---|
| 结果 | PASS=13 **FAIL=2** | **PASS=13 FAIL=2（逐项一致）** |
| FAIL 项 | `p2p_engine`、`one_side_register` | **相同**（既有问题） |
| allgather 4G | 30.471 GB/s | 30.469 GB/s |
| allreduce 4G | 15.854 GB/s | 15.850 GB/s |
| alltoall 4G | 31.871 GB/s | 31.873 GB/s |
| sendrecv 4G | 15.939 GB/s | 15.937 GB/s |
| reduce / broadcast / gather / scatter / reducescatter 4G | — | 均与基线一致 |

> 附带：`ipc_sendrecv` 4G 从基线 `0.3379s / 12.71 GB/s` 变为 `0.0770s / 55.81 GB/s` —— 这是**上游 transport 重构**带来的提速，与本次修复无关。

---

## 6. 判决性实验（每个结论的来源）

| 结论 | 实验 | 结果 |
|---|---|---|
| ③ 是解开死锁的点 | 只做 ③（保留门控） | 6/7；同时 gdb 显示三线程自旋状态消失 |
| ① 必须配 ② | 只做 ①、不做 ② | `ring`/`self1` **由通过变挂死** |
| ④ 与 ③ 耦合 | 「仅 ④」对比「③ 单独」 | 仅 ④ = 2/7（完全不生效）；③ 到位后才成为必需 |
| ⑤ 独立且必要 | ③④（不做 ⑤） | 6/7，仅 `remote2` 挂（跨 rank step 流水线四环） |
| 最小集 | 逐个"去掉一个"共 5 组 4-of-5 + 多组 3-of-5 | 只有 ③④⑤ = 7/7 |
| 上游未修 | 在 `250ee84` 上跑同一矩阵 | 7 项里 5 项复现挂死 |
| 无回归 | 完整 sweep 对比基线 | PASS/FAIL 逐项一致、吞吐无差异 |
| `perf_allreduce` segv **非本次引入** | `git checkout` 恢复核心文件重建后复测 | pristine 上同样 segv（且只在该用例带 `FLAGCX_USE_HETERO_COMM=1` 时出现） |

---

## 7. 遗留问题（不影响上面的结论）

1. **`self3`/`self4` 是"假通过"**：Round-0 self 配对按 `(bytes, dtype, channelId)` 配对，**配不上的任务被塞回 `peers[comm->rank]` 队列，而 Round 1+ 只遍历 `p2pSchedule` 的对端（永不含 self）→ 这些 op 被静默丢弃**，数据实际未传输。
2. **消息数不配对的非法 pattern（`self3`/`self4`/`remote3`/`remote4`）会挂死而非报错**，建议显式校验并返回错误。
3. **`proxyInfo.events[]` / `args.subs[]` 按"每 op 从 0 起算"的计数器索引** —— 同一连接上并发的多个 op 会复用同一 event 槽。③ 的修复让并发变多后更接近可达（本次 sweep 未暴露异常，建议单独收口）。
4. **`p2pOpHashMap` 计数器永不重置、map 无限增长** → 槽位分配依赖历史调用次数。
5. **`perf_allreduce` + `FLAGCX_USE_HETERO_COMM=1` 崩溃（exit=139）**：已定位为 **ixcuda 适配器 `hostGetDevicePointer` 为 NULL 却被无条件调用**（`ixcuda_adaptor.cc:387` 显式 `NULL`；`uni_runner_impl.cc:1816` 调用点无检查）；`hip` 适配器同样缺失。与常规 sweep 无关（集合通信不带该 env）。上游新 CI 也把 heterogeneous perf 的集合通信限定为 alltoall/alltoallv/sendrecv/allgather/broadcast/gather/scatter，理由写着 *"uniRunner does not implement reduction collectives"*。
6. 上游 `511cdac` 遗留的 `work->isBarrierOp_ = true`（注释写着 "hanging issue"）—— 本次修复后可能可以去掉，值得复测。

---

## 8. 分支 / 补丁 / 回滚

```
fix/p2p-hang-matrix（基于 250ee84）
5670481 fix(proxy): advance the whole p2p queue instead of only the head
1586eb5 fix(p2p): mix the op key before deriving the sync slot index
fb08e8b fix(group): key the p2p step by bytes/dtype so both ranks agree
1c4092d test(perf): add self2same/remote2same patterns for p2p hang triage
```

```bash
# 回滚
git checkout -- flagcx/core/proxy.cc flagcx/core/p2p.cc flagcx/core/group.cc
# 构建
make USE_ILUVATAR=1 MPI_HOME=/usr/local/openmpi -j$(nproc)
make -C test/perf/host_api USE_ILUVATAR=1 MPI_HOME=/usr/local/openmpi -j$(nproc)
```

备选：`fix/p2p-group-deadlock-v2`（5 处，①②③④⑤；功能等价，但多动了 `launch_kernel.{h,cc}` 与退役语义）。
