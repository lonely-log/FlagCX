// Minimal A/B reproducer: is "send/recv to your own rank" (self-loopback)
// broken in the FlagCX hetero (uniRunner) path?
//
// This is test_sendrecv.cpp with a single change: the peer rank is selected by
// the SELF_LOOP environment variable.
//
//   SELF_LOOP=0 (default) -> peer = next rank       (same as perf_sendrecv)
//   SELF_LOOP=1           -> peer = own rank        (self-loopback)
//
// Everything else (setup, buffers, warmup, benchmark loop, teardown) comes
// from the shared perf infrastructure, so the two runs differ only in the peer.
//
// Build (same flags as the perf tests):
//   cd <root>/test/perf/host_api
//   ROOT=<root>
//   mpicxx -std=c++17 -g -DOMPI_SKIP_MPICXX -DMPICH_SKIP_MPICXX \
//     -o repro_self_sendrecv repro_self_sendrecv.cpp \
//     $ROOT/test/tools.cc $ROOT/test/perf_common.cc \
//     -I$ROOT/flagcx/include -I$ROOT/flagcx/adaptor/include \
//     -I$ROOT/flagcx/core/include -I$ROOT/flagcx/runner/include \
//     -I$ROOT/flagcx/service/include -I$ROOT/third-party/json/single_include \
//     -I$ROOT/test/include -I$ROOT/flagcx/core -I$ROOT/flagcx/service -I$ROOT \
//     -L$ROOT/build/lib -Wl,--no-as-needed -Wl,-rpath,$ROOT/build/lib -lflagcx \
//     -L/usr/local/corex/lib64 -lcudart -lcuda
//
// Run (2 ranks):
//   export FLAGCX_USE_HETERO_COMM=1 FLAGCX_MEM_ENABLE=1 FLAGCX_VMM_ENABLE=0
//   SELF_LOOP=0 mpirun --allow-run-as-root -np 2 ./repro_self_sendrecv -b 8 -e 64K -f 2 -w 1 -n 2 -R 1   # expect: passes
//   SELF_LOOP=1 mpirun --allow-run-as-root -np 2 ./repro_self_sendrecv -b 8 -e 64K -f 2 -w 1 -n 2 -R 1   # expect: hangs
//
// Even smaller variant (no cross-rank traffic at all):
//   SELF_LOOP=1 mpirun --allow-run-as-root -np 1 ./repro_self_sendrecv -b 8 -e 64K -f 2 -w 1 -n 2 -R 1
//
// Expected observation when it hangs: no "Comm size:" line is printed (it
// stalls inside the warmup), the main thread sits in streamSynchronize and GPU
// utilisation stays at 0.

#include "perf_common.h"

static bool selfLoop() {
  const char *env = getenv("SELF_LOOP");
  return env != NULL && atoi(env) == 1;
}

static void collFn(PerfContext &ctx, size_t count) {
  int peer = selfLoop() ? ctx.proc : (ctx.proc + 1) % ctx.totalProcs;
  flagcxGroupStart(ctx.comm);
  flagcxSend(ctx.sendbuff, count, ctx.datatype, peer, ctx.comm, ctx.stream);
  flagcxRecv(ctx.recvbuff, count, ctx.datatype, peer, ctx.comm, ctx.stream);
  flagcxGroupEnd(ctx.comm);
}

int main(int argc, char *argv[]) {
  PerfContext ctx;
  perfSetup(ctx, argc, argv);

  if (ctx.proc == 0 && ctx.color == 0) {
    printf("mode: %s  (peer = %s, totalProcs = %d)\n",
           selfLoop() ? "SELF-LOOP" : "RING",
           selfLoop() ? "own rank" : "next rank", ctx.totalProcs);
    fflush(stdout);
  }

  perfWarmup(ctx, collFn);
  perfBenchmarkLoop(ctx, collFn, nullptr, nullptr, nullptr, false);
  perfTeardown(ctx);
  return 0;
}
