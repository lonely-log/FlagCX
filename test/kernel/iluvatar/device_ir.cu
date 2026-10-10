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
