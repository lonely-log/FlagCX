#!/usr/bin/env python3
"""Minimal repro: FlagCX Torch backend deadlocks in batch_isend_irecv.

Symptom (exactly 2 ranks, single node)
    dist.batch_isend_irecv([P2POp(isend, NEXT), P2POp(irecv, PREV)])
    never completes.  work.wait() returns, but the device synchronisation
    that follows blocks forever:

        [rank 0]     wait() returned -- nothing is synchronised yet
        [rank 0] >>> torch.cuda.synchronize()
        <hangs here, forever>

    The payload is 2 floats and the tensors are valid, so this is a
    scheduling deadlock, not a transport or size problem.

Root cause
    plugin/torch/flagcx/src/backend_flagcx.cpp, endCoalescing():

        // Sort by peer ascending: canonical (min,max) order avoids deadlock.
        std::stable_sort(pendingOps.begin(), pendingOps.end(),
                         [](const auto &a, const auto &b) {
                           return a.first < b.first;   // key == peer rank
                         });

    Sorting by peer only canonicalises the order when a rank's send and
    recv go to *different* peers.  With a single peer (2 ranks) both
    entries carry the same key, stable_sort keeps the insertion order, and
    both ranks therefore issue send-then-recv.  The GPU-side send waits for
    a matching recv that is queued behind it on the same stream -> deadlock.

    PyTorch's own NCCL batch_isend_irecv does not have this problem: it
    accepts the symmetric [isend, irecv] order, which is what upstream
    examples use.

How to run (must be exactly 2 ranks)

    torchrun --standalone --nproc_per_node 2 repro_batch_isend_irecv_deadlock.py
        -> reproduces the deadlock, watchdog exits 124

    torchrun --standalone --nproc_per_node 2 repro_batch_isend_irecv_deadlock.py --stagger
        -> workaround: odd ranks post irecv first, exits 0

Expected output
    Without --stagger the last line is the watchdog message; with
    --stagger the last line is "OK".
"""

import argparse
import os
import sys
import threading
import time

import torch

import flagcx  # noqa: F401  -- registers the flagcx backend with torch
import torch.distributed as dist

RANK = int(os.environ["RANK"])
LOCAL_RANK = int(os.environ["LOCAL_RANK"])
WORLD = int(os.environ["WORLD_SIZE"])
NEXT = (RANK + 1) % WORLD
PREV = (RANK - 1 + WORLD) % WORLD


def log(*args):
    print(f"[rank {RANK}]", *args, flush=True)


def watchdog(seconds):
    """Turn the hang into a self-terminating, self-describing failure."""
    time.sleep(seconds)
    log(f"WATCHDOG: still stuck after {seconds}s -> DEADLOCK confirmed")
    os._exit(124)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--stagger",
        action="store_true",
        help="post irecv before isend on odd ranks (workaround)",
    )
    ap.add_argument(
        "--timeout",
        type=int,
        default=30,
        help="watchdog seconds before declaring the deadlock (default 30)",
    )
    args = ap.parse_args()

    if WORLD != 2:
        log(f"this repro targets the 2-rank case (send and recv share one peer), "
            f"but WORLD={WORLD}. Re-run with --nproc_per_node 2.")
        sys.exit(2)

    threading.Thread(target=watchdog, args=(args.timeout,), daemon=True).start()

    log(f"torch={torch.__version__} world={WORLD} rank={RANK} next={NEXT} prev={PREV}")

    torch.cuda.set_device(LOCAL_RANK)
    dist.init_process_group("cpu:gloo,cuda:flagcx", rank=RANK, world_size=WORLD)
    group = dist.new_group(
        ranks=list(range(WORLD)), backend="cpu:gloo,cuda:flagcx"
    )
    log("init ok")

    dev = f"cuda:{LOCAL_RANK}"
    send_buf = torch.full((WORLD,), float(RANK), device=dev)
    recv_buf = torch.full((WORLD,), -1.0, device=dev)

    send_op = dist.P2POp(dist.isend, send_buf, NEXT, group=group)
    recv_op = dist.P2POp(dist.irecv, recv_buf, PREV, group=group)

    if args.stagger and RANK % 2 == 1:
        ops = [recv_op, send_op]
    else:
        ops = [send_op, recv_op]
    log("op order:", "irecv, isend" if ops[0] is recv_op else "isend, irecv")

    log(">>> batch_isend_irecv")
    works = dist.batch_isend_irecv(ops)
    log("    returned", len(works), "work object(s)")

    for i, work in enumerate(works):
        work.wait()
        log(f"    work[{i}].wait() returned -- nothing is synchronised yet")

    log(">>> torch.cuda.synchronize()")
    torch.cuda.synchronize()
    log("    synchronize() returned")

    got = recv_buf.tolist()
    log("recv_buf =", got, "| expected", float(PREV))
    assert got == [float(PREV)] * WORLD, "payload mismatch"

    log("OK")
    dist.destroy_process_group()


if __name__ == "__main__":
    main()
