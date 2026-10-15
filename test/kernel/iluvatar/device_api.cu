// Reuse the platform-neutral CUDA-compatible native Device API tests. The
// build supplies CoreX's explicit device-pass marker to both compiler passes.
#include "../nvidia/device_api.cu"

// Declarations for this file's launchers, including flagcxTestLaunchKernelFn
// used by the launch bridges below.
#include "device_api.h"

// Compile every CoreX-supported RMW operation. Aligned uint64_t load/store is
// covered by the public tests; RMW intentionally uses the 32-bit domain.
__global__ void flagcxIluvatarAtomicContractKernel(uint32_t *value) {
  if (FLAGCX_THREAD_IDX_X != 0)
    return;
  uint32_t expected =
      DeviceAPI::Atomic::load(value, flagcxDeviceMemoryOrderAcquire);
  DeviceAPI::Atomic::store(value, expected, flagcxDeviceMemoryOrderRelease);
  DeviceAPI::Atomic::fetchAdd(value, uint32_t{1},
                              flagcxDeviceMemoryOrderAcqRel);
  DeviceAPI::Atomic::fetchSub(value, uint32_t{1},
                              flagcxDeviceMemoryOrderAcqRel);
  DeviceAPI::Atomic::fetchOr(value, uint32_t{1}, flagcxDeviceMemoryOrderAcqRel);
  DeviceAPI::Atomic::fetchAnd(value, ~uint32_t{0},
                              flagcxDeviceMemoryOrderAcqRel);
  DeviceAPI::Atomic::exchange(value, expected, flagcxDeviceMemoryOrderAcqRel);
  DeviceAPI::Atomic::compareExchange(value, expected, expected,
                                     flagcxDeviceMemoryOrderAcqRel);
}

// This kernel is intentionally not part of the normal success path. A CoreX
// runtime test launches it separately and expects a kernel error, proving that
// an unsupported partial mask fails rather than widening to a full-wave
// barrier and hanging.
__global__ void flagcxIluvatarUnsupportedCoopKernel() {
  flagcxCoopLanes partial(flagcxLaneMask_t{3});
  partial.sync();
}

// ---------------------------------------------------------------------------
// Launch bridges for the DeviceAdaptor launchKernel slot.
//
// launchKernel takes a runtime kernel address plus a kernelParams array, both of
// which <<<>>> resolves implicitly at compile time, so this translation unit has
// to expose them explicitly. The launch function itself is passed in by the
// caller: that keeps this device translation unit free of the host-only
// adaptor.h header, and lets the test decide exactly which slot it exercises.
// ---------------------------------------------------------------------------

// Parameters passed by value (flagcxDevMem / flagcxDevComm) require the args
// array to hold the addresses of the host-side values.
flagcxResult_t flagcxTestLaunchCommQueriesViaAdaptor(
    flagcxTestLaunchKernelFn launchKernel, flagcxDevMem_t devMem,
    flagcxDevComm_t devComm, int *results, unsigned int blockX,
    unsigned int gridX, flagcxStream_t stream) {
  if (launchKernel == nullptr || devMem == nullptr || devComm == nullptr ||
      results == nullptr) {
    return flagcxInvalidArgument;
  }
  flagcxDevMem dm(*devMem);
  flagcxDevComm dc(*devComm);
  void *args[] = {&dm, &dc, &results};
  return launchKernel((void *)flagcxIntraTestCommQueriesKernel, blockX, 1, 1,
                      gridX, 1, 1, args, 0, stream, nullptr);
}

// Plain kernel address, for kernels whose parameters are already plain
// pointers and can therefore be marshalled by the host test itself.
void *flagcxTestKernelCoopGroupsPtr(void) {
  return (void *)flagcxIntraTestCoopGroupsKernel;
}

// Launchers for the two CoreX-only kernels, so that the 32-bit RMW contract and
// the "unsupported partial mask must fail instead of hanging" behaviour can be
// exercised by a test. NOTE: the trap in the second kernel is asynchronous, so
// the launcher only reports launch-time errors; the caller must synchronise
// with its own timeout to observe the failure.
flagcxResult_t flagcxTestLaunchAtomicContract(uint32_t *value,
                                              flagcxStream_t stream) {
  if (value == nullptr) {
    return flagcxInvalidArgument;
  }
  flagcxIluvatarAtomicContractKernel<<<1, 1, 0, stream->base>>>(value);
  cudaError_t err = cudaGetLastError();
  return (err == cudaSuccess) ? flagcxSuccess : flagcxUnhandledDeviceError;
}

flagcxResult_t flagcxTestLaunchUnsupportedCoop(flagcxStream_t stream) {
  flagcxIluvatarUnsupportedCoopKernel<<<1, FLAGCX_SIMT_WIDTH, 0,
                                        stream->base>>>();
  cudaError_t err = cudaGetLastError();
  return (err == cudaSuccess) ? flagcxSuccess : flagcxUnhandledDeviceError;
}

// ===========================================================================
// S3 / D2: coop sync matrix - one shape, three cooperative groups, two delay
// positions.
//
// Block of 128 threads = two complete 64-lane waves on CoreX, and every cell
// runs the same code path except for the two template parameters:
//   COOP   BLOCK | TILESPAN(0, blockDim/simtWidth, 0) | WARP
//   DELAY  other-wave (wave 1 late) | same-wave-upper (tid 32..63 late)
//
// Every thread calls its group sync exactly once, so no cell can deadlock, and
// tid 0 / tid 31 always belong to the early set: their elapsed time says whether
// the sync held them until the delayed threads arrived.
//   delay-other-wave       -> separates block scope from wave scope
//   delay-same-wave-upper  -> separates "a wave sync really waits for its own
//                             lanes" from "it does nothing"
//
// Self-checks: the delayed thread's own elapsed proves the spin happened, and
// tid 127 writes a sentinel after the sync so a deadlock cannot look like data.
//
// results: [0]=tid 0  [1]=tid 31  [2]=delayed thread  [3]=sentinel
// ===========================================================================
#if !defined(FLAGCX_ILUVATAR_PROBE_SLOW_CYCLES)
// Self-contained on branches that do not carry the S3 header changes.
#define FLAGCX_ILUVATAR_PROBE_SLOW_CYCLES 2000000
#endif

template <int COOP, int DELAY>
__global__ void flagcxIluvatarCoopMatrixProbeKernel(int *results) {
  const int t = FLAGCX_THREAD_IDX_X;
  const int wave = t / FLAGCX_SIMT_WIDTH;
  const bool delayed =
      (DELAY == FLAGCX_TEST_COOP_MATRIX_DELAY_OTHER_WAVE)
          ? (wave == 1)
          : (wave == 0 && t >= FLAGCX_SIMT_WIDTH / 2);
  const long long start = clock64();
  if (delayed) {
    const long long spinStart = clock64();
    int guard = 0;
    while (++guard < 20000000 && clock64() - spinStart <
                                     (long long)
                                         FLAGCX_ILUVATAR_PROBE_SLOW_CYCLES) {
    }
  }
  if (COOP == FLAGCX_TEST_COOP_MATRIX_BLOCK) {
    flagcxCoopBlock{}.sync();
  } else if (COOP == FLAGCX_TEST_COOP_MATRIX_TILESPAN) {
    flagcxCoopTileSpan(0, (int)(FLAGCX_BLOCK_DIM_X / FLAGCX_SIMT_WIDTH), 0)
        .sync();
  } else {
    flagcxCoopWarp{}.sync();
  }
  const long long elapsed = clock64() - start;
  if (t == 0)
    results[0] = (int)elapsed;
  if (t == FLAGCX_SIMT_WIDTH / 2 - 1)
    results[1] = (int)elapsed;
  const int delayedTid = (DELAY == FLAGCX_TEST_COOP_MATRIX_DELAY_OTHER_WAVE)
                             ? FLAGCX_SIMT_WIDTH
                             : FLAGCX_SIMT_WIDTH / 2;
  if (t == delayedTid)
    results[2] = (int)elapsed;
  if (t == 2 * FLAGCX_SIMT_WIDTH - 1)
    results[3] = 0xABCD;
}

flagcxResult_t flagcxTestLaunchCoopMatrixProbe(int coop, int delay, int *results,
                                               flagcxStream_t stream) {
  if (results == nullptr || stream == nullptr)
    return flagcxInvalidArgument;
  const int block = 2 * FLAGCX_SIMT_WIDTH; // two complete waves
  const int other = FLAGCX_TEST_COOP_MATRIX_DELAY_OTHER_WAVE;
  const int same = FLAGCX_TEST_COOP_MATRIX_DELAY_SAME_WAVE_UPPER;
  if (coop == FLAGCX_TEST_COOP_MATRIX_BLOCK) {
    if (delay == other)
      flagcxIluvatarCoopMatrixProbeKernel<FLAGCX_TEST_COOP_MATRIX_BLOCK, other>
          <<<1, block, 0, stream->base>>>(results);
    else if (delay == same)
      flagcxIluvatarCoopMatrixProbeKernel<FLAGCX_TEST_COOP_MATRIX_BLOCK, same>
          <<<1, block, 0, stream->base>>>(results);
    else
      return flagcxInvalidArgument;
  } else if (coop == FLAGCX_TEST_COOP_MATRIX_TILESPAN) {
    if (delay == other)
      flagcxIluvatarCoopMatrixProbeKernel<FLAGCX_TEST_COOP_MATRIX_TILESPAN,
                                          other>
          <<<1, block, 0, stream->base>>>(results);
    else if (delay == same)
      flagcxIluvatarCoopMatrixProbeKernel<FLAGCX_TEST_COOP_MATRIX_TILESPAN,
                                          same>
          <<<1, block, 0, stream->base>>>(results);
    else
      return flagcxInvalidArgument;
  } else if (coop == FLAGCX_TEST_COOP_MATRIX_WARP) {
    if (delay == other)
      flagcxIluvatarCoopMatrixProbeKernel<FLAGCX_TEST_COOP_MATRIX_WARP, other>
          <<<1, block, 0, stream->base>>>(results);
    else if (delay == same)
      flagcxIluvatarCoopMatrixProbeKernel<FLAGCX_TEST_COOP_MATRIX_WARP, same>
          <<<1, block, 0, stream->base>>>(results);
    else
      return flagcxInvalidArgument;
  } else {
    return flagcxInvalidArgument;
  }
  cudaError_t err = cudaGetLastError();
  return (err == cudaSuccess) ? flagcxSuccess : flagcxUnhandledDeviceError;
}
