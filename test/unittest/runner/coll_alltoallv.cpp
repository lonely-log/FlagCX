// AlltoAllV correctness test.
//
// Unlike AlltoAll (which splits the buffer into equal chunks), every rank here
// sends a *different* number of elements to each peer, and the number it
// receives from a peer differs from the number it sends to that peer. That
// uneven, asymmetric traffic is exactly what the "V" in AlltoAllV exercises.
//
// The per-peer counts are a pure function of (src, dst), so every rank derives
// both its send and its receive layout locally, without extra communication,
// and the two sides are guaranteed to agree.
//
// Buffer offsets are in element units; the fixture allocates `count` floats.

#include "runner_fixtures.hpp"
#include "test_utils.hpp"
#include <iostream>
#include <vector>

namespace {

// Sentinel for "no data was written here". Rank ids are >= 0, so -1 can only
// come from memory the kernel never touched.
constexpr float kUnset = -1.0f;

// Number of elements rank `src` sends to rank `dst`. Depends on both endpoints
// so that sendcounts and recvcounts end up different per peer. Result is in
// [1, 5] * unit.
size_t elementsFor(int src, int dst, size_t unit) {
  return unit * (1 + static_cast<size_t>((src * 3 + dst) % 5));
}

} // namespace

TEST_F(FlagCXCollTest, AlltoAllv) {

  if (nranks < 2) {
    GTEST_SKIP() << "AlltoAllV needs at least 2 ranks";
  }

  // The largest block is 5*unit, so a rank sends at most
  // nranks*5*unit == count elements in total.
  const size_t unit = count / (5 * static_cast<size_t>(nranks));
  if (unit == 0) {
    GTEST_SKIP() << "buffer too small for a " << nranks << "-rank exchange";
  }

  std::vector<size_t> sendcounts(nranks), sdispls(nranks);
  std::vector<size_t> recvcounts(nranks), rdispls(nranks);

  size_t sdis = 0;
  size_t rdis = 0;
  for (int p = 0; p < nranks; p++) {
    sendcounts[p] = elementsFor(rank, p, unit);
    recvcounts[p] = elementsFor(p, rank, unit);
    sdispls[p] = sdis;
    rdispls[p] = rdis;
    sdis += sendcounts[p];
    rdis += recvcounts[p];
  }

  // Invariant by construction; checked before any buffer is touched.
  ASSERT_LE(sdis, count);
  ASSERT_LE(rdis, count);

  if (rank == 0) {
    std::cout << "AlltoAllV layout (rank 0): send(";
    for (int p = 0; p < nranks; p++)
      std::cout << (p ? "," : "") << sendcounts[p];
    std::cout << ") recv(";
    for (int p = 0; p < nranks; p++)
      std::cout << (p ? "," : "") << recvcounts[p];
    std::cout << ")" << std::endl;
  }

  float *hsend = static_cast<float *>(hostsendbuff);
  float *hrecv = static_cast<float *>(hostrecvbuff);

  // Seed both staging buffers so stale device memory can never be mistaken for
  // a successful transfer.
  for (size_t i = 0; i < count; i++) {
    hsend[i] = kUnset;
    hrecv[i] = kUnset;
  }

  // Each block carries the sender's rank id, so the receiver can tell exactly
  // which peer a block came from.
  for (int p = 0; p < nranks; p++) {
    for (size_t j = 0; j < sendcounts[p]; j++) {
      hsend[sdispls[p] + j] = static_cast<float>(rank);
    }
  }

  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->deviceMemcpy(recvbuff, hostrecvbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  flagcxResult_t res =
      flagcxAlltoAllv(sendbuff, sendcounts.data(), sdispls.data(), recvbuff,
                      recvcounts.data(), rdispls.data(), flagcxFloat, comm,
                      stream);
  EXPECT_EQ(res, flagcxSuccess);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  // Expected receive layout: block p holds peer p's rank id, everything else
  // stays at the sentinel. Comparing the whole buffer in one pass verifies both
  // that each block arrived from the right peer, and that nothing was written
  // outside the receive windows.
  std::vector<float> expected(count, kUnset);
  for (int p = 0; p < nranks; p++) {
    for (size_t j = 0; j < recvcounts[p]; j++) {
      expected[rdispls[p] + j] = static_cast<float>(p);
    }
  }

  EXPECT_TRUE(verifyBuffer(hrecv, expected.data(), count));
}
