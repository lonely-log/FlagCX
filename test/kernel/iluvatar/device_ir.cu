// Reuse the complete IR test surface. The build supplies CoreX's explicit
// device-pass marker to both compiler passes.
#include "../nvidia/device_ir.cu"

// Launch bridge for the DeviceAdaptor launchKernel slot: expose an IR kernel as
// a plain address so a host test can launch it through the slot's
// (void *func, void **args) contract instead of <<<>>>. Kept in this file so the
// shared nvidia source stays untouched.
void *flagcxTestKernelCommQueriesSPtr(void) {
  return (void *)kernelCommQueriesS;
}

// Reference bridge for the same kernel, so the host test can run the <<<>>>
// path and compare against the slot path.
//
// The host test cannot call launchKernelCommQueriesS() directly: that launcher
// takes `const void FLAGCX_IR_GLOBAL_PTR *`, which on CoreX is
// address_space(1), while this translation unit is compiled without
// FLAGCX_ILUVATAR_DEVICE_COMPILE and therefore sees a generic `const void *`.
// Different parameter types mean a different mangled name, so the direct call
// links against nothing. This wrapper takes the generic pointer the host has,
// adds the address space here (where the definition is visible), and forwards.
void flagcxTestLaunchCommQueriesS(const void *devCommPtr, int *devResults,
                                 flagcxStream_t stream) {
  launchKernelCommQueriesS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr,
                           devResults, stream);
}


// ---------------------------------------------------------------------------
// Host-callable IR launcher bridges (CoreX address-space ABI)
//
// Every launchKernel*S declaration in test/kernel/include/device_ir.h takes at
// least one `const void FLAGCX_IR_GLOBAL_PTR *` parameter. In this translation
// unit FLAGCX_ILUVATAR_DEVICE_COMPILE is defined, so that qualifier expands to
// address_space(1) and each launcher's mangled name carries AS1.
//
// The T3/T4 programs (test/unittest/device_api/test_device_ir_*.cpp) are
// compiled by the host compiler, which never defines the device-pass marker and
// therefore sees a generic `const void *`. Same function name with a different
// parameter type means a different mangled name, so those direct calls resolve
// to nothing and the link fails with 45 "undefined reference to launchKernel*S".
//
// Give each launcher a generic-pointer overload in this file, where the AS1
// spelling is legal. Overload resolution keeps the two apart: there is no
// address_space(1) <-> generic conversion (see device_utils.h), so a host call
// binds to the plain overload while the bridge's explicitly cast call binds to
// the AS1 one. The AS1 IR surface and every shared test source stay untouched.
// ---------------------------------------------------------------------------

void launchKernelCommQueriesS(const void *devCommPtr, int *devResults, flagcxStream_t stream) {
  launchKernelCommQueriesS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResults, stream);
}
void launchKernelCoopGroupsS(const void *devCommPtr, int *devResults, flagcxStream_t stream) {
  launchKernelCoopGroupsS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResults, stream);
}
void launchKernelTeamQueriesS(const void *devCommPtr, int *devResults, flagcxStream_t stream) {
  launchKernelTeamQueriesS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResults, stream);
}
void launchKernelLocalPointerS(const void *devMemPtr, void *rawBuff, int *devResults, flagcxStream_t stream) {
  launchKernelLocalPointerS((const void FLAGCX_IR_GLOBAL_PTR *)devMemPtr, rawBuff, devResults, stream);
}
void launchKernelIntraPointerS(const void *devCommPtr, const void *devMemPtr, float *devOutput, int count, flagcxStream_t stream) {
  launchKernelIntraPointerS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)devMemPtr, devOutput, count, stream);
}
void launchKernelPeerPointerS(const void *devCommPtr, const void *devMemPtr, float *devOutput, int count, flagcxStream_t stream) {
  launchKernelPeerPointerS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)devMemPtr, devOutput, count, stream);
}
void launchKernelIntraBarrierSyncS(const void *devCommPtr, const void *devMemPtr, float *buffer, float *output, int N, flagcxStream_t stream) {
  launchKernelIntraBarrierSyncS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)devMemPtr, buffer, output, N, stream);
}
void launchKernelIntraBarrierArriveWaitS(const void *devCommPtr, const void *devMemPtr, float *buffer, float *output, int N, flagcxStream_t stream) {
  launchKernelIntraBarrierArriveWaitS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)devMemPtr, buffer, output, N, stream);
}
void launchKernelIntraAllReduceS(const void *devCommPtr, const void *devMemPtr, float *buffer, int count, flagcxStream_t stream) {
  launchKernelIntraAllReduceS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)devMemPtr, buffer, count, stream);
}
void launchKernelNetGetFromCommS(const void *devCommPtr, int *devResults, flagcxStream_t stream) {
  launchKernelNetGetFromCommS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResults, stream);
}
void launchKernelNetResetS(const void *devCommPtr, int *devResults, flagcxStream_t stream) {
  launchKernelNetResetS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResults, stream);
}
void launchKernelNetPutSignalIncS(const void *devCommPtr, const void *sendMemPtr, const void *recvMemPtr, size_t countPerPeer, flagcxStream_t stream) {
  launchKernelNetPutSignalIncS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)sendMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)recvMemPtr, countPerPeer, stream);
}
void launchKernelNetPutSignalAddS(const void *devCommPtr, const void *sendMemPtr, const void *recvMemPtr, size_t countPerPeer, flagcxStream_t stream) {
  launchKernelNetPutSignalAddS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)sendMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)recvMemPtr, countPerPeer, stream);
}
void launchKernelNetCounterPipelineS(const void *devCommPtr, const void *sendMemPtr, const void *recvMemPtr, size_t countPerPeer, flagcxStream_t stream) {
  launchKernelNetCounterPipelineS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)sendMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)recvMemPtr, countPerPeer, stream);
}
void launchKernelNetFlushDecoupleS(const void *devCommPtr, const void *sendMemPtr, const void *recvMemPtr, size_t countPerPeer, flagcxStream_t stream) {
  launchKernelNetFlushDecoupleS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)sendMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)recvMemPtr, countPerPeer, stream);
}
void launchKernelNetPutValueS(const void *devCommPtr, const void *recvMemPtr, size_t putValBase, flagcxStream_t stream) {
  launchKernelNetPutValueS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)recvMemPtr, putValBase, stream);
}
void launchKernelNetSignalS(const void *devCommPtr, flagcxStream_t stream) {
  launchKernelNetSignalS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, stream);
}
void launchKernelNetWaitSignalFlushS(const void *devCommPtr, flagcxStream_t stream) {
  launchKernelNetWaitSignalFlushS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, stream);
}
void launchKernelInterBarrierS(const void *devCommPtr, int *devResults, int nIters, flagcxStream_t stream) {
  launchKernelInterBarrierS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResults, nIters, stream);
}
void launchKernelWorldBarrierS(const void *devCommPtr, flagcxStream_t stream) {
  launchKernelWorldBarrierS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, stream);
}
void launchKernelNetOneSidedAlltoAllS(const void *devCommPtr, const void *sendMemPtr, const void *recvMemPtr, size_t countPerPeer, flagcxStream_t stream) {
  launchKernelNetOneSidedAlltoAllS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)sendMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)recvMemPtr, countPerPeer, stream);
}
void launchKernelDevPutIntraWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, bytes, stream);
}
void launchKernelDevGetIntraWorldS(const void *devCommPtr, const void *remoteMemPtr, const void *localMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevGetIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)remoteMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)localMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutSignalWaitIntraWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutSignalWaitIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, bytes, stream);
}
void launchKernelDevBarrierIntraWorldS(const void *devCommPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevBarrierIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResult, stream);
}
void launchKernelDevPutInterWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, bytes, stream);
}
void launchKernelDevGetInterWorldS(const void *devCommPtr, const void *remoteMemPtr, const void *localMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevGetInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)remoteMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)localMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutSignalWaitInterWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutSignalWaitInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, bytes, stream);
}
void launchKernelDevBarrierInterWorldS(const void *devCommPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevBarrierInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResult, stream);
}
void launchKernelDevPutRSigIntraWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutRSigIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutRSigInterWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutRSigInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutValueIntraWorldS(const void *devCommPtr, const void *dstMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutValueIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutValueInterWorldS(const void *devCommPtr, const void *dstMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutValueInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutValueRSigIntraWorldS(const void *devCommPtr, const void *dstMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutValueRSigIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutValueRSigInterWorldS(const void *devCommPtr, const void *dstMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutValueRSigInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutCounterIntraWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutCounterIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, bytes, stream);
}
void launchKernelDevPutCounterInterWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, size_t bytes, flagcxStream_t stream) {
  launchKernelDevPutCounterInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, bytes, stream);
}
void launchKernelDevSignalStandaloneIntraWorldS(const void *devCommPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevSignalStandaloneIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResult, stream);
}
void launchKernelDevSignalStandaloneInterWorldS(const void *devCommPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevSignalStandaloneInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResult, stream);
}
void launchKernelDevSignalShadowFlushIntraWorldS(const void *devCommPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevSignalShadowFlushIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResult, stream);
}
void launchKernelDevSignalShadowFlushInterWorldS(const void *devCommPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevSignalShadowFlushInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResult, stream);
}
void launchKernelDevBarrierArriveWaitIntraWorldS(const void *devCommPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevBarrierArriveWaitIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResult, stream);
}
void launchKernelDevBarrierArriveWaitInterWorldS(const void *devCommPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevBarrierArriveWaitInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, devResult, stream);
}
void launchKernelDevTeamResolutionIntraWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevTeamResolutionIntraWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, stream);
}
void launchKernelDevTeamResolutionInterWorldS(const void *devCommPtr, const void *dstMemPtr, const void *srcMemPtr, int *devResult, flagcxStream_t stream) {
  launchKernelDevTeamResolutionInterWorldS((const void FLAGCX_IR_GLOBAL_PTR *)devCommPtr, (const void FLAGCX_IR_GLOBAL_PTR *)dstMemPtr, (const void FLAGCX_IR_GLOBAL_PTR *)srcMemPtr, devResult, stream);
}
