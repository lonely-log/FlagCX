// Group semantics (flagcxGroupStart / flagcxGroupEnd) correctness test.
//
// On the homogeneous path these two calls forward straight through to the
// vendor CCL:  flagcxGroupStart -> homoRunnerGroupStart -> cclAdaptor->
// groupStart -> ncclGroupStart()  (IXCCL on Iluvatar).
//
// Documented semantics exercised here (see flagcx.h "Group semantics"):
//   1. calls between Start and End are fused; nothing starts on the FlagCX
//      stream until GroupEnd
//   2. GroupEnd only *enqueues* for collective communication -- it does not
//      wait for completion, so the data is only valid after a stream sync
//   3. several send/recv can progress concurrently inside one group; run
//      serially they would block against each other
//   4. groups may be nested
//
// Every case below syncs the stream before touching the results -- skipping
// that is the classic way to get a flaky group test.

#include "runner_fixtures.hpp"
#include "test_utils.hpp"
#include <vector>

namespace {

constexpr float kUnset = -1.0f;

// Sum across all ranks of (r * 1000.0f + (i % 10)).
float expectedSum(int nranks, size_t i) {
  return nranks * (nranks - 1) / 2.0f * 1000.0f + nranks * (float)(i % 10);
}

} // namespace

// ---------------------------------------------------------------------------
// Two independent collectives fused into one group.
// ---------------------------------------------------------------------------
TEST_F(FlagCXCollTest, GroupFusedCollectives) {

  const size_t half = count / 2;
  float *sendA = static_cast<float *>(sendbuff);
  float *recvA = static_cast<float *>(recvbuff);
  float *sendB = sendA + half;
  float *recvB = recvA + half;

  for (size_t i = 0; i < count; i++) {
    static_cast<float *>(hostsendbuff)[i] = rank * 1000.0f + (i % 10);
  }
  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  flagcxResult_t startRes = flagcxGroupStart(comm);
  EXPECT_EQ(startRes, flagcxSuccess);
  flagcxAllReduce(sendA, recvA, half, flagcxFloat, flagcxSum, comm, stream);
  flagcxAllReduce(sendB, recvB, half, flagcxFloat, flagcxSum, comm, stream);
  flagcxResult_t endRes = flagcxGroupEnd(comm);
  EXPECT_EQ(endRes, flagcxSuccess);

  // GroupEnd only enqueues; the results are not valid before this sync.
  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  std::vector<float> expected(count);
  for (size_t i = 0; i < half; i++) {
    expected[i] = expectedSum(nranks, i);
    expected[half + i] = expectedSum(nranks, half + i);
  }

  EXPECT_TRUE(
      verifyBuffer(static_cast<float *>(hostrecvbuff), expected.data(), count));
}

// ---------------------------------------------------------------------------
// Several send/recv pairs inside one group.
//
// Executed one at a time these would block against each other: flagcxSend is
// blocking for the GPU, so the first send would wait for a receive that has
// not been posted yet. Grouping is what makes them progress concurrently --
// this is the primary reason the API exists.
// ---------------------------------------------------------------------------
TEST_F(FlagCXCollTest, GroupConcurrentSendRecv) {

  const size_t half = count / 2;
  const int sendPeer = (rank + 1) % nranks;
  const int recvPeer = (rank - 1 + nranks) % nranks;

  float *hsend = static_cast<float *>(hostsendbuff);
  float *hrecv = static_cast<float *>(hostrecvbuff);
  float *sendA = static_cast<float *>(sendbuff);
  float *recvA = static_cast<float *>(recvbuff);
  float *sendB = sendA + half;
  float *recvB = recvA + half;

  // Two exchanges carry distinguishable payloads.
  for (size_t i = 0; i < half; i++) {
    hsend[i] = static_cast<float>(rank);
    hsend[half + i] = static_cast<float>(rank) + 100.0f;
  }
  for (size_t i = 0; i < count; i++) {
    hrecv[i] = kUnset;
  }

  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->deviceMemcpy(recvbuff, hostrecvbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  flagcxResult_t startRes = flagcxGroupStart(comm);
  EXPECT_EQ(startRes, flagcxSuccess);
  flagcxSend(sendA, half, flagcxFloat, sendPeer, comm, stream);
  flagcxRecv(recvA, half, flagcxFloat, recvPeer, comm, stream);
  flagcxSend(sendB, half, flagcxFloat, sendPeer, comm, stream);
  flagcxRecv(recvB, half, flagcxFloat, recvPeer, comm, stream);
  flagcxResult_t endRes = flagcxGroupEnd(comm);
  EXPECT_EQ(endRes, flagcxSuccess);

  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  std::vector<float> expected(count);
  for (size_t i = 0; i < half; i++) {
    expected[i] = static_cast<float>(recvPeer);
    expected[half + i] = static_cast<float>(recvPeer) + 100.0f;
  }

  EXPECT_TRUE(
      verifyBuffer(static_cast<float *>(hostrecvbuff), expected.data(), count));
}

// ---------------------------------------------------------------------------
// Nested groups.
// ---------------------------------------------------------------------------
TEST_F(FlagCXCollTest, GroupNested) {

  const size_t half = count / 2;
  float *sendA = static_cast<float *>(sendbuff);
  float *recvA = static_cast<float *>(recvbuff);
  float *sendB = sendA + half;
  float *recvB = recvA + half;

  for (size_t i = 0; i < count; i++) {
    static_cast<float *>(hostsendbuff)[i] = rank * 1000.0f + (i % 10);
  }
  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  EXPECT_EQ(flagcxGroupStart(comm), flagcxSuccess);          // outer
  flagcxAllReduce(sendA, recvA, half, flagcxFloat, flagcxSum, comm, stream);
  EXPECT_EQ(flagcxGroupStart(comm), flagcxSuccess);          // inner
  flagcxAllReduce(sendB, recvB, half, flagcxFloat, flagcxSum, comm, stream);
  EXPECT_EQ(flagcxGroupEnd(comm), flagcxSuccess);            // inner end
  EXPECT_EQ(flagcxGroupEnd(comm), flagcxSuccess);            // outer end

  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  std::vector<float> expected(count);
  for (size_t i = 0; i < half; i++) {
    expected[i] = expectedSum(nranks, i);
    expected[half + i] = expectedSum(nranks, half + i);
  }

  EXPECT_TRUE(
      verifyBuffer(static_cast<float *>(hostrecvbuff), expected.data(), count));
}

// ---------------------------------------------------------------------------
// An empty group must be harmless: it may not hang, and it must leave the
// communicator usable for the following collective.
// ---------------------------------------------------------------------------
TEST_F(FlagCXCollTest, GroupEmpty) {

  for (size_t i = 0; i < count; i++) {
    static_cast<float *>(hostsendbuff)[i] = rank * 1000.0f + (i % 10);
  }
  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  EXPECT_EQ(flagcxGroupStart(comm), flagcxSuccess);
  EXPECT_EQ(flagcxGroupEnd(comm), flagcxSuccess);

  devHandle->streamSynchronize(stream);
  MPI_Barrier(MPI_COMM_WORLD);

  // The communicator must still work after an empty group.
  flagcxAllReduce(sendbuff, recvbuff, count, flagcxFloat, flagcxSum, comm,
                  stream);
  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, stream);
  devHandle->streamSynchronize(stream);

  MPI_Barrier(MPI_COMM_WORLD);

  std::vector<float> expected(count);
  for (size_t i = 0; i < count; i++) {
    expected[i] = expectedSum(nranks, i);
  }

  EXPECT_TRUE(
      verifyBuffer(static_cast<float *>(hostrecvbuff), expected.data(), count));
}
