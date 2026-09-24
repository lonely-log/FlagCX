// Point-to-point Send/Recv correctness test, with per-rank logging.
//
// Ring pattern: each rank sends to (rank+1)%nranks and receives from
// (rank-1+nranks)%nranks. Each rank fills its sendbuff with its own rank id;
// after the exchange every rank verifies that the block it received still
// carries the sender's rank id.
//
// Every rank logs what it sent and what it received, so the whole exchange can
// be reconstructed from the merged output. Because MPI interleaves stdout,
// grab clean per-rank files with:
//   mpirun ... --output-filename DIR runner_mpi_tests --gtest_filter=...
// which lands in DIR/1/rank.N/stdout.

#include "runner_fixtures.hpp"
#include <cstdio>

namespace {

constexpr size_t kPreview = 4;    // elements shown at each end of a block
constexpr float kUnset = -1.0f;   // sentinel for "receive never happened"

// Print tag, peer, element count and a head/tail preview of the block.
void logBlock(const char *tag, int rank, int peer, const float *buf,
              size_t n) {
  std::printf("[rank %d] %s peer %d | count=%zu | head=[", rank, tag, peer, n);
  for (size_t i = 0; i < n && i < kPreview; i++)
    std::printf("%s%.1f", i ? " " : "", buf[i]);
  std::printf("] tail=[");
  const size_t tailStart = (n > kPreview) ? n - kPreview : 0;
  for (size_t i = tailStart; i < n; i++)
    std::printf("%s%.1f", i > tailStart ? " " : "", buf[i]);
  std::printf("]\n");
}

} // namespace

TEST_F(FlagCXCollTest, SendRecv) {

  const int sendPeer = (rank + 1) % nranks;
  const int recvPeer = (rank - 1 + nranks) % nranks;

  float *hsend = static_cast<float *>(hostsendbuff);
  float *hrecv = static_cast<float *>(hostrecvbuff);

  // Seed with a sentinel so a receive that never lands cannot be mistaken for
  // a success.
  for (size_t i = 0; i < count; i++) {
    hsend[i] = static_cast<float>(rank);
    hrecv[i] = kUnset;
  }

  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->deviceMemcpy(recvbuff, hostrecvbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->streamSynchronize(stream);

  std::printf("[rank %d] layout: send -> peer %d, recv <- peer %d, "
              "count=%zu\n",
              rank, sendPeer, recvPeer, count);
  std::fflush(stdout);

  MPI_Barrier(MPI_COMM_WORLD);

  // Use group API for concurrent send/recv
  flagcxGroupStart(comm);
  flagcxSend(sendbuff, count, flagcxFloat, sendPeer, comm, stream);
  flagcxRecv(recvbuff, count, flagcxFloat, recvPeer, comm, stream);
  flagcxGroupEnd(comm);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  logBlock("SEND ->", rank, sendPeer, hsend, count);
  logBlock("RECV <-", rank, recvPeer, hrecv, count);

  // Every received element must carry the sender's rank id.
  const float expected = static_cast<float>(recvPeer);
  size_t bad = 0;
  size_t firstBad = 0;
  for (size_t i = 0; i < count; i++) {
    if (hrecv[i] != expected) {
      if (bad == 0)
        firstBad = i;
      bad++;
    }
  }

  std::printf("[rank %d] VERIFY | expected=%.1f | matched=%zu/%zu | %s",
              rank, expected, count - bad, count, bad ? "MISMATCH" : "OK");
  if (bad)
    std::printf(" | first mismatch at index %zu (got %.1f, want %.1f)",
                firstBad, hrecv[firstBad], expected);
  std::printf("\n");
  std::fflush(stdout);

  EXPECT_EQ(bad, 0u) << "rank " << rank << ": " << bad
                     << " of " << count << " elements from peer " << recvPeer
                     << " are wrong (first at index " << firstBad << ")";
}
