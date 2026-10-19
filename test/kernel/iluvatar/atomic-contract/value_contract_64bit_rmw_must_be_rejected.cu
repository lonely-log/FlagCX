// Negative compile: the six RMW members must reject a 64-bit operand at
// compile time. Each function below must trigger the same static_assert, so the
// build is expected to FAIL with exactly six copies of the message. That is the
// only way to test a compile-time contract.
#include "platform_traits.h"

typedef PlatformTraits<IluvatarPlatform>::Atomic A;
typedef flagcxDeviceMemoryOrder_t Ord;

__global__ void neg_fetchAdd(unsigned long long *p) {
  (void)A::fetchAdd(p, 1ull, flagcxDeviceMemoryOrderRelaxed);
}
__global__ void neg_fetchSub(unsigned long long *p) {
  (void)A::fetchSub(p, 1ull, flagcxDeviceMemoryOrderRelaxed);
}
__global__ void neg_fetchOr(unsigned long long *p) {
  (void)A::fetchOr(p, 1ull, flagcxDeviceMemoryOrderRelaxed);
}
__global__ void neg_fetchAnd(unsigned long long *p) {
  (void)A::fetchAnd(p, 1ull, flagcxDeviceMemoryOrderRelaxed);
}
__global__ void neg_exchange(unsigned long long *p) {
  (void)A::exchange(p, 1ull, flagcxDeviceMemoryOrderRelaxed);
}
__global__ void neg_compareExchange(unsigned long long *p) {
  unsigned long long e = 0ull;
  (void)A::compareExchange(p, e, 1ull, flagcxDeviceMemoryOrderRelaxed);
}
