// Minimal reproducer: two communicators in one process, symmetric window
// registration on each of them, hangs on the SECOND registration.
//
// This is the reduced form of test_one_side_register.cpp: it drops the signal
// registration, the warmup send/recv, all three phases and the deregistration,
// and keeps only what is needed to reproduce the deadlock.
//
// Build:
//   ROOT=<FlagCX root>
//   mpicxx -std=c++17 -g -DOMPI_SKIP_MPICXX -o repro_two_comm_window \
//     repro_two_comm_window.cpp -I$ROOT/flagcx/include \
//     -L$ROOT/build/lib -Wl,--no-as-needed -Wl,-rpath,$ROOT/build/lib -lflagcx
//
// Run (single node, 2 devices):
//   FLAGCX_USE_HETERO_COMM=1 FLAGCX_MEM_ENABLE=1 FLAGCX_VMM_ENABLE=0 \
//   mpirun --allow-run-as-root -np 2 -x CUDA_VISIBLE_DEVICES=1,2 \
//     -x LD_LIBRARY_PATH -x FLAGCX_USE_HETERO_COMM -x FLAGCX_MEM_ENABLE \
//     -x FLAGCX_VMM_ENABLE ./repro_two_comm_window
//
// Expected (buggy): rank 0 prints "[repro] window register on comm1 ... DONE"
// then "[repro] window register on comm2 ...", then the job hangs forever.
// Root cause: flagcxOneSideBuildOneContext pairs connect/accept by arrival
// order, so the cross-rank connection can be consumed as the self-loopback
// connection of the second communicator's full-mesh build.
//
// Since it is a timing race, run it a few times; if it does not hang, increase
// NCOMM.
#include "flagcx.h"

#include <mpi.h>
#include <cstdio>
#include <cstdlib>

#define NCOMM 2

static void check(flagcxResult_t res, const char *what, int rank) {
  if (res != flagcxSuccess) {
    fprintf(stderr, "[rank %d] %s failed (res=%d)\n", rank, what, (int)res);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
}

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, nranks = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  if (nranks != 2) {
    if (rank == 0)
      printf("this reproducer needs exactly 2 ranks\n");
    MPI_Finalize();
    return 1;
  }

  flagcxDeviceHandle_t devHandle = nullptr;
  check(flagcxDeviceHandleInit(&devHandle), "flagcxDeviceHandleInit", rank);
  int nGpu = 0;
  devHandle->getDeviceCount(&nGpu);
  devHandle->setDevice(rank % nGpu);

  flagcxStream_t stream = nullptr;
  devHandle->streamCreate(&stream);

  const size_t bytes = 1UL << 20; // 1 MiB
  flagcxComm_t comm[NCOMM] = {};
  flagcxWindow_t win[NCOMM] = {};
  void *buf[NCOMM] = {};

  // Create NCOMM communicators (collective on both ranks).
  for (int c = 0; c < NCOMM; c++) {
    flagcxUniqueId uid;
    if (rank == 0)
      check(flagcxGetUniqueId(&uid), "flagcxGetUniqueId", rank);
    MPI_Bcast(&uid, sizeof(uid), MPI_BYTE, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    check(flagcxCommInitRank(&comm[c], nranks, &uid, rank),
          "flagcxCommInitRank", rank);
    if (rank == 0) {
      printf("[repro] comm%d created\n", c + 1);
      fflush(stdout);
    }
  }

  for (int c = 0; c < NCOMM; c++) {
    check(flagcxMemAlloc(&buf[c], bytes), "flagcxMemAlloc", rank);
    devHandle->deviceMemset(buf[c], 0, bytes, flagcxMemDevice, stream);
    devHandle->streamSynchronize(stream);
  }

  // The trigger: register a symmetric window on each communicator, back to
  // back, with no synchronization in between.
  for (int c = 0; c < NCOMM; c++) {
    if (rank == 0) {
      printf("[repro] window register on comm%d ...\n", c + 1);
      fflush(stdout);
    }
    check(flagcxCommWindowRegister(comm[c], buf[c], bytes, &win[c],
                                   FLAGCX_WIN_COLL_SYMMETRIC),
          "flagcxCommWindowRegister", rank);
    if (rank == 0) {
      printf("[repro] window register on comm%d DONE\n", c + 1);
      fflush(stdout);
    }
  }

  MPI_Barrier(MPI_COMM_WORLD);
  if (rank == 0)
    printf("[repro] all %d windows registered, no hang\n", NCOMM);

  for (int c = 0; c < NCOMM; c++) {
    flagcxCommWindowDeregister(comm[c], win[c]);
    flagcxMemFree(buf[c]);
    flagcxCommDestroy(comm[c]);
  }
  devHandle->streamDestroy(stream);
  flagcxDeviceHandleFree(devHandle);
  MPI_Finalize();
  return 0;
}
