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
