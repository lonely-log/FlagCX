// Value contract: return values of struct Atomic, the compareExchange
// expected-writeback, and the 32/64-bit load/store round trip.
//
// Single thread, one exact sequence, every observation compared against a value
// derived from the source and from the documented builtin semantics. No race and
// no window: a mismatch is a real defect, not timing.
//
//   fetch* / exchange return the OLD value
//   compareExchange returns true on success, and on failure returns false AND
//   writes the observed value back into `expected` (:242 passes &expected)
//   load/store accept any T, including 64-bit (no static_assert on those)
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>

#define FLAGCX_ILUVATAR_DEVICE_COMPILE 1
#define USE_ILUVATAR_ADAPTOR 1
#include "platform_traits.h"

typedef PlatformTraits<IluvatarPlatform>::Atomic A;
typedef flagcxDeviceMemoryOrder_t Ord;

#define R (flagcxDeviceMemoryOrderRelaxed)
#define REL (flagcxDeviceMemoryOrderRelease)
#define ACQ (flagcxDeviceMemoryOrderAcquire)

__global__ void valueContractKernel(unsigned *p, unsigned long long *q, unsigned *out,
                         unsigned long long *out64) {
  if (threadIdx.x != 0)
    return;

  A::store(p, 100u, R);

  out[0] = A::fetchAdd(p, 7u, R);      // old = 100
  out[1] = A::load(p, R);              // 107
  out[2] = A::fetchSub(p, 7u, R);      // old = 107
  out[3] = A::load(p, R);              // 100
  out[4] = A::fetchOr(p, 0x0F00u, R);  // old = 100
  out[5] = A::load(p, R);              // 0x0F64 = 3940
  out[6] = A::fetchAnd(p, 0x00FFu, R); // old = 3940
  out[7] = A::load(p, R);              // 100
  out[8] = A::exchange(p, 0xDEADu, R); // old = 100
  out[9] = A::load(p, R);              // 0xDEAD

  {
    unsigned e = 0xDEADu;
    const bool ok = A::compareExchange(p, e, 0xBEEFu, R); // success
    out[10] = ok ? 1u : 0u;   // 1
    out[11] = e;              // expected untouched on success = 0xDEAD
    out[12] = A::load(p, R);  // 0xBEEF
  }
  {
    unsigned e = 0x1234u;                                 // deliberately wrong
    const bool ok = A::compareExchange(p, e, 0x9999u, R); // failure
    out[13] = ok ? 1u : 0u;   // 0
    out[14] = e;              // must be the observed value 0xBEEF
    out[15] = A::load(p, R);  // unchanged 0xBEEF
  }
  {
    unsigned e = 0xBEEFu;
    const bool ok = A::compareExchange(p, e, 0x1234u, R); // success
    out[16] = ok ? 1u : 0u;   // 1
    out[17] = A::load(p, R);  // 0x1234
  }

  // 32-bit store/load on the Relaxed and the fenced path, plus the two edges.
  A::store(p, 0x12345678u, R);
  out[18] = A::load(p, R);
  A::store(p, 0xFFFFFFFFu, REL);
  out[19] = A::load(p, ACQ);
  A::store(p, 0u, R);
  out[20] = A::load(p, R);

  // 64-bit load/store is allowed (no static_assert on those two members).
  A::store(q, 0x1122334455667788ull, REL);
  out64[0] = A::load(q, ACQ);
  A::store(q, 0xFFFFFFFFFFFFFFFFull, R);
  out64[1] = A::load(q, R);
  A::store(q, 0ull, REL);
  out64[2] = A::load(q, ACQ);
}

#define CK(x)                                                                  \
  do {                                                                         \
    cudaError_t e_ = (x);                                                      \
    if (e_ != cudaSuccess) {                                                   \
      printf("FAIL %s -> %s\n", #x, cudaGetErrorString(e_));                   \
      return 1;                                                                \
    }                                                                          \
  } while (0)

struct Exp {
  int idx;
  const char *what;
  unsigned long long want;
  int is64;
};

int main() {
  static const Exp exp[] = {
      {0, "fetchAdd returns old", 100ull, 0},
      {1, "  after fetchAdd(+7)", 107ull, 0},
      {2, "fetchSub returns old", 107ull, 0},
      {3, "  after fetchSub(-7)", 100ull, 0},
      {4, "fetchOr returns old", 100ull, 0},
      {5, "  after fetchOr(0x0f00)", 0x0F64ull, 0},
      {6, "fetchAnd returns old", 0x0F64ull, 0},
      {7, "  after fetchAnd(0x00ff)", 100ull, 0},
      {8, "exchange returns old", 100ull, 0},
      {9, "  after exchange(0xdead)", 0xDEADull, 0},
      {10, "CAS success returns true", 1ull, 0},
      {11, "  expected untouched on success", 0xDEADull, 0},
      {12, "  memory after CAS", 0xBEEFull, 0},
      {13, "CAS failure returns false", 0ull, 0},
      {14, "  expected WRITTEN BACK on failure", 0xBEEFull, 0},
      {15, "  memory unchanged after failure", 0xBEEFull, 0},
      {16, "CAS success (value unchanged) returns true", 1ull, 0},
      {17, "  memory after second CAS", 0x1234ull, 0},
      {18, "32-bit store/load round trip", 0x12345678ull, 0},
      {19, "32-bit Release store / Acquire load", 0xFFFFFFFFull, 0},
      {20, "32-bit store 0 / load", 0ull, 0},
      {0, "64-bit store/load round trip", 0x1122334455667788ull, 1},
      {1, "64-bit all-ones round trip", 0xFFFFFFFFFFFFFFFFull, 1},
      {2, "64-bit store 0 / load", 0ull, 1},
  };
  const int kN = (int)(sizeof(exp) / sizeof(exp[0]));

  unsigned *p = NULL, *out = NULL;
  unsigned long long *q = NULL, *out64 = NULL;
  CK(cudaMalloc(&p, sizeof(unsigned)));
  CK(cudaMalloc(&q, sizeof(unsigned long long)));
  CK(cudaMalloc(&out, 32 * sizeof(unsigned)));
  CK(cudaMalloc(&out64, 4 * sizeof(unsigned long long)));
  CK(cudaMemset(out, 0, 32 * sizeof(unsigned)));
  CK(cudaMemset(out64, 0, 4 * sizeof(unsigned long long)));

  valueContractKernel<<<1, 1>>>(p, q, out, out64);
  CK(cudaDeviceSynchronize());

  unsigned got[32] = {0};
  unsigned long long got64[4] = {0};
  CK(cudaMemcpy(got, out, 32 * sizeof(unsigned), cudaMemcpyDeviceToHost));
  CK(cudaMemcpy(got64, out64, 4 * sizeof(unsigned long long), cudaMemcpyDeviceToHost));

  int bad = 0;
  for (int k = 0; k < kN; ++k) {
    const unsigned long long v = exp[k].is64 ? got64[exp[k].idx] : (unsigned long long)got[exp[k].idx];
    const int ok = (v == exp[k].want);
    if (!ok) ++bad;
    printf("PROBE|value-contract|%s|want=0x%llx|got=0x%llx|%s\n", exp[k].what,
           exp[k].want, v, ok ? "OK" : "MISMATCH");
  }
  printf("VALUE_CONTRACT_DONE mismatches=%d\n", bad);
  cudaFree(p);
  cudaFree(q);
  cudaFree(out);
  cudaFree(out64);
  return 0;
}
