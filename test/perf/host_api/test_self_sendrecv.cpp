#include "perf_common.h"

#include <string>

// Pattern-driven send/recv test built on the shared perf framework.
//
// PATTERN selects the batch submitted in every iteration:
//   ring   (default) : 1 send + 1 recv to the next rank       (same as perf_sendrecv)
//   self1            : 1 send + 1 recv to the own rank
//   self2            : 2 sends + 2 recvs to the own rank      (suspected shape)
//   self3            : 2 sends + 1 recv to the own rank       (control: should pass)
//   remote2          : 2 sends + 2 recvs to the next rank     (same shape, no self)
//   remote3          : 2 sends + 1 recv to the next rank      (3 remote ops, control)
//   self4            : 1 send + 2 recvs to the own rank       (mirror of self3)
//   remote4          : 1 send + 2 recvs to the next rank      (mirror of remote3)
//   mixed            : 1 ring pair + 2 self pairs             (same as test_core_sendrecv)
//
// Build:
//   make -C test/perf/host_api USE_ILUVATAR=1 MPI_HOME=/usr/local/openmpi -j$(nproc)
// Run:
//   PATTERN=self2 mpirun --allow-run-as-root -np 2 -x PATTERN \
//     -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_MEM_ENABLE=1 -x FLAGCX_VMM_ENABLE=0 \
//     -x CUDA_VISIBLE_DEVICES=1,2 ./build/bin/perf_self_sendrecv -b 8 -e 64K -f 2 -w 1 -n 2

namespace {

struct SelfBufs {
  void *s1;
  void *s2;
  void *r1;
  void *r2;
  size_t n1;
  size_t n2;
};

std::string gPattern = "ring";

void collFn(PerfContext &ctx, size_t count) {
  SelfBufs *b = (SelfBufs *)ctx.userData;
  size_t bytes = count * getFlagcxDataTypeSize(ctx.datatype);
  int nextPeer = (ctx.proc + 1) % ctx.totalProcs;
  int selfPeer = ctx.proc;

  flagcxGroupStart(ctx.comm);

  if (gPattern == "ring") {
    flagcxSend(ctx.sendbuff, bytes, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(ctx.recvbuff, bytes, flagcxChar, nextPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "self1") {
    flagcxSend(b->s1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "self2") {
    flagcxSend(b->s1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxSend(b->s2, b->n2, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n2, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "self3") {
    flagcxSend(b->s1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxSend(b->s2, b->n2, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n2, flagcxChar, selfPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "remote2") {
    flagcxSend(b->s1, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxSend(b->s2, b->n2, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n2, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r1, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "self2same") {
    flagcxSend(b->s1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxSend(b->s2, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "remote2same") {
    flagcxSend(b->s1, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxSend(b->s2, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r1, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "remote3") {
    flagcxSend(b->s1, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxSend(b->s2, b->n2, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n2, flagcxChar, nextPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "self4") {
    flagcxSend(b->s1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n2, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
  } else if (gPattern == "remote4") {
    flagcxSend(b->s1, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n2, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r1, b->n1, flagcxChar, nextPeer, ctx.comm, ctx.stream);
  } else { // "mixed"
    flagcxSend(ctx.sendbuff, bytes, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxRecv(ctx.recvbuff, bytes, flagcxChar, nextPeer, ctx.comm, ctx.stream);
    flagcxSend(b->s1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxSend(b->s2, b->n2, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r2, b->n2, flagcxChar, selfPeer, ctx.comm, ctx.stream);
    flagcxRecv(b->r1, b->n1, flagcxChar, selfPeer, ctx.comm, ctx.stream);
  }

  flagcxGroupEnd(ctx.comm);
}

} // namespace

int main(int argc, char *argv[]) {
  PerfContext ctx;
  perfSetup(ctx, argc, argv);

  const char *patternEnv = getenv("PATTERN");
  if (patternEnv != NULL && *patternEnv != '\0') {
    gPattern = patternEnv;
  }

  SelfBufs bufs;
  bufs.n1 = 100;
  bufs.n2 = 200;
  bufs.s1 = bufs.s2 = bufs.r1 = bufs.r2 = NULL;
  ctx.devHandle->deviceMalloc(&bufs.s1, bufs.n1, flagcxMemDevice, NULL);
  ctx.devHandle->deviceMalloc(&bufs.s2, bufs.n2, flagcxMemDevice, NULL);
  ctx.devHandle->deviceMalloc(&bufs.r1, bufs.n1, flagcxMemDevice, NULL);
  ctx.devHandle->deviceMalloc(&bufs.r2, bufs.n2, flagcxMemDevice, NULL);
  ctx.devHandle->deviceMemset(bufs.s1, 0xAA, bufs.n1, flagcxMemDevice,
                              ctx.stream);
  ctx.devHandle->deviceMemset(bufs.s2, 0xBB, bufs.n2, flagcxMemDevice,
                              ctx.stream);
  ctx.devHandle->deviceMemset(bufs.r1, 0x00, bufs.n1, flagcxMemDevice,
                              ctx.stream);
  ctx.devHandle->deviceMemset(bufs.r2, 0x00, bufs.n2, flagcxMemDevice,
                              ctx.stream);
  ctx.devHandle->streamSynchronize(ctx.stream);
  ctx.userData = &bufs;

  if (ctx.proc == 0 && ctx.color == 0) {
    printf("pattern: %s (nranks=%d, nextPeer=%d, selfPeer=%d)\n",
           gPattern.c_str(), ctx.totalProcs, (0 + 1) % ctx.totalProcs, 0);
    fflush(stdout);
  }

  perfWarmup(ctx, collFn);
  perfBenchmarkLoop(ctx, collFn, nullptr, nullptr, nullptr, false);

  ctx.userData = nullptr;
  ctx.devHandle->deviceFree(bufs.s1, flagcxMemDevice, NULL);
  ctx.devHandle->deviceFree(bufs.s2, flagcxMemDevice, NULL);
  ctx.devHandle->deviceFree(bufs.r1, flagcxMemDevice, NULL);
  ctx.devHandle->deviceFree(bufs.r2, flagcxMemDevice, NULL);

  perfTeardown(ctx);
  return 0;
}
