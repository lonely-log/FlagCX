// Minimal public-API reproducer: self-loopback send/recv, one pair vs two pairs.
//
// Phase 1 = one self pair   (send 100 + recv 100 to own rank)
// Phase 2 = two self pairs  (send 100 + send 200 + recv 200 + recv 100 to own rank)
//
// It uses only the public API (flagcx.h): flagcxCommInitRank, flagcxSend/Recv,
// flagcxGroupStart/End. No internal headers, no perf framework.
//
// Build:
//   ROOT=/usr/local/corex-5.1.0/FlagCX
//   mpicxx -std=c++17 -g -DOMPI_SKIP_MPICXX -o repro_two_self_pairs repro_two_self_pairs.cpp \
//     -I$ROOT/flagcx/include -L$ROOT/build/lib -Wl,--no-as-needed \
//     -Wl,-rpath,$ROOT/build/lib -lflagcx
//
// Run (PHASE=1|2|both, default both):
//   export FLAGCX_USE_HETERO_COMM=1 FLAGCX_MEM_ENABLE=1 FLAGCX_VMM_ENABLE=0
//   PHASE=1 timeout 60 mpirun --allow-run-as-root -np 2 \
//     -x PHASE -x FLAGCX_USE_HETERO_COMM -x FLAGCX_MEM_ENABLE -x FLAGCX_VMM_ENABLE \
//     -x CUDA_VISIBLE_DEVICES ./repro_two_self_pairs
//
// Expected with the suspected bug: "phase 1 OK" is printed, then it hangs
// before "phase 2 OK" (exit=124 from timeout).

#include "flagcx.h"

#include <mpi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int gRank = 0;

#define CHECK(call)                                                            \
  do {                                                                         \
    flagcxResult_t res_ = (call);                                              \
    if (res_ != flagcxSuccess) {                                               \
      fprintf(stderr, "[rank %d] %s failed (res=%d)\n", gRank, #call,          \
              (int)res_);                                                      \
      MPI_Abort(MPI_COMM_WORLD, 1);                                            \
    }                                                                          \
  } while (0)

static void fillAndCheck(flagcxDeviceHandle_t dev, flagcxStream_t stream,
                         void *devBuf, void *hostBuf, size_t bytes,
                         unsigned char value) {
  memset(hostBuf, value, bytes);
  dev->deviceMemcpy(devBuf, hostBuf, bytes, flagcxMemcpyHostToDevice, NULL);
  dev->streamSynchronize(stream);
}

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  int nranks = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &gRank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  const char *phaseEnv = getenv("PHASE");
  const bool run1 = (phaseEnv == NULL) || (strcmp(phaseEnv, "2") != 0);
  const bool run2 = (phaseEnv == NULL) || (strcmp(phaseEnv, "1") != 0);

  flagcxDeviceHandle_t dev = NULL;
  CHECK(flagcxDeviceHandleInit(&dev));
  int nGpu = 0;
  dev->getDeviceCount(&nGpu);
  dev->setDevice(gRank % nGpu);

  flagcxUniqueId uid;
  if (gRank == 0)
    CHECK(flagcxGetUniqueId(&uid));
  MPI_Bcast((void *)&uid, sizeof(uid), MPI_BYTE, 0, MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD);

  flagcxComm_t comm = NULL;
  CHECK(flagcxCommInitRank(&comm, nranks, &uid, gRank));

  flagcxStream_t stream = NULL;
  dev->streamCreate(&stream);

  const size_t n1 = 100;
  const size_t n2 = 200;
  void *sb1 = NULL, *sb2 = NULL, *rb1 = NULL, *rb2 = NULL;
  dev->deviceMalloc(&sb1, n1, flagcxMemDevice, NULL);
  dev->deviceMalloc(&sb2, n2, flagcxMemDevice, NULL);
  dev->deviceMalloc(&rb1, n1, flagcxMemDevice, NULL);
  dev->deviceMalloc(&rb2, n2, flagcxMemDevice, NULL);

  unsigned char *host = (unsigned char *)malloc(n2);
  fillAndCheck(dev, stream, sb1, host, n1, 0xAA);
  fillAndCheck(dev, stream, sb2, host, n2, 0xBB);
  fillAndCheck(dev, stream, rb1, host, n1, 0x00);
  fillAndCheck(dev, stream, rb2, host, n2, 0x00);

  const int self = gRank; // the local rank: send/recv to ourselves
  const int iters = 3;

  if (run1) {
    printf("[rank %d] phase 1: one self pair (send %zu + recv %zu)\n", gRank, n1,
           n1);
    fflush(stdout);
    for (int i = 0; i < iters; i++) {
      flagcxGroupStart(comm);
      flagcxSend(sb1, n1, flagcxChar, self, comm, stream);
      flagcxRecv(rb1, n1, flagcxChar, self, comm, stream);
      flagcxGroupEnd(comm);
    }
    dev->streamSynchronize(stream);
    printf("[rank %d] phase 1 OK\n", gRank);
    fflush(stdout);
  }

  if (run2) {
    printf("[rank %d] phase 2: two self pairs (2 sends + 2 recvs)\n", gRank);
    fflush(stdout);
    for (int i = 0; i < iters; i++) {
      flagcxGroupStart(comm);
      flagcxSend(sb1, n1, flagcxChar, self, comm, stream);
      flagcxSend(sb2, n2, flagcxChar, self, comm, stream);
      flagcxRecv(rb2, n2, flagcxChar, self, comm, stream);
      flagcxRecv(rb1, n1, flagcxChar, self, comm, stream);
      flagcxGroupEnd(comm);
    }
    dev->streamSynchronize(stream);
    printf("[rank %d] phase 2 OK\n", gRank);
    fflush(stdout);
  }

  // Data check: rb1 should hold 0xAA, rb2 should hold 0xBB.
  if (run2) {
    memset(host, 0, n2);
    dev->deviceMemcpy(host, rb1, n1, flagcxMemcpyDeviceToHost, NULL);
    dev->streamSynchronize(stream);
    int ok1 = (host[0] == 0xAA && host[n1 - 1] == 0xAA);
    memset(host, 0, n2);
    dev->deviceMemcpy(host, rb2, n2, flagcxMemcpyDeviceToHost, NULL);
    dev->streamSynchronize(stream);
    int ok2 = (host[0] == 0xBB && host[n2 - 1] == 0xBB);
    printf("[rank %d] verify rb1=%s rb2=%s\n", gRank, ok1 ? "OK" : "MISMATCH",
           ok2 ? "OK" : "MISMATCH");
    fflush(stdout);
  }

  MPI_Barrier(MPI_COMM_WORLD);
  if (gRank == 0)
    printf("ALL DONE\n");

  free(host);
  dev->deviceFree(sb1, flagcxMemDevice, NULL);
  dev->deviceFree(sb2, flagcxMemDevice, NULL);
  dev->deviceFree(rb1, flagcxMemDevice, NULL);
  dev->deviceFree(rb2, flagcxMemDevice, NULL);
  dev->streamDestroy(stream);
  flagcxCommDestroy(comm);
  flagcxDeviceHandleFree(dev);
  MPI_Finalize();
  return 0;
}
