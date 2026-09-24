#!/usr/bin/env python3
"""Minimal probe: which Torch P2P variant hangs with the FlagCX backend?

example.py's test_sendrecv() mixes three different point-to-point paths into
one function, so a hang cannot be attributed to any single one of them. This
script splits them into separately selectable parts:

  -o send      plain blocking dist.send / dist.recv
  -o batch     dist.batch_isend_irecv (async coalescing -> groupStart/End)
  -o barrier   dist.barrier(async_op=True)
  -o all       all three, in that order (default)

Run one part at a time and see which one fails to reach its "<<< END" line.

  torchrun --standalone --nproc_per_node 2 torch_p2p_probe.py -o batch

Every part logs ">>> BEGIN" and "<<< END"; the one that prints BEGIN but never
END is the culprit.
"""

import argparse
import os
import sys

# Same device-backend detection order as example.py.
try:
    import torch_mlu  # noqa: F401

    dev_name = "mlu"
except Exception:
    try:
        import torch_npu  # noqa: F401

        dev_name = "npu"
    except Exception:
        try:
            import torch_musa  # noqa: F401

            dev_name = "musa"
        except Exception:
            dev_name = "cuda"

import torch

import flagcx  # noqa: F401  -- registers the flagcx backend with torch
import torch.distributed as dist

RANK = int(os.environ["RANK"])
LOCAL_RANK = int(os.environ["LOCAL_RANK"])
WORLD = int(os.environ["WORLD_SIZE"])
NEXT = (RANK + 1) % WORLD
PREV = (RANK - 1 + WORLD) % WORLD

GROUP = None
DEV = f"{dev_name}:{LOCAL_RANK}"


def log(*args):
    print(f"[rank {RANK}]", *args, flush=True)


def part_send():
    log(">>> BEGIN plain dist.send/recv")
    x = torch.full((WORLD,), float(RANK), device=DEV)
    y = torch.full((WORLD,), -1.0, device=DEV)
    # Even/odd split so the blocking send/recv cannot deadlock on itself.
    if RANK % 2 == 0:
        dist.send(x, NEXT, group=GROUP)
        dist.recv(y, PREV, group=GROUP)
    else:
        dist.recv(y, PREV, group=GROUP)
        dist.send(x, NEXT, group=GROUP)
    log("    y =", y.tolist(), "expected", float(PREV))
    log("<<< END   plain dist.send/recv")


def part_batch():
    log(">>> BEGIN batch_isend_irecv")
    x = torch.full((WORLD,), float(RANK), device=DEV)
    y = torch.full((WORLD,), -1.0, device=DEV)
    ops = [
        dist.P2POp(dist.isend, x, NEXT, group=GROUP),
        dist.P2POp(dist.irecv, y, PREV, group=GROUP),
    ]
    reqs = dist.batch_isend_irecv(ops)
    log(f"    batch_isend_irecv returned {len(reqs)} work object(s)")
    for i, req in enumerate(reqs):
        req.wait()
        log(f"    work[{i}].wait() returned")
    log("    y =", y.tolist(), "expected", float(PREV))
    log("<<< END   batch_isend_irecv")


def part_barrier():
    log(">>> BEGIN barrier(async_op=True)")
    handle = dist.barrier(group=GROUP, async_op=True)
    handle.wait()
    log("<<< END   barrier(async_op=True)")


def main():
    global GROUP

    ap = argparse.ArgumentParser()
    ap.add_argument(
        "-o",
        "--op",
        default="all",
        choices=["send", "batch", "barrier", "all"],
        help="which P2P variant to exercise (default: all)",
    )
    args = ap.parse_args()

    # Fail loudly instead of silently exiting 0 like example.py does: for a
    # probe, "nothing ran" must be obvious.
    if not torch.cuda.is_available():
        log(f"[SKIP] torch.cuda.is_available() is False (device={dev_name})")
        sys.exit(3)

    torch.cuda.set_device(LOCAL_RANK)
    dist.init_process_group(
        f"cpu:gloo,{dev_name}:flagcx", rank=RANK, world_size=WORLD
    )
    GROUP = dist.new_group(
        ranks=list(range(WORLD)), backend=f"cpu:gloo,{dev_name}:flagcx"
    )
    log(
        f"init ok | world={WORLD} next={NEXT} prev={PREV} "
        f"device_index={torch.cuda.current_device()} tensor_dev={DEV}"
    )

    if args.op in ("send", "all"):
        part_send()
    if args.op in ("batch", "all"):
        part_batch()
    if args.op in ("barrier", "all"):
        part_barrier()

    log("ALL REQUESTED PARTS COMPLETED")
    dist.destroy_process_group()


if __name__ == "__main__":
    main()
