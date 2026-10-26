/*************************************************************************
 * Copyright (c) 2026 BAAI. All rights reserved.
 *
 * Device-compiled helpers for Iluvatar/CoreX Device API host integration.
 ************************************************************************/

#include "device_api/flagcx_device_internal.h"
#include "device_api/flagcx_device_core.h"
#include <cuda_runtime.h>
#include <sched.h>
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>
#include <stdlib.h>
#include <new>

extern "C" size_t flagcxDevNetSizeOf() { return sizeof(flagcxDevNet); }

static __global__ void flagcxDevNetConstructKernel(flagcxDevNet *nets,
                                                   flagcxDevComm *comm,
                                                   int count) {
  int i = threadIdx.x + blockIdx.x * blockDim.x;
  if (i < count)
    ::new (&nets[i]) flagcxDevNet(*comm, i);
}

extern "C" void flagcxDevNetLaunchConstruct(void *devNets, void *devComm,
                                            int count, void *stream) {
  if (count <= 0 || devNets == nullptr || devComm == nullptr)
    return;
  int threads = count < 256 ? count : 256;
  int blocks = (count + threads - 1) / threads;
  flagcxDevNetConstructKernel<<<blocks, threads, 0, (cudaStream_t)stream>>>(
      static_cast<flagcxDevNet *>(devNets),
      static_cast<flagcxDevComm *>(devComm), count);
}


/* ==========================================================================
 * Host-side peer-memory transfer for the socket one-sided emulation.
 *
 * The socket transport carries no remote memory keys, so an inter-node peer's
 * registered region is reached through an IPC mapping instead. Data movement
 * and remote signal updates must run on a stream that does not serialize
 * behind the application kernel: while a peer's kernel spins in waitSignal on
 * its own stream, the legacy default stream would block forever, so use a
 * dedicated non-blocking stream.
 * ========================================================================== */

// One transfer stream per posting thread. Sharing a single stream makes every
// cudaStreamSynchronize wait for the other proxy contexts' transfers as well.
#define FLAGCX_SOCK_CHUNK (64u * 1024u)

static __thread cudaStream_t flagcxSockDevStream = nullptr;
static __thread void *flagcxSockBounce = nullptr;
static __thread size_t flagcxSockBounceSize = 0;

// Wait for this thread's stream without forcing the device to drain other
// streams. cudaStreamSynchronize is not safe here: it can wait for an
// application kernel that is itself waiting for this transfer.
static cudaError_t flagcxSockStreamPoll(cudaStream_t stream) {
  for (;;) {
    cudaError_t err = cudaStreamQuery(stream);
    if (err == cudaSuccess)
      return cudaSuccess;
    if (err != cudaErrorNotReady)
      return err;
  }
}

static cudaError_t flagcxSockBounceBuffer(void **out, size_t size) {
  if (flagcxSockBounceSize >= size && flagcxSockBounce != nullptr) {
    *out = flagcxSockBounce;
    return cudaSuccess;
  }
  size_t want = size < (1u << 20) ? (1u << 20) : size;
  if (flagcxSockBounce != nullptr) {
    (void)cudaFreeHost(flagcxSockBounce);
    flagcxSockBounce = nullptr;
    flagcxSockBounceSize = 0;
  }
  cudaError_t err = cudaMallocHost(&flagcxSockBounce, want);
  if (err != cudaSuccess) {
    flagcxSockBounce = nullptr;
    return err;
  }
  flagcxSockBounceSize = want;
  *out = flagcxSockBounce;
  return cudaSuccess;
}

static int flagcxSockDevStreamEnsure(cudaStream_t *out) {
  if (flagcxSockDevStream == nullptr) {
    // Highest priority: the one-thread signal kernel must be scheduled while
    // the application kernel is spinning in waitSignal, otherwise the write it
    // is waiting for never lands.
    int leastPriority = 0;
    int greatestPriority = 0;
    cudaStream_t stream = nullptr;
    cudaError_t err = cudaDeviceGetStreamPriorityRange(&leastPriority,
                                                      &greatestPriority);
    if (err == cudaSuccess) {
      err = cudaStreamCreateWithPriority(&stream, cudaStreamNonBlocking,
                                        greatestPriority);
    }
    if (err != cudaSuccess || stream == nullptr) {
      stream = nullptr;
      err = cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
    }
    if (err != cudaSuccess) {
      flagcxSockDevStream = nullptr;
      return (int)err;
    }
    flagcxSockDevStream = stream;
  }
  *out = flagcxSockDevStream;
  return 0;
}

// Exact allocation range for a device pointer. ixcuda reports
// flagcxNotSupported for the adaptor's getAddressRange, so the IPC export
// cannot otherwise tell where the registered region sits inside its
// allocation.
extern "C" int flagcxSockDevMemRange(void *ptr, void **base, size_t *size) {
  if (ptr == nullptr || base == nullptr || size == nullptr)
    return -1;
  CUdeviceptr rangeBase = 0;
  size_t rangeSize = 0;
  if (cuMemGetAddressRange(&rangeBase, &rangeSize, (CUdeviceptr)ptr) !=
      CUDA_SUCCESS)
    return -1;
  *base = reinterpret_cast<void *>(rangeBase);
  *size = rangeSize;
  return 0;
}

// copyType follows flagcxMemcpyType_t.
extern "C" int flagcxSockDevTransfer(void *dst, const void *src, size_t size,
                                    int copyType, void *signalPtr,
                                    unsigned long long signalValue) {
  static const cudaMemcpyKind kinds[3] = {cudaMemcpyHostToDevice,
                                          cudaMemcpyDeviceToHost,
                                          cudaMemcpyDeviceToDevice};
  if (copyType < 0 || copyType > 2)
    return -1;
  if (dst == nullptr && signalPtr == nullptr)
    return -1;
  cudaStream_t stream = nullptr;
  int rc = flagcxSockDevStreamEnsure(&stream);
  if (rc != 0)
    return rc;
  static __thread int xseq = -1;
  const int xtrace = getenv("FLAGCX_XTRACE") != NULL;
  int xid = 0;
  if (xtrace) {
    xid = ++xseq;
    fprintf(stderr,
            "[XFER] pid=%d id=%d s=enter size=%zu kind=%d dst=%p src=%p "
            "sig=%p val=%llu\n",
            (int)getpid(), xid, size, copyType, dst, src, signalPtr,
            (unsigned long long)signalValue);
    fflush(stderr);
  }
  if (size > 0 && src != nullptr && dst != nullptr) {
    cudaError_t err = cudaSuccess;
    if (copyType == 2 && dst == src)
      return 0;
    if (copyType != 2) {
      // Host-direction transfer: one blocking hop, no bounce.
      err = cudaMemcpy(dst, src, size, kinds[copyType]);
      if (err != cudaSuccess)
        return (int)err;
      if (xtrace) {
        fprintf(stderr, "[XFER] pid=%d id=%d s=direct_copy_done kind=%d\n",
                (int)getpid(), xid, copyType);
        fflush(stderr);
      }
      if (signalPtr == nullptr)
        return 0;
    } else {
    // Bounce through pinned host memory in bounded chunks: every hop stays on
    // the local device, and no single host-issued copy is big enough to stall
    // behind a resident application kernel.
    void *bounce = nullptr;
    err = flagcxSockBounceBuffer(&bounce, FLAGCX_SOCK_CHUNK);
    if (err != cudaSuccess)
      return (int)err;
    const char *srcBytes = (const char *)src;
    char *dstBytes = (char *)dst;
    size_t done = 0;
    while (done < size) {
      size_t n = size - done;
      if (n > FLAGCX_SOCK_CHUNK)
        n = FLAGCX_SOCK_CHUNK;
      err = cudaMemcpy(bounce, srcBytes + done, n, cudaMemcpyDeviceToHost);
      if (err != cudaSuccess)
        return (int)err;
      err = cudaMemcpy(dstBytes + done, bounce, n, cudaMemcpyHostToDevice);
      if (err != cudaSuccess)
        return (int)err;
      done += n;
    }
    if (xtrace) {
      fprintf(stderr, "[XFER] pid=%d id=%d s=bounce_done size=%zu\n",
              (int)getpid(), xid, size);
      fflush(stderr);
    }
    }
  }
  if (signalPtr == nullptr)
    return 0;
  // Remote signal update: read/add/write with synchronous copies, the one
  // transport primitive that always makes progress here. The proxy thread
  // serialises its own posts and each signal word has a single writer per
  // phase, so this preserves the counter semantics.
  // Several processes may add to the same signal word (T2 PutSigInc: every
  // inter peer increments the receiver's slot 0), so the read/add/write has to
  // be serialised across processes. A GPU helper kernel would be atomic too but
  // waiting on it from a proxy thread can wedge, so use an inter-process lock.
  // Signal traffic is a handful of words per round, so one lock file is enough.
  int lockFd = open("/dev/shm/flagcx-socklock", O_CREAT | O_RDWR, 0644);
  if (lockFd >= 0)
    (void)flock(lockFd, LOCK_EX);
  unsigned long long current = 0;
  cudaError_t err =
      cudaMemcpy(&current, signalPtr, sizeof(current), cudaMemcpyDeviceToHost);
  if (err == cudaSuccess) {
    const unsigned long long updated = current + signalValue;
    err = cudaMemcpy(signalPtr, &updated, sizeof(updated),
                     cudaMemcpyHostToDevice);
  }
  if (lockFd >= 0) {
    (void)flock(lockFd, LOCK_UN);
    close(lockFd);
  }
  if (xtrace) {
    fprintf(stderr, "[XFER] pid=%d id=%d s=signal_done cur=%llu err=%d\n",
            (int)getpid(), xid, current, (int)err);
    fflush(stderr);
  }
  return (int)err;
}
