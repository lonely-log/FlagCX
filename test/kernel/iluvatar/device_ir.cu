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
