// Parameterised reproducer for "which op pattern inside one group hangs?".
//
// It uses the *public* API only (flagcxSend/Recv + flagcxGroupStart/End and the
// shared perf setup), so whatever it reproduces also affects normal users.
//
// OPS is a comma/space separated list of tokens <dir><target>:<bytes>:
//   dir    : S = send, R = recv
//   target : N = next rank ((rank+1) % nranks), S = own rank (self-loopback)
//   bytes  : 0 means maxBytes
//
// Examples
//   OPS="SN:0,RN:0"                              # what perf_sendrecv does (1 send + 1 recv, remote)
//   OPS="SS:100,RS:100"                          # 1 send + 1 recv, both to self
//   OPS="SS:100,SS:200,RS:200,RS:100"            # 2 sends + 2 recvs to self (original order)
//   OPS="SS:100,RS:100,SS:200,RS:200"            # same, size aligned in order
//   OPS="SS:100,SS:200,RS:200"                   # 2 sends + 1 recv to self
//   OPS="SN:0,RN:0,SS:100,SS:200,RS:200,RS:100"  # the batch used by test_core_sendrecv.cpp
//
// Build:
//   cd <root>/test/perf/host_api
//   ROOT=<root>
//   mpicxx -std=c++17 -g -DOMPI_SKIP_MPICXX -DMPICH_SKIP_MPICXX -o repro_group_pattern repro_group_pattern.cpp \
//     $ROOT/test/tools.cc $ROOT/test/perf_common.cc \
//     -I$ROOT/flagcx/include -I$ROOT/flagcx/adaptor/include -I$ROOT/flagcx/core/include \
//     -I$ROOT/flagcx/runner/include -I$ROOT/flagcx/service/include \
//     -I$ROOT/third-party/json/single_include -I$ROOT/test/include \
//     -I$ROOT/flagcx/core -I$ROOT/flagcx/service -I$ROOT \
//     -L$ROOT/build/lib -Wl,--no-as-needed -Wl,-rpath,$ROOT/build/lib -lflagcx \
//     -L/usr/local/corex/lib64 -lcudart -lcuda
//
// Run:
//   export CUDA_VISIBLE_DEVICES=1,2
//   OPS="SS:100,SS:200,RS:200,RS:100" timeout 60 mpirun --allow-run-as-root -np 2 \
//     -x OPS -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_MEM_ENABLE=1 -x FLAGCX_VMM_ENABLE=0 \
//     -x CUDA_VISIBLE_DEVICES ./repro_group_pattern -b 8 -e 64K -f 2 -w 1 -n 2 -R 1

#include "perf_common.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Op {
  bool isSend;
  bool toSelf;
  size_t bytes;
};

const char *envOrDefault(const char *name, const char *dflt) {
  const char *value = getenv(name);
  return (value != NULL && *value != '\0') ? value : dflt;
}

// Parses tokens such as "SN:8", "RS:200", separated by commas or spaces.
std::vector<Op> parseOps(const char *spec) {
  std::vector<Op> ops;
  const char *p = spec;
  while (*p != '\0') {
    while (*p == ' ' || *p == ',' || *p == '\t')
      p++;
    if (*p == '\0')
      break;
    Op op;
    op.isSend = false;
    op.toSelf = false;
    op.bytes = 0;
    if (*p == 'S' || *p == 's')
      op.isSend = true;
    else if (*p == 'R' || *p == 'r')
      op.isSend = false;
    else {
      fprintf(stderr, "parselops: bad direction at '%s'\n", p);
      exit(2);
    }
    p++;
    if (*p == 'N' || *p == 'n')
      op.toSelf = false;
    else if (*p == 'S' || *p == 's')
      op.toSelf = true;
    else {
      fprintf(stderr, "parseOps: bad target at '%s'\n", p);
      exit(2);
    }
    p++;
    if (*p != ':') {
      fprintf(stderr, "parseOps: expected ':' at '%s'\n", p);
      exit(2);
    }
    p++;
    char *end = NULL;
    op.bytes = (size_t)strtoull(p, &end, 0);
    p = end;
    ops.push_back(op);
  }
  return ops;
}

std::vector<Op> g_ops;

void collFn(PerfContext &ctx, size_t count) {
  (void)count;
  flagcxGroupStart(ctx.comm);
  for (size_t i = 0; i < g_ops.size(); i++) {
    const Op &op = g_ops[i];
    int peer = op.toSelf ? ctx.proc : (ctx.proc + 1) % ctx.totalProcs;
    size_t bytes = (op.bytes == 0) ? ctx.maxBytes : op.bytes;
    if (bytes > ctx.maxBytes)
      bytes = ctx.maxBytes;
    if (op.isSend)
      flagcxSend(ctx.sendbuff, bytes, flagcxChar, peer, ctx.comm, ctx.stream);
    else
      flagcxRecv(ctx.recvbuff, bytes, flagcxChar, peer, ctx.comm, ctx.stream);
  }
  flagcxGroupEnd(ctx.comm);
}

} // namespace

int main(int argc, char *argv[]) {
  PerfContext ctx;
  perfSetup(ctx, argc, argv);

  g_ops = parseOps(envOrDefault("OPS", "SN:0,RN:0"));

  if (ctx.proc == 0 && ctx.color == 0) {
    printf("pattern:");
    for (size_t i = 0; i < g_ops.size(); i++) {
      printf(" %s%s:%zu", g_ops[i].isSend ? "S" : "R",
             g_ops[i].toSelf ? "S" : "N", g_ops[i].bytes);
    }
    printf("   (nranks=%d)\n", ctx.totalProcs);
    fflush(stdout);
  }

  perfWarmup(ctx, collFn);
  perfBenchmarkLoop(ctx, collFn, nullptr, nullptr, nullptr, false);
  perfTeardown(ctx);
  return 0;
}
