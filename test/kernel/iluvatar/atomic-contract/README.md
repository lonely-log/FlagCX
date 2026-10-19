# Atomic 契约验证探针

直连 `PlatformTraits<IluvatarPlatform>::Atomic`，验证
`flagcx/adaptor/include/device_api/iluvatar_platform_traits.h` 里 `struct Atomic` 的 8 个成员。

**这四个程序不在 make 构建里**：它们只需要该头文件 + `libcudart`，不链接 `libflagcx`，
所以既不进 `test/kernel` 的 Makefile，也不触碰 T1–T4 的驱动与 K/S 编号。

## 四个程序回答四个不同的问题

| 探针 | 回答什么 | 判据 | 需要 GPU |
|---|---|---|---|
| `order_scope_compile.py` | `order` / `Scope` 到底有没有被用 | 8 成员 × 5 order × 4 scope = 160 kernel，**只有 9 种指令序列** | ❌ |
| `load_store_order.cu` | 发布边 / 观察边有没有效 | `store(Release)` → 0 违例；`store(Relaxed)` → 大量；NEG 必须大量、POS 必须 0 | ✅ |
| `rmw_atomicity.cu` | RMW 是不是真 read-modify-write | 精确等式：`fetchAdd`==N·M、`fetchSub`==0、`fetchOr`==0xffffffff、`fetchAnd`==0、`exchange` 旧值互异、CAS==N 且 `fwd-fail==0`；NEG 必须丢更新 | ✅ |
| `value_contract.cu` | 交回调用方的值对不对 | 24 个精确值：返回旧值、CAS 失败写回 `expected`、32/64 位往返 | ✅ |
| `value_contract_64bit_rmw_must_be_rejected.cu` | 64 位 RMW 被编译期拒绝 | **编译必须失败**，且 `CoreX RMW operations require a 32-bit operand` 出现 **6 次** | ❌ |

四者的关系：`order_scope_compile` 证明参数被丢弃（纯编译期），另外三个证明运行时行为。
`rmw_atomicity` 只看共享内存最终值，`value_contract` 只看每次调用交回的**回执** —— 两者不可互相替代。

## 一条命令跑完全部

```bash
./run_atomic_contract.sh                        # 结果写到 /tmp/atomic-contract/
REPO=/path/to/FlagCX ./run_atomic_contract.sh   # 显式指定仓库根
OUT=/tmp/my-run ./run_atomic_contract.sh        # 显式指定输出目录
CLANG=/path/to/clang ./run_atomic_contract.sh   # 显式指定编译器
```

需要：`clang`（CoreX 工具链，默认 `/usr/local/corex/bin/clang`）、能链 `libcudart`、`python3`。
默认用 `CUDA_VISIBLE_DEVICES=1,2`（宿主机 GPU0 有间歇性挂死，已排除）。

## 期望的签名（通过的样子）

```
PROBE|order-scope-compile|TOTAL|kernels=160|distinct=9|prediction=9|HOLDS
PROBE|load-store-order|RELEASE/RELAXED        |violations=0      ... |expect=none|OK
PROBE|rmw-atomicity|fetchAdd            |grid=2 ... |expect 512000|got=512000|OK
PROBE|value-contract|64bit-rmw-rejected-at-compile-time|want=6|got=6|OK
```

汇总：`OK = 122 / MISMATCH = 0 / VIOLATION = 0`

## 日志不入库

本目录只放源码。运行产生的日志、汇编、IR 都不进版本库：

- 需要时用 `run_atomic_contract.sh` 重新生成（确定性；唯一会变的是 NEG 那格，因为它是真竞态）
- `.s` / `.ll` 是 `order_scope_compile.py` 的中间产物，一条命令即可再生

## 已知边界

跨卡（peer 访问）、64 位 `load/store` 的原子性、Block/Thread scope 的真实充分性，
本组探针**都没有覆盖**；32 位 `load/store` 自身的原子性也未测（`value_contract` 是单线程）。
