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

// ---------------------------------------------------------------------------
// S3 wave/mask micro-benchmark.
//
// The partial-mask policy cannot be set from the headers: nothing states how
// wide __activemask() really is, and nothing states whether the no-argument
// __syncwarp() waits for the whole wave or only for the lanes that arrive. The
// plan asks for a probe with a watchdog; measuring waiting time answers the
// same question without ever being able to wedge a shared device, because every
// lane reaches the barrier and the upper half merely arrives late.
//
//   results[0] low word of __activemask()
//   results[1] popcount of __activemask()      (expect the wave width, 64)
//   results[2] high word of __activemask()     (expect -1, i.e. lanes 32-63 set)
//   results[3] cycles lane 0 spent in the barrier (the early half)
//   results[4] cycles lane 32 spent altogether   (the late half)
//   results[5] 0x1234 written by lane 63 after the barrier
// ---------------------------------------------------------------------------
#define FLAGCX_ILUVATAR_PROBE_SLOW_CYCLES 2000000

__global__ void flagcxIluvatarSyncwarpProbeKernel(int *results) {
  const int t = FLAGCX_THREAD_IDX_X;
  const unsigned long long mask = (unsigned long long)__activemask();
  if (t == 0) {
    results[0] = (int)(mask & 0xffffffffull);
    results[1] = (int)__popcll(mask);
    results[2] = (int)(mask >> 32);
  }

  const long long start = clock64();
  if (t >= 32) {
    const long long spinStart = clock64();
    // Hard iteration cap on top of the clock deadline: if the device clock ever
    // stops advancing, an uncapped deadline-spin never returns and wedges the
    // GPU for every other user until the process is force-killed.
    int guard = 0;
    while (++guard < 20000000 && clock64() - spinStart <
                                     (long long)
                                         FLAGCX_ILUVATAR_PROBE_SLOW_CYCLES) {
    }
  }
  __syncwarp();
  const long long elapsed = clock64() - start;
  if (t == 0) {
    results[3] = (int)elapsed;
  }
  if (t == 32) {
    results[4] = (int)elapsed;
  }
  if (t == 63) {
    results[5] = 0x1234;
  }
}

flagcxResult_t flagcxTestLaunchSyncwarpProbe(int *results,
                                             flagcxStream_t stream) {
  if (results == nullptr || stream == nullptr) {
    return flagcxInvalidArgument;
  }
  flagcxIluvatarSyncwarpProbeKernel<<<1, FLAGCX_SIMT_WIDTH, 0, stream->base>>>(
      results);
  cudaError_t err = cudaGetLastError();
  return (err == cudaSuccess) ? flagcxSuccess : flagcxUnhandledDeviceError;
}
