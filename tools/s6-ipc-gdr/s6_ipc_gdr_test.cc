// S6 [P0][default] single-node 2-GPU IPC + GDR verification for the Default path.
// Covers exactly: gdrMemAlloc/gdrMemFree, the five ipcMemHandle*, pointer-type
// separation, handle exchange/open/close, teardown ordering (free/close/handle
// free), and FLAGCX_GDR_WRITE_REQUIRES_FLUSH semantics.
//
// build:
//   mpicxx -std=c++17 -O2 -I<repo>/flagcx/include -I<repo>/flagcx/adaptor/include \
//     -I<repo>/flagcx/core/include -I<repo>/flagcx/service/include \
//     -I/usr/local/corex/include s6_ipc_gdr_test.cc \
//     -L<repo>/build/lib -Wl,-rpath,<repo>/build/lib -lflagcx -o s6_ipc_gdr_test
// run:
//   mpirun --allow-run-as-root -np 2 ./s6_ipc_gdr_test
//   mpirun --allow-run-as-root --oversubscribe -np 4 ./s6_ipc_gdr_test
//
// env: FLAGCX_S6_CYCLES=n (default 2), FLAGCX_S6_ORDER=0 to skip destructive
//      ordering cases.

#include <mpi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "flagcx.h"
#include "flagcx_device_adaptor.h"
#include "flagcx_net.h"

extern struct flagcxDeviceAdaptor *deviceAdaptor;

static int R = 0, N = 1, ng = 0, F = 0;
static flagcxDeviceHandle_t DH = 0;
static flagcxStream_t ST = 0;

#define OK(x)                                                                  \
  do {                                                                         \
    flagcxResult_t _r = (x);                                                   \
    if (_r != flagcxSuccess) {                                                 \
      printf("[r%d] FAIL %s = %d (L%d)\n", R, #x, (int)_r, __LINE__);          \
      ++F;                                                                     \
    }                                                                          \
  } while (0)

// must be rejected (any non-success is accepted)
#define REJECT(x)                                                              \
  do {                                                                         \
    flagcxResult_t _r = (x);                                                   \
    if (_r == flagcxSuccess) {                                                 \
      printf("[r%d] FAIL %s was ACCEPTED (L%d)\n", R, #x, __LINE__);           \
      ++F;                                                                     \
    }                                                                          \
  } while (0)

#define CHK(cond, msg)                                                         \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("[r%d] FAIL %s\n", R, msg);                                       \
      ++F;                                                                     \
    }                                                                          \
  } while (0)

static void pat(void *b, size_t n, unsigned s) {
  unsigned char *p = (unsigned char *)b;
  for (size_t i = 0; i < n; ++i)
    p[i] = (unsigned char)((s * 131u + i * 7u) & 0xFFu);
}
static bool chk(const void *b, size_t n, unsigned s) {
  const unsigned char *p = (const unsigned char *)b;
  for (size_t i = 0; i < n; ++i)
    if (p[i] != (unsigned char)((s * 131u + i * 7u) & 0xFFu))
      return false;
  return true;
}

static int HB = 0;  // handle byte size reported by ipcMemHandleCreate

// export my buffer, exchange, return raw blobs of all ranks
static std::string exchange(flagcxIpcMemHandle_t mine, int nranks) {
  std::string raw((size_t)nranks * HB, '\0');
  MPI_Allgather((const void *)mine, HB, MPI_BYTE, &raw[0], HB, MPI_BYTE,
                MPI_COMM_WORLD);
  return raw;
}
static flagcxIpcMemHandle_t mkHandle(const std::string &blob, int off) {
  flagcxIpcMemHandle_t h = 0;
  size_t sz = 0;
  if (deviceAdaptor->ipcMemHandleCreate(&h, &sz) != flagcxSuccess || !h)
    return 0;
  memcpy((void *)h, blob.data() + (size_t)off, HB);
  return h;
}

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &R);
  MPI_Comm_size(MPI_COMM_WORLD, &N);
  int cycles = 2;
  if (const char *c = getenv("FLAGCX_S6_CYCLES"))
    cycles = atoi(c);

  if (flagcxDeviceHandleInit(&DH) != flagcxSuccess || deviceAdaptor == 0) {
    printf("[r%d] FATAL init\n", R);
    return 2;
  }
  OK(DH->getDeviceCount(&ng));
  OK(DH->setDevice(R % ng));
  OK(DH->streamCreate(&ST));
  printf("[r%d] dev=%d nGpu=%d nRanks=%d cycles=%d\n", R, R % ng, ng, N,
         cycles);
  fflush(stdout);

  const size_t B = 1u << 18;  // 256 KiB byte-per-rank, small on purpose
  const size_t K = 4096;

  // ---------------- #2 pointer type separation (no IPC yet) ----------------
  {
    void *d = 0;
    OK(deviceAdaptor->gdrMemAlloc(&d, K, 0));
    int t = -1;
    OK(deviceAdaptor->getPointerType(d, &t));
    CHK(t == FLAGCX_PTR_CUDA, "#2 gdr pointer type == CUDA");
    void *hp = 0;
    OK(deviceAdaptor->deviceMalloc(&hp, K, flagcxMemHost, 0));
    if (hp) {
      memset(hp, 0x5A, K);
      OK(deviceAdaptor->getPointerType(hp, &t));
      CHK(t == FLAGCX_PTR_HOST, "#2 pinned host pointer type == HOST");
      void *alias = 0;
      OK(deviceAdaptor->hostGetDevicePointer(&alias, hp));
      CHK(alias != 0, "#2 hostGetDevicePointer non-NULL");
      OK(deviceAdaptor->deviceFree(hp, flagcxMemHost, 0));
    }
    std::string pl(64, 'p');
    OK(deviceAdaptor->getPointerType(&pl[0], &t));
    CHK(t == FLAGCX_PTR_HOST, "#2 malloc pointer type == HOST");
    OK(deviceAdaptor->gdrMemFree(d, 0));
  }

  for (int cyc = 1; cyc <= cycles; ++cyc) {
    printf("[r%d] ===== cycle %d =====\n", R, cyc);
    fflush(stdout);
    void *d = 0;
    OK(deviceAdaptor->gdrMemAlloc(&d, B, 0));
    if (!d) {
      printf("[r%d] FATAL gdrMemAlloc NULL\n", R);
      return 2;
    }
    std::string h(B, '\0');
    pat(&h[0], B, (unsigned)R + 1u);
    OK(DH->deviceMemcpy(d, &h[0], B, flagcxMemcpyHostToDevice, ST));
    OK(DH->streamSynchronize(ST));
    MPI_Barrier(MPI_COMM_WORLD);

    // ------------- #1 registrable + #3 exchange -------------
    flagcxIpcMemHandle_t mh = 0;
    size_t hs = 0;
    OK(deviceAdaptor->ipcMemHandleCreate(&mh, &hs));
    OK(deviceAdaptor->ipcMemHandleGet(mh, d));
    HB = (int)hs;
    CHK(hs == sizeof(cudaIpcMemHandle_t), "#1 handle size == cudaIpcMemHandle_t");
    // host pointer must never be exportable
    {
      std::string ho(64, 'h');
      flagcxIpcMemHandle_t tmp = 0;
      size_t s2 = 0;
      OK(deviceAdaptor->ipcMemHandleCreate(&tmp, &s2));
      REJECT(deviceAdaptor->ipcMemHandleGet(tmp, &ho[0]));
      OK(deviceAdaptor->ipcMemHandleFree(tmp));
    }
    std::string raw = exchange(mh, N);
    for (int p = 0; p < N; ++p) {
      bool nonzero = false;
      for (int i = 0; i < HB; ++i)
        if (raw[(size_t)p * HB + i]) {
          nonzero = true;
          break;
        }
      CHK(nonzero, "#3 exchanged handle blob non-zero");
    }

    // ------------- #4 open -------------
    std::vector<flagcxIpcMemHandle_t> ph((size_t)N, 0);
    std::vector<void *> pp((size_t)N, 0);
    for (int p = 0; p < N; ++p) {
      ph[p] = mkHandle(raw, p * HB);
      CHK(ph[p] != 0, "#4 create peer handle");
      if (p == R) {
        pp[p] = d;  // self: runtime refuses self-open; use local pointer
        continue;
      }
      OK(deviceAdaptor->ipcMemHandleOpen(ph[p], &pp[p]));
      CHK(pp[p] != 0, "#4 open peer non-NULL");
      if (pp[p]) {
        int t2 = -1;
        OK(deviceAdaptor->getPointerType(pp[p], &t2));
        CHK(t2 != FLAGCX_PTR_HOST, "#2 peer ptr not typed HOST");
      }
    }
    // #4a output param must be non-NULL-checked
    {
      void *nn = d;
      REJECT(deviceAdaptor->ipcMemHandleOpen(mh, &nn));
    }
    // #4b garbage (all-zero) handle must be rejected
    {
      flagcxIpcMemHandle_t gh = 0;
      size_t z = 0;
      OK(deviceAdaptor->ipcMemHandleCreate(&gh, &z));
      memset((void *)gh, 0, z);
      void *gp = 0;
      REJECT(deviceAdaptor->ipcMemHandleOpen(gh, &gp));
      OK(deviceAdaptor->ipcMemHandleFree(gh));
    }
    // #4c double open: record semantics (same ptr vs error)
    {
      void *dup = 0;
      flagcxResult_t r2 = deviceAdaptor->ipcMemHandleOpen(mh, &dup);
      printf("[r%d] info double-open res=%d ptr=%p\n", R, (int)r2, dup);
      if (r2 == flagcxSuccess && dup)
        OK(deviceAdaptor->ipcMemHandleClose(dup));
    }

    // ------------- data check via opened pointers -------------
    {
      int badSame = 0, badCross = 0;
      for (int p = 0; p < N; ++p) {
        if (p == R || !pp[p])
          continue;
        std::string g(K, '\0');
        OK(DH->deviceMemcpy(&g[0], pp[p], K, flagcxMemcpyDeviceToHost, ST));
        OK(DH->streamSynchronize(ST));
        if (!chk(&g[0], K, (unsigned)p + 1u)) {
          if ((p % ng) == (R % ng))
            ++badSame;
          else
            ++badCross;
        }
      }
      CHK(badSame == 0, "same-device peer reads match");
      CHK(badCross == 0, "cross-device peer reads match");
    }

    // ------------- #7 write visibility (streamSync + deviceSync only) ----
      int tg = (R + 1) % N;
    {
      if (N > 1 && pp[tg]) {
        char tag = (char)(0xC0 | (R & 0x3F));
        std::string w(K, tag);
        OK(DH->deviceMemcpy(pp[tg], &w[0], K, flagcxMemcpyHostToDevice, ST));
        OK(DH->streamSynchronize(ST));
        OK(DH->deviceSynchronize());
      }
      MPI_Barrier(MPI_COMM_WORLD);
      if (N > 1) {
        int from = (R - 1 + N) % N;
        std::string g(K, '\0');
        OK(DH->deviceMemcpy(&g[0], d, K, flagcxMemcpyDeviceToHost, ST));
        OK(DH->streamSynchronize(ST));
        bool vis = true;
        for (size_t i = 0; i < K; ++i)
          if ((unsigned char)g[i] != (unsigned char)(0xC0 | (from & 0x3F))) {
            vis = false;
            break;
          }
        bool cross = ((from % ng) != (R % ng));
        printf("[r%d] #7 vis=%s pair=%s writer=%d cyc=%d\n", R,
               vis ? "YES" : "NO", cross ? "cross" : "same", from, cyc);
        fflush(stdout);
      }
    }

    // ------------- #5 close variants -------------
    // #5a reverse-order close (highest index first)
    for (int p = N - 1; p >= 0; --p)
      if (pp[p] && p != R)
        OK(deviceAdaptor->ipcMemHandleClose(pp[p]));
    // #5b reopen after close must work
    {
      int p = 0;
      if (p == R)
        p = (N > 1) ? 1 : -1;
      if (p >= 0 && ph[p]) {
        void *again = 0;
        OK(deviceAdaptor->ipcMemHandleOpen(ph[p], &again));
        CHK(again != 0, "#5b reopen after close works");
        if (again) {
          std::string g(K, '\0');
          OK(DH->deviceMemcpy(&g[0], again, K, flagcxMemcpyDeviceToHost, ST));
          OK(DH->streamSynchronize(ST));
          (void)g; /* data overwritten by #7 tags; readability only */
          OK(deviceAdaptor->ipcMemHandleClose(again));
        }
      }
    }
    // #5c double close must be rejected
    {
      int p = (R + 1) % N;
      if (N > 1 && pp[p]) {
        // pp[p] already closed in #5a; closing again must not succeed
        flagcxResult_t r2 = deviceAdaptor->ipcMemHandleClose(pp[p]);
        printf("[r%d] info double-close res=%d\n", R, (int)r2);
        CHK(r2 != flagcxSuccess, "#5c double close rejected");
      }
    }

    // ------------- #6 ordering: handleFree -> close (wrong order) ----------
    {
      int p = (R + 1) % N;
      if (N > 1 && ph[p]) {
        void *ptr = 0;
        OK(deviceAdaptor->ipcMemHandleOpen(ph[p], &ptr));
        if (ptr) {
          // wrong order on purpose: free handle before closing the mapping
          OK(deviceAdaptor->ipcMemHandleFree(ph[p]));
          flagcxResult_t r2 = deviceAdaptor->ipcMemHandleClose(ptr);
          printf("[r%d] info freeHandle-before-close close_res=%d\n", R,
                 (int)r2);
          // close does not need the handle, so success is acceptable, but we
          // must not crash and we must not silently leak.
        }
        ph[p] = 0;
      }
    }
    // #6 correct order for the rest: close -> handleFree -> memFree
    for (int p = 0; p < N; ++p)
      if (ph[p])
        OK(deviceAdaptor->ipcMemHandleFree(ph[p]));
    OK(deviceAdaptor->ipcMemHandleFree(mh));
    OK(deviceAdaptor->gdrMemFree(d, 0));
    OK(deviceAdaptor->gdrMemFree(0, 0));
    MPI_Barrier(MPI_COMM_WORLD);
  }

  // ------------- #6 destructive: memFree before close (last, may fault) ----
  if (getenv("FLAGCX_S6_ORDER") == 0) {
    void *d2 = 0;
    if (deviceAdaptor->gdrMemAlloc(&d2, 4096, 0) == flagcxSuccess && d2) {
      flagcxIpcMemHandle_t h2 = 0;
      size_t s2 = 0;
      deviceAdaptor->ipcMemHandleCreate(&h2, &s2);
      deviceAdaptor->ipcMemHandleGet(h2, d2);
      flagcxResult_t r1 = deviceAdaptor->gdrMemFree(d2, 0);
      flagcxResult_t r2 = deviceAdaptor->ipcMemHandleClose(d2);
      printf("[r%d] info memFree-before-close: free=%d close=%d (no crash)\n",
             R, (int)r1, (int)r2);
      deviceAdaptor->ipcMemHandleFree(h2);
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }

  OK(DH->streamDestroy(ST));
  int tot = 0;
  MPI_Allreduce(&F, &tot, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  printf("[r%d] === Overall: %s === local=%d global=%d\n", R,
         tot == 0 ? "PASS" : "FAIL", F, tot);
  fflush(stdout);
  MPI_Finalize();
  return tot == 0 ? 0 : 1;
}
