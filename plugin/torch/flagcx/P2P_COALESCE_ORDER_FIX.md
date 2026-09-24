# FlagCX Torch 插件 — P2P 融合排序缺陷:定位与修改记录

## 4. 代码修改(共 4 处,2 个文件)

> 行号基于修改前的版本;实际应用时以代码内容匹配为准。

### 4.1 include/backend_flagcx.hpp:314

**改前:**

```cpp
  struct pairCoalesceCtx {
    bool active = false;
    std::vector<std::pair<int, std::function<void()>>> pendingOps;
  };
```

**改后:**

```cpp
  // Direction of a pending P2P operation. endCoalescing() needs it to break
  // the send/recv ordering symmetry when both ops target the same peer.
  enum class P2pDir { Send, Recv };

  struct PendingP2pOp {
    int peer;
    P2pDir dir;
    std::function<void()> run;

    PendingP2pOp(int peer, P2pDir dir, std::function<void()> run)
        : peer(peer), dir(dir), run(std::move(run)) {}
  };

  struct pairCoalesceCtx {
    bool active = false;
    std::vector<PendingP2pOp> pendingOps;
  };
```

两点说明:

- **必须提供构造函数。** `PendingP2pOp` 若为聚合类型,C++17 下
  `emplace_back(a, b, c)` 无法编译(括号形式的聚合初始化是 C++20 才有)。
- **方向用 `enum class` 而非 `bool`。** `int` 与 `bool` 可隐式互转,用 `bool`
  会让"参数顺序写反"静默通过编译。

### 4.2 src/backend_flagcx.cpp:630 — 排序 + 执行

**改前:**

```cpp
    // Sort by peer ascending: canonical (min,max) order avoids deadlock.
    std::stable_sort(
        pairCoalesce_.pendingOps.begin(), pairCoalesce_.pendingOps.end(),
        [](const auto &a, const auto &b) { return a.first < b.first; });
    for (auto &kv : pairCoalesce_.pendingOps) {
      kv.second();
    }
```

**改后:**

```cpp
    // Canonical ordering. Two properties are needed to avoid the GPU-side
    // send/recv self-deadlock:
    //   1. ops to DIFFERENT peers are ordered by peer rank, so both sides of
    //      every conversation keep the same relative position;
    //   2. when a rank's send and recv share the SAME peer -- the degenerate
    //      case that occurs with two ranks -- ordering by peer alone is a
    //      no-op (equal keys), so the tie is broken by rank: the lower rank
    //      posts its send first, the higher rank posts its recv first. That
    //      makes the two sides antisymmetric rather than identical.
    std::stable_sort(
        pairCoalesce_.pendingOps.begin(), pairCoalesce_.pendingOps.end(),
        [this](const PendingP2pOp &a, const PendingP2pOp &b) {
          if (a.peer != b.peer) {
            return a.peer < b.peer;
          }
          const bool aIsSend = (a.dir == P2pDir::Send);
          const bool bIsSend = (b.dir == P2pDir::Send);
          if (aIsSend == bIsSend) {
            return false;
          }
          const bool lowerRankSendsFirst = (rank_ < a.peer);
          return lowerRankSendsFirst ? aIsSend : bIsSend;
        });
    for (auto &op : pairCoalesce_.pendingOps) {
      op.run();
    }
```

> **`if (aIsSend == bIsSend) return false;` 这一句不能省。**
> `std::stable_sort` 要求严格弱序。若两个操作方向相同却返回 `true`,
> 即"a 排在 a 前面",违反自反性,排序行为不可预测。

### 4.3 src/backend_flagcx.cpp:1494 — send 入队

```cpp
// 改前
      pairCoalesce_.pendingOps.emplace_back(dstRank, std::move(doSend));
// 改后
      pairCoalesce_.pendingOps.emplace_back(dstRank, P2pDir::Send,
                                            std::move(doSend));
```

### 4.4 src/backend_flagcx.cpp:1548 — recv 入队

```cpp
// 改前
      pairCoalesce_.pendingOps.emplace_back(srcRank, std::move(doRecv));
// 改后
      pairCoalesce_.pendingOps.emplace_back(srcRank, P2pDir::Recv,
                                            std::move(doRecv));
```

---

## 5. 为什么这样能修好

修复后的顺序:

| rank       | 排序结果  | 流顺序                   |
| ---------- | --------- | ------------------------ |
| 0(低 rank) | send 在前 | `send -> 1`, `recv <- 1` |
| 1(高 rank) | recv 在前 | `recv <- 0`, `send -> 0` |

关键点:**规则依赖 rank,而不是操作类型。**

两个 rank 的待排列表在结构上完全相同(各一个 send、一个 recv,peer 都是对方),
因此任何只看"操作类型"的规则(不论 send 优先还是 recv 优先)都会在两边算出
**相同**的结果 —— 仍然对称,仍然死锁。只有把 rank 纳入判据,才能产生反对称。

同理:该规则**纯粹由 (rank, peer) 决定**,双方无需通信即可算出互相兼容的顺序
—— 这正是"规范化排序"成立的前提。
