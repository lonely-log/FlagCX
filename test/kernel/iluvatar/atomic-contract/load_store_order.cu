// Load/store ordering: does the fence that `struct Atomic` emits actually work?
//
// The compile-time side already proved (identical ISA over 20 combinations) that
//   - Atomic::load  emits no fence for any order or scope
//   - Atomic::store emits one wbinv for every order except Relaxed
// so the only open question is behavioural: does that wbinv make the payload
// visible before the flag, and does its absence really lose the payload?
//
// Shape: two blocks, one thread each, fresh words per iteration, bounded spins.
// Both sides call the traits directly, so this measures the production code.
//
// cells
//   NEG              publisher stores the flag FIRST, then the payload
//                    -> must violate, otherwise the detector is blind
//   POS              one block, two threads, __syncthreads() between the two
//                    -> ordering is guaranteed, must not violate
//   RELEASE/RELAXED  fence on the publisher                    -> expect 0
//   RELAXED/RELAXED  no fence on the publisher                 -> expect many
//   RELEASE/ACQUIRE  fence on both                             -> expect 0
//   RELAXED/ACQUIRE  no fence on the publisher, acquire on the consumer
//                    -> expect many: the load emits nothing, so a consumer-side
//                       acquire cannot compensate for a publisher that never
//                       released
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>

#define FLAGCX_ILUVATAR_DEVICE_COMPILE 1
#define USE_ILUVATAR_ADAPTOR 1
#include "platform_traits.h"

typedef PlatformTraits<IluvatarPlatform>::Atomic A;
typedef flagcxDeviceMemoryOrder_t Ord;

#define ITER (1u << 20)
#define WINDOW 800u

__device__ __forceinline__ void knownDelay(unsigned cycles) {
  const long long s = clock64();
  while (clock64() - s < (long long)cycles) {
  }
}

// counters: [0]=violations [1]=timeouts [2]=raced [3]=producer_done
template <int PUB_ORDER, int CON_ORDER, int NEGATIVE>
__global__ void mpLitmusKernel(unsigned *data, unsigned *flag, int *c,
                           unsigned int iter, unsigned int window) {
  if (blockIdx.x == 0u) {
    for (unsigned int i = 0; i < iter; ++i) {
      const unsigned int v = i + 1u;
      if (NEGATIVE) {
        // Deliberate protocol break: publish first, payload after.
        A::store(&flag[i], v, (Ord)PUB_ORDER);
        knownDelay(window);
        data[i] = v;
      } else {
        knownDelay(window / 2u);              // publisher lags: consumer arrives first
        data[i] = v;                        // payload, plain write
        A::store(&flag[i], v, (Ord)PUB_ORDER);   // publication edge under test
        knownDelay(window / 2u);
      }
    }
    c[3] = 1;
  } else {
    for (unsigned int i = 0; i < iter; ++i) {
      const unsigned int v = i + 1u;
      int guard = 0;
      int polls = 0;
      unsigned int f = 0u;
      do {
        f = A::load(&flag[i], (Ord)CON_ORDER);   // observation edge under test
        ++polls;
      } while (f != v && ++guard < 20000000);
      if (f != v) {
        c[1] += 1;
        continue;
      }
      if (polls > 1)
        c[2] += 1;
      if (A::load(&data[i], (Ord)CON_ORDER) != v)
        c[0] += 1;
    }
  }
}

// Positive control: ordering is guaranteed by the block barrier, so any
// violation here means the measurement path is broken.
__global__ void barrierOrderedKernel(unsigned *data, unsigned *flag, int *c,
                                 unsigned int iter) {
  for (unsigned int i = 0; i < iter; ++i) {
    __syncthreads();
    if (threadIdx.x == 0u) {
      data[i] = i + 1u;
      A::store(&flag[i], i + 1u, flagcxDeviceMemoryOrderRelease);
    }
    __syncthreads();
    if (threadIdx.x == 1u) {
      if (A::load(&flag[i], flagcxDeviceMemoryOrderAcquire) != i + 1u)
        c[1] += 1;
      if (A::load(&data[i], flagcxDeviceMemoryOrderAcquire) != i + 1u)
        c[0] += 1;
    }
    __syncthreads();
  }
  if (threadIdx.x == 0u)
    c[3] = 1;
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
  int id;
  const char *name;
  int expectViolations; // 1 = must be many, 0 = must be none
};

int main() {
  const Cell cells[] = {
      {0, "NEG(flag-before-data)", 1},
      {1, "POS(barrier-ordered)", 0},
      {2, "RELEASE/RELAXED", 0},
      {3, "RELAXED/RELAXED", 1},
      {4, "RELEASE/ACQUIRE", 0},
      {5, "RELAXED/ACQUIRE", 1},
  };
  const int kCells = (int)(sizeof(cells) / sizeof(cells[0]));

  unsigned *dData = NULL, *dFlag = NULL;
  int *dC = NULL;
  CK(cudaMalloc(&dData, (size_t)ITER * sizeof(unsigned)));
  CK(cudaMalloc(&dFlag, (size_t)ITER * sizeof(unsigned)));
  CK(cudaMalloc(&dC, 8 * sizeof(int)));
  printf("=== iter=%u window=%u (two blocks, one thread each) ===\n", ITER, WINDOW);
  fflush(stdout);

  for (int k = 0; k < kCells; ++k) {
    int got[8] = {0};
    CK(cudaMemset(dC, 0, 8 * sizeof(int)));
    CK(cudaMemset(dData, 0, (size_t)ITER * sizeof(unsigned)));
    CK(cudaMemset(dFlag, 0, (size_t)ITER * sizeof(unsigned)));

    switch (cells[k].id) {
      case 0:
        mpLitmusKernel<flagcxDeviceMemoryOrderRelease,
                   flagcxDeviceMemoryOrderRelaxed, 1>
            <<<2, 1>>>(dData, dFlag, dC, ITER, WINDOW);
        break;
      case 1:
        barrierOrderedKernel<<<1, 2>>>(dData, dFlag, dC, ITER);
        break;
      case 2:
        mpLitmusKernel<flagcxDeviceMemoryOrderRelease,
                   flagcxDeviceMemoryOrderRelaxed, 0>
            <<<2, 1>>>(dData, dFlag, dC, ITER, WINDOW);
        break;
      case 3:
        mpLitmusKernel<flagcxDeviceMemoryOrderRelaxed,
                   flagcxDeviceMemoryOrderRelaxed, 0>
            <<<2, 1>>>(dData, dFlag, dC, ITER, WINDOW);
        break;
      case 4:
        mpLitmusKernel<flagcxDeviceMemoryOrderRelease,
                   flagcxDeviceMemoryOrderAcquire, 0>
            <<<2, 1>>>(dData, dFlag, dC, ITER, WINDOW);
        break;
      default:
        mpLitmusKernel<flagcxDeviceMemoryOrderRelaxed,
                   flagcxDeviceMemoryOrderAcquire, 0>
            <<<2, 1>>>(dData, dFlag, dC, ITER, WINDOW);
        break;
    }
    CK(cudaDeviceSynchronize());
    CK(cudaMemcpy(got, dC, 8 * sizeof(int), cudaMemcpyDeviceToHost));

    const int many = got[0] > 0;
    const int ok = (many == cells[k].expectViolations) && (got[1] == 0) &&
                   (got[3] == 1);
    printf("PROBE|load-store-order|%-24s|violations=%-8d|timeouts=%-3d|raced=%-8d|"
           "producer_done=%d|expect=%s|%s\n",
           cells[k].name, got[0], got[1], got[2], got[3],
           cells[k].expectViolations ? "many" : "none", ok ? "OK" : "MISMATCH");
    fflush(stdout);
  }

  cudaFree(dData);
  cudaFree(dFlag);
  cudaFree(dC);
  printf("LOAD_STORE_ORDER_DONE\n");
  return 0;
}
