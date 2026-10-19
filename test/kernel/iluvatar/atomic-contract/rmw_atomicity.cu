// RMW atomicity: is each RMW in iluvatar_platform_traits.h a real read-modify-write?
//
// Standalone so it needs neither libflagcx nor the test harness: it includes the
// traits header and calls PlatformTraits<IluvatarPlatform>::Atomic directly, so
// what is measured is the production code, not a copy of it.
//
// Every detector is an exact equality, not a race window: either the update was
// lost or it was not. One run per cell is therefore enough, and a reproduced
// value means the property holds.
//
//   fetchAdd         N threads x M increments           -> final == N*M
//   fetchSub         N*M starts, one decrement each     -> final == 0
//   fetchOr          each thread sets one distinct bit  -> final == all ones
//   fetchAnd         all ones starts, clears one bit    -> final == 0
//   exchange         each thread writes a distinct value and keeps the old one
//                                                       -> the N returned values
//                                                          are all distinct
//   compareExchange  CAS retry loop, one success each   -> final == N
//
// NEG deliberately uses a plain non-atomic read-modify-write: it must lose
// updates, otherwise the detectors above cannot be trusted to see a loss.
//
// Two topologies: one block (same SM) and two blocks (cross-SM, which is the
// shape signal/counter words actually see).
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>

#define FLAGCX_ILUVATAR_DEVICE_COMPILE 1
#define USE_ILUVATAR_ADAPTOR 1
#include "platform_traits.h"

typedef PlatformTraits<IluvatarPlatform>::Atomic A;
typedef flagcxDeviceMemoryOrder_t Order;

#define T3_NEG 0
#define T3_ADD 1
#define T3_SUB 2
#define T3_OR 3
#define T3_AND 4
#define T3_EXCH 5
#define T3_CAS 6

template <int OP>
__global__ void t3Kernel(unsigned *p, unsigned *sink, int *aux,
                         unsigned perThread) {
  const unsigned gtid = blockIdx.x * blockDim.x + threadIdx.x;
  const Order ord = flagcxDeviceMemoryOrderRelaxed;

  if (OP == T3_NEG) {
    // Deliberate non-atomic read-modify-write: volatile so it cannot be folded
    // into a single atomic, but still a lost-update generator.
    volatile unsigned *vp = p;
    for (unsigned i = 0; i < perThread; ++i) {
      const unsigned v = *vp;
      *vp = v + 1u;
    }
  } else if (OP == T3_ADD) {
    for (unsigned i = 0; i < perThread; ++i)
      (void)A::fetchAdd(p, 1u, ord);
  } else if (OP == T3_SUB) {
    for (unsigned i = 0; i < perThread; ++i)
      (void)A::fetchSub(p, 1u, ord);
  } else if (OP == T3_OR) {
    const unsigned bit = 1u << (gtid & 31u);
    (void)A::fetchOr(p, bit, ord);
  } else if (OP == T3_AND) {
    const unsigned bit = 1u << (gtid & 31u);
    (void)A::fetchAnd(p, ~bit, ord);
  } else if (OP == T3_EXCH) {
    sink[gtid] = A::exchange(p, gtid + 1u, ord);
  } else {
    // CAS retry loop. compareExchange writes the observed value back into
    // expected on failure, so the retry always targets the current value.
    for (unsigned i = 0; i < perThread; ++i) {
      unsigned expected = A::load(p, ord);
      int guard = 0;
      while (!A::compareExchange(p, expected, expected + 1u, ord)) {
        if (++guard > 1000000) {
          atomicAdd(aux, 1); // forward-progress failure
          return;
        }
      }
    }
  }
  (void)sink;
  (void)aux;
}

#define CK(x)                                                                  \
  do {                                                                         \
    cudaError_t e_ = (x);                                                      \
    if (e_ != cudaSuccess) {                                                   \
      printf("FAIL %s -> %s\n", #x, cudaGetErrorString(e_));                   \
      return 1;                                                                \
    }                                                                          \
  } while (0)

struct Cell {
  int op;
  const char *name;
  int grid;
  unsigned perThread;
  unsigned init;
};

static int cmpU(const void *a, const void *b) {
  const unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
  return (x > y) - (x < y);
}

int main() {
  const int block = 256;
  const Cell cells[] = {
      {T3_NEG, "NEG(non-atomic +=)", 2, 1000u, 0u},
      {T3_ADD, "fetchAdd", 1, 1000u, 0u},
      {T3_ADD, "fetchAdd", 2, 1000u, 0u},
      {T3_SUB, "fetchSub", 2, 1000u, 0u}, // init filled in below
      {T3_OR, "fetchOr", 2, 1u, 0u},
      {T3_AND, "fetchAnd", 2, 1u, 0xFFFFFFFFu},
      {T3_EXCH, "exchange", 2, 1u, 0u},
      {T3_CAS, "compareExchange", 2, 1u, 0u},
  };
  const int kCells = (int)(sizeof(cells) / sizeof(cells[0]));

  unsigned *p = NULL, *sink = NULL, *host = NULL;
  int *aux = NULL;
  CK(cudaMalloc(&p, sizeof(unsigned)));
  CK(cudaMalloc(&sink, 1024 * sizeof(unsigned)));
  CK(cudaMalloc(&aux, 2 * sizeof(int)));
  CK(cudaMallocHost(&host, 1024 * sizeof(unsigned)));

  for (int c = 0; c < kCells; ++c) {
    const unsigned nThreads = (unsigned)(cells[c].grid * block);
    const unsigned total = nThreads * cells[c].perThread;
    unsigned init = cells[c].init;
    if (cells[c].op == T3_SUB) init = total;

    CK(cudaMemset(p, 0, sizeof(unsigned)));
    if (init != 0u) CK(cudaMemcpy(p, &init, sizeof(unsigned), cudaMemcpyHostToDevice));
    CK(cudaMemset(sink, 0, 1024 * sizeof(unsigned)));
    CK(cudaMemset(aux, 0, 2 * sizeof(int)));

    switch (cells[c].op) {
      case T3_NEG:
        t3Kernel<T3_NEG><<<cells[c].grid, block>>>(p, sink, aux, cells[c].perThread);
        break;
      case T3_ADD:
        t3Kernel<T3_ADD><<<cells[c].grid, block>>>(p, sink, aux, cells[c].perThread);
        break;
      case T3_SUB:
        t3Kernel<T3_SUB><<<cells[c].grid, block>>>(p, sink, aux, cells[c].perThread);
        break;
      case T3_OR:
        t3Kernel<T3_OR><<<cells[c].grid, block>>>(p, sink, aux, cells[c].perThread);
        break;
      case T3_AND:
        t3Kernel<T3_AND><<<cells[c].grid, block>>>(p, sink, aux, cells[c].perThread);
        break;
      case T3_EXCH:
        t3Kernel<T3_EXCH><<<cells[c].grid, block>>>(p, sink, aux, cells[c].perThread);
        break;
      default:
        t3Kernel<T3_CAS><<<cells[c].grid, block>>>(p, sink, aux, cells[c].perThread);
        break;
    }
    CK(cudaDeviceSynchronize());

    unsigned got = 0;
    CK(cudaMemcpy(&got, p, sizeof(unsigned), cudaMemcpyDeviceToHost));
    int auxHost[2] = {0, 0};
    CK(cudaMemcpy(auxHost, aux, 2 * sizeof(int), cudaMemcpyDeviceToHost));

    char detail[128] = "";
    int ok = 0;
    unsigned expect = 0;
    if (cells[c].op == T3_NEG) {
      ok = (got < total);
      snprintf(detail, sizeof(detail), "expect <%u (lost updates)", total);
    } else if (cells[c].op == T3_ADD) {
      expect = total;
      ok = (got == expect);
      snprintf(detail, sizeof(detail), "expect %u", expect);
    } else if (cells[c].op == T3_SUB) {
      ok = (got == 0u);
      snprintf(detail, sizeof(detail), "expect 0");
    } else if (cells[c].op == T3_OR) {
      ok = (got == 0xFFFFFFFFu);
      snprintf(detail, sizeof(detail), "expect 0xffffffff");
    } else if (cells[c].op == T3_AND) {
      ok = (got == 0u);
      snprintf(detail, sizeof(detail), "expect 0");
    } else if (cells[c].op == T3_EXCH) {
      CK(cudaMemcpy(host, sink, nThreads * sizeof(unsigned), cudaMemcpyDeviceToHost));
      qsort(host, nThreads, sizeof(unsigned), cmpU);
      // The verdict is "every exchange handed back a distinct previous value".
      // The memory word itself holds whichever thread exchanged last, which is
      // incidental, so report the distinct count rather than that value.
      unsigned distinctCount = 1;
      for (unsigned i = 1; i < nThreads; ++i)
        if (host[i] != host[i - 1]) ++distinctCount;
      const int inRange = (host[nThreads - 1] <= nThreads);
      ok = (distinctCount == nThreads) && inRange;
      got = distinctCount;
      snprintf(detail, sizeof(detail), "expect %u distinct old values <= %u",
               nThreads, nThreads);
    } else {
      expect = nThreads * cells[c].perThread;
      ok = (got == expect) && (auxHost[0] == 0);
      snprintf(detail, sizeof(detail), "expect %u, fwd-fail=%d", expect, auxHost[0]);
    }

    printf("PROBE|rmw-atomicity|%-20s|grid=%d block=%d x %-4u|%s|got=%u|%s\n",
           cells[c].name, cells[c].grid, block, cells[c].perThread, detail, got,
           ok ? "OK" : "VIOLATION");
    fflush(stdout);
  }

  cudaFreeHost(host);
  cudaFree(p);
  cudaFree(sink);
  cudaFree(aux);
  printf("RMW_ATOMICITY_DONE\n");
  return 0;
}
