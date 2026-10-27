/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "adaptor.h"
#include "comm.h"
#include "core.h"
#include "net.h"
#include "net_transport.h"
#include "param.h"
#include "onesided.h"
#include "socket.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int flagcxNetIfs = -1;
struct flagcxNetSocketDev {
  union flagcxSocketAddress addr;
  char devName[MAX_IF_NAME_SIZE];
  char *pciPath;
};
static struct flagcxNetSocketDev flagcxNetSocketDevs[MAX_IFS];

pthread_mutex_t flagcxNetSocketLock = PTHREAD_MUTEX_INITIALIZER;

static flagcxResult_t flagcxNetSocketGetPciPath(char *devName, char **pciPath) {
  char devicePath[PATH_MAX];
  snprintf(devicePath, PATH_MAX, "/sys/class/net/%s/device", devName);
  // May return NULL if the file doesn't exist.
  *pciPath = realpath(devicePath, NULL);
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketInit() {
  if (flagcxNetIfs == -1) {
    pthread_mutex_lock(&flagcxNetSocketLock);
    if (flagcxNetIfs == -1) {
      char names[MAX_IF_NAME_SIZE * MAX_IFS];
      union flagcxSocketAddress addrs[MAX_IFS];
      flagcxNetIfs =
          flagcxFindInterfaces(names, addrs, MAX_IF_NAME_SIZE, MAX_IFS);
      if (flagcxNetIfs <= 0) {
        WARN("NET/Socket : no interface found");
        return flagcxInternalError;
      } else {
#define MAX_LINE_LEN (2047)
        char line[MAX_LINE_LEN + 1];
        char addrline[SOCKET_NAME_MAXLEN + 1];
        line[0] = '\0';
        addrline[SOCKET_NAME_MAXLEN] = '\0';
        for (int i = 0; i < flagcxNetIfs; i++) {
          strcpy(flagcxNetSocketDevs[i].devName, names + i * MAX_IF_NAME_SIZE);
          memcpy(&flagcxNetSocketDevs[i].addr, addrs + i,
                 sizeof(union flagcxSocketAddress));
          FLAGCXCHECK(flagcxNetSocketGetPciPath(
              flagcxNetSocketDevs[i].devName, &flagcxNetSocketDevs[i].pciPath));
          snprintf(line + strlen(line), MAX_LINE_LEN - strlen(line),
                   " [%d]%s:%s", i, names + i * MAX_IF_NAME_SIZE,
                   flagcxSocketToString(&addrs[i], addrline));
        }
        line[MAX_LINE_LEN] = '\0';
        INFO(FLAGCX_INIT | FLAGCX_NET, "NET/Socket : Using%s", line);
      }
    }
    pthread_mutex_unlock(&flagcxNetSocketLock);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketDevices(int *ndev) {
  *ndev = flagcxNetIfs;
  return flagcxSuccess;
}

static flagcxResult_t flagcxNetSocketGetSpeed(char *devName, int *speed) {
  *speed = 0;
  char speedPath[PATH_MAX];
  sprintf(speedPath, "/sys/class/net/%s/speed", devName);
  int fd = open(speedPath, O_RDONLY);
  if (fd != -1) {
    char speedStr[] = "        ";
    if (read(fd, speedStr, sizeof(speedStr) - 1) > 0) {
      *speed = strtol(speedStr, NULL, 0);
    }
    close(fd);
  }
  if (*speed <= 0) {
    INFO(FLAGCX_NET, "Could not get speed from %s. Defaulting to 10 Gbps.",
         speedPath);
    *speed = 10000;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketGetProperties(int dev, void *props) {
  flagcxNetProperties_t *netProps = (flagcxNetProperties_t *)props;
  netProps->name = flagcxNetSocketDevs[dev].devName;
  netProps->pciPath = flagcxNetSocketDevs[dev].pciPath;
  netProps->guid = dev;
  netProps->ptrSupport = FLAGCX_PTR_HOST;
  netProps->regIsGlobal = 0;
  FLAGCXCHECK(flagcxNetSocketGetSpeed(netProps->name, &netProps->speed));
  netProps->latency = 0; // Not set
  netProps->port = 0;
  netProps->maxComms = 65536;
  netProps->maxRecvs = 1;
  netProps->netDeviceType = FLAGCX_NET_DEVICE_HOST;
  netProps->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  return flagcxSuccess;
}

/* Communication functions */

#define MAX_SOCKETS 64
#define MAX_THREADS 16
#define MAX_REQUESTS FLAGCX_NET_MAX_REQUESTS
#define MIN_CHUNKSIZE (64 * 1024)

// Op code carried by the stateless token that reports one-sided completion.
#define FLAGCX_SOCK_OP_ONESHOT 0x5a5a

FLAGCX_PARAM(SocketNsocksPerThread, "NSOCKS_PERTHREAD", -2);
FLAGCX_PARAM(SocketNthreads, "SOCKET_NTHREADS", -2);

enum flagcxNetSocketCommState {
  flagcxNetSocketCommStateStart = 0,
  flagcxNetSocketCommStateConnect = 1,
  flagcxNetSocketCommStateAccept = 3,
  flagcxNetSocketCommStateSend = 4,
  flagcxNetSocketCommStateRecv = 5,
};

struct flagcxNetSocketCommStage {
  enum flagcxNetSocketCommState state;
  uint8_t iteration;
  struct flagcxSocket *sock;
  struct flagcxNetSocketComm *comm;
};

struct flagcxNetSocketHandle {
  union flagcxSocketAddress connectAddr;
  uint64_t magic; // random number to help debugging
  int nSocks;
  int nThreads;
  struct flagcxNetSocketCommStage stage;
};

struct flagcxNetSocketTask {
  int op;
  void *data;
  int size;
  struct flagcxSocket *sock;
  int offset;
  int used;
  flagcxResult_t result;
};

struct flagcxNetSocketRequest {
  int op;
  void *data;
  int size;
  struct flagcxSocket *ctrlSock;
  int offset;
  int used;
  struct flagcxNetSocketComm *comm;
  struct flagcxNetSocketTask *tasks[MAX_SOCKETS];
  int nSubs;
};

struct flagcxNetSocketTaskQueue {
  int next;
  int len;
  struct flagcxNetSocketTask *tasks;
};

struct flagcxNetSocketThreadResources {
  struct flagcxNetSocketTaskQueue threadTaskQueue;
  int stop;
  struct flagcxNetSocketComm *comm;
  pthread_mutex_t threadLock;
  pthread_cond_t threadCond;
};

struct flagcxNetSocketListenComm {
  struct flagcxSocket sock;
  struct flagcxNetSocketCommStage stage;
  int nSocks;
  int nThreads;
  int dev;
};

struct flagcxNetSocketComm {
  struct flagcxSocket ctrlSock;
  struct flagcxSocket socks[MAX_SOCKETS];
  int dev;
  int cudaDev;
  int nSocks;
  int nThreads;
  int nextSock;
  struct flagcxNetSocketRequest requests[MAX_REQUESTS];
  pthread_t helperThread[MAX_THREADS];
  struct flagcxNetSocketThreadResources threadResources[MAX_THREADS];
};

void *persistentSocketThread(void *args_) {
  struct flagcxNetSocketThreadResources *resource =
      (struct flagcxNetSocketThreadResources *)args_;
  struct flagcxNetSocketComm *comm = resource->comm;
  struct flagcxNetSocketTaskQueue *myQueue = &resource->threadTaskQueue;
  int nSocksPerThread = comm->nSocks / comm->nThreads;
  while (1) {
    int idle = 1;
    int mark = myQueue->next; // mark newest task seen
    for (int i = 0; i < myQueue->len; i += nSocksPerThread) {
      int repeat;
      do {
        repeat = 0;
        for (int j = 0; j < nSocksPerThread; j++) {
          struct flagcxNetSocketTask *r = myQueue->tasks + i + j;
          if (r != NULL && r->used == 1 && r->offset < r->size) {
            r->result = flagcxSocketProgress(r->op, r->sock, r->data, r->size,
                                             &r->offset);
            if (r->result != flagcxSuccess) {
              WARN("NET/Socket : socket progress error");
              return NULL;
            }
            idle = 0;
            if (r->offset < r->size)
              repeat = 1;
          }
        }
      } while (repeat);
    }
    if (idle) {
      pthread_mutex_lock(&resource->threadLock);
      while (mark == myQueue->next &&
             resource->stop == 0) { // no new tasks, wait
        pthread_cond_wait(&resource->threadCond, &resource->threadLock);
      }
      pthread_mutex_unlock(&resource->threadLock);
    }
    if (resource->stop)
      return NULL;
  }
}

flagcxResult_t flagcxNetSocketGetNsockNthread(int dev, int *ns, int *nt) {
  int nSocksPerThread = flagcxParamSocketNsocksPerThread();
  int nThreads = flagcxParamSocketNthreads();
  if (nThreads > MAX_THREADS) {
    WARN("NET/Socket : FLAGCX_SOCKET_NTHREADS is greater than the maximum "
         "allowed, setting to %d",
         MAX_THREADS);
    nThreads = MAX_THREADS;
  }
  if (nThreads == -2 || nSocksPerThread == -2) {
    // Auto-detection
    int autoNt = 0, autoNs = 1; // By default, we only use the main thread and
                                // do not spawn extra threads
    char vendorPath[PATH_MAX];
    snprintf(vendorPath, PATH_MAX, "/sys/class/net/%s/device/vendor",
             flagcxNetSocketDevs[dev].devName);
    char *rPath = realpath(vendorPath, NULL);
    int fd = open(rPath, O_RDONLY);
    free(rPath);
    if (fd == -1) {
      // Could not find device vendor. This is handled silently so
      // we don't want to print an INFO error.
      TRACE(FLAGCX_NET, "Open of %s failed : %s", vendorPath, strerror(errno));
      goto end;
    }
    char vendor[7];
    strncpy(vendor, "0x0000", 7);
    int len;
    SYSCHECKVAL(read(fd, vendor, 6), "read", len);
    SYSCHECK(close(fd), "close");
    if (strcmp(vendor, "0x1d0f") == 0) { // AWS
      autoNt = 2;
      autoNs = 8;
    } else if (strcmp(vendor, "0x1ae0") == 0) { // GCP
      autoNt = 4;
      autoNs = 1;
    }
  end:
    if (nThreads == -2)
      nThreads = autoNt;
    if (nSocksPerThread == -2)
      nSocksPerThread = autoNs;
  }
  int nSocks = nSocksPerThread * nThreads;
  if (nSocks > MAX_SOCKETS) {
    nSocksPerThread = MAX_SOCKETS / nThreads;
    WARN("NET/Socket : the total number of sockets is greater than the maximum "
         "allowed, setting FLAGCX_NSOCKS_PERTHREAD to %d",
         nSocksPerThread);
    nSocks = nSocksPerThread * nThreads;
  }
  *ns = nSocks;
  *nt = nThreads;
  if (nSocks > 0)
    INFO(FLAGCX_INIT, "NET/Socket: Using %d threads and %d sockets per thread",
         nThreads, nSocksPerThread);
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketListen(int dev, void *opaqueHandle,
                                     void **listenComm) {
  if (dev < 0 ||
      dev >= flagcxNetIfs) { // data transfer socket is based on specified dev
    return flagcxInternalError;
  }
  struct flagcxNetSocketHandle *handle =
      (struct flagcxNetSocketHandle *)opaqueHandle;
  memset(handle, 0, sizeof(struct flagcxNetSocketHandle));
  static_assert(sizeof(struct flagcxNetSocketHandle) <=
                    FLAGCX_NET_HANDLE_MAXSIZE,
                "flagcxNetSocketHandle size too large");
  struct flagcxNetSocketListenComm *comm;
  FLAGCXCHECK(flagcxCalloc(&comm, 1));
  handle->magic = FLAGCX_SOCKET_MAGIC;
  FLAGCXCHECK(flagcxSocketInit(&comm->sock, &flagcxNetSocketDevs[dev].addr,
                               handle->magic, flagcxSocketTypeNetSocket, NULL,
                               1));
  FLAGCXCHECK(flagcxSocketListen(&comm->sock));
  FLAGCXCHECK(flagcxSocketGetAddr(&comm->sock, &handle->connectAddr));
  FLAGCXCHECK(
      flagcxNetSocketGetNsockNthread(dev, &comm->nSocks, &comm->nThreads));
  handle->nSocks = comm->nSocks;
  handle->nThreads = comm->nThreads;
  comm->dev = dev;
  *listenComm = comm;
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketConnect(int dev, void *opaqueHandle,
                                      void **sendComm) {
  if (dev < 0 ||
      dev >= flagcxNetIfs) { // data transfer socket is based on specified dev
    return flagcxInternalError;
  }

  int ready;
  struct flagcxNetSocketHandle *handle =
      (struct flagcxNetSocketHandle *)opaqueHandle;
  struct flagcxNetSocketCommStage *stage = &handle->stage;
  struct flagcxNetSocketComm *comm = stage->comm;
  uint8_t i = stage->iteration;
  struct flagcxSocket *sock = stage->sock;
  *sendComm = NULL;

  if (stage->state == flagcxNetSocketCommStateConnect)
    goto socket_connect_check;
  if (stage->state == flagcxNetSocketCommStateSend)
    goto socket_send;

  FLAGCXCHECK(flagcxCalloc(&comm, 1));
  stage->comm = comm;
  comm->nSocks = handle->nSocks;
  comm->nThreads = handle->nThreads;
  comm->dev = dev;
  for (; i < comm->nSocks + 1; i++) {
    sock = (i == comm->nSocks) ? &comm->ctrlSock : comm->socks + i;
    FLAGCXCHECK(flagcxSocketInit(sock, &handle->connectAddr, handle->magic,
                                 flagcxSocketTypeNetSocket, NULL, 1));

    stage->sock = sock;
    stage->state = flagcxNetSocketCommStateConnect;
    stage->iteration = i;
    FLAGCXCHECK(flagcxSocketConnect(sock));

  socket_connect_check:
    FLAGCXCHECK(flagcxSocketReady(sock, &ready));
    if (!ready)
      return flagcxSuccess;
    stage->state = flagcxNetSocketCommStateSend;

  socket_send:
    int done = 0;
    FLAGCXCHECK(flagcxSocketProgress(FLAGCX_SOCKET_SEND, sock, &i,
                                     sizeof(uint8_t), &done));
    if (done == 0)
      return flagcxSuccess;
  }
  *sendComm = comm;
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketAccept(void *listenComm, void **recvComm) {
  struct flagcxNetSocketListenComm *lComm =
      (struct flagcxNetSocketListenComm *)listenComm;
  struct flagcxNetSocketCommStage *stage = &lComm->stage;
  struct flagcxNetSocketComm *rComm = stage->comm;
  uint8_t i = stage->iteration;
  struct flagcxSocket *sock = stage->sock;
  int ready;

  *recvComm = NULL;
  if (stage->state == flagcxNetSocketCommStateAccept)
    goto socket_accept_check;
  if (stage->state == flagcxNetSocketCommStateRecv)
    goto socket_recv;

  FLAGCXCHECK(flagcxCalloc(&rComm, 1));
  stage->comm = rComm;
  rComm->nSocks = lComm->nSocks;
  rComm->nThreads = lComm->nThreads;
  rComm->dev = lComm->dev;
  for (; i < rComm->nSocks + 1; i++) {
    uint8_t sendSockIdx;

    FLAGCXCHECK(flagcxCalloc(&sock, 1));
    FLAGCXCHECK(flagcxSocketInit(sock));
    stage->sock = sock;
    stage->state = flagcxNetSocketCommStateAccept;
    stage->iteration = i;
    FLAGCXCHECK(flagcxSocketAccept(sock, &lComm->sock));

  socket_accept_check:
    FLAGCXCHECK(flagcxSocketReady(sock, &ready));
    if (!ready)
      return flagcxSuccess;

    stage->state = flagcxNetSocketCommStateRecv;
  socket_recv:
    int done = 0;
    FLAGCXCHECK(flagcxSocketProgress(FLAGCX_SOCKET_RECV, sock, &sendSockIdx,
                                     sizeof(uint8_t), &done));
    if (done == 0)
      return flagcxSuccess;

    if (sendSockIdx == rComm->nSocks)
      memcpy(&rComm->ctrlSock, sock, sizeof(struct flagcxSocket));
    else
      memcpy(rComm->socks + sendSockIdx, sock, sizeof(struct flagcxSocket));
    free(sock);
  }
  *recvComm = rComm;

  /* reset lComm state */
  stage->state = flagcxNetSocketCommStateStart;
  stage->iteration = 0;
  stage->sock = NULL;
  stage->comm = NULL;
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketGetRequest(struct flagcxNetSocketComm *comm,
                                         int op, void *data, size_t size,
                                         struct flagcxNetSocketRequest **req) {
  for (int i = 0; i < MAX_REQUESTS; i++) {
    struct flagcxNetSocketRequest *r = comm->requests + i;
    if (r->used == 0) {
      r->op = op;
      r->data = data;
      r->size = size;
      r->ctrlSock = &comm->ctrlSock;
      r->used = 1;
      r->comm = comm;
      r->nSubs = 0;
      *req = r;
      return flagcxSuccess;
    }
  }
  WARN("NET/Socket : unable to allocate requests");
  return flagcxInternalError;
}

flagcxResult_t flagcxNetSocketGetTask(struct flagcxNetSocketComm *comm, int op,
                                      void *data, int size,
                                      struct flagcxNetSocketTask **req) {
  if (comm == NULL || req == NULL || comm->nSocks <= 0 || comm->nThreads <= 0)
    return flagcxInvalidArgument;
  struct flagcxNetLaneSet lanes = {
      (uint32_t)comm->nSocks,
      (uint32_t)comm->nextSock,
  };
  uint32_t laneIndex = 0;
  FLAGCXCHECK(
      flagcxNetSelectLane(&lanes, FLAGCX_NET_LANE_UNORDERED, 0, &laneIndex));
  int tid = (int)laneIndex % comm->nThreads;
  struct flagcxNetSocketThreadResources *res = comm->threadResources + tid;
  struct flagcxNetSocketTaskQueue *queue = &res->threadTaskQueue;
  // create helper threads and prepare per-thread task queue
  if (queue->tasks == NULL) {
    // each request can be divided up to nSocks tasks, and
    // these tasks are distributed to nThreads threads,
    // we need to make sure each thread queue has enough slots for MAX_REQUESTS
    queue->len = MAX_REQUESTS * DIVUP(comm->nSocks, comm->nThreads);
    FLAGCXCHECK(flagcxCalloc(&queue->tasks, queue->len));
    queue->next = 0;
    res->comm = comm;
    pthread_mutex_init(&res->threadLock, NULL);
    pthread_cond_init(&res->threadCond, NULL);
    pthread_create(comm->helperThread + tid, NULL, persistentSocketThread, res);
    flagcxSetThreadName(comm->helperThread[tid], "FLAGCX Sock%c%1u%2u%2u",
                        op == FLAGCX_SOCKET_SEND ? 'S' : 'R', comm->dev, tid,
                        comm->cudaDev);
  }
  struct flagcxNetSocketTask *r = queue->tasks + queue->next;
  if (r->used == 0) {
    r->op = op;
    r->data = data;
    r->size = size;
    r->sock = comm->socks + laneIndex;
    r->offset = 0;
    r->result = flagcxSuccess;
    FLAGCXCHECK(
        flagcxNetCommitLane(&lanes, FLAGCX_NET_LANE_UNORDERED, laneIndex));
    comm->nextSock = (int)lanes.unorderedCursor;
    r->used = 1;
    *req = r;
    pthread_mutex_lock(&res->threadLock);
    queue->next = (queue->next + 1) % queue->len;
    pthread_cond_signal(&res->threadCond);
    pthread_mutex_unlock(&res->threadLock);
    return flagcxSuccess;
  }
  WARN("NET/Socket : unable to allocate subtasks");
  return flagcxInternalError;
}

flagcxResult_t flagcxNetSocketTest(void *request, int *done, int *size) {
  *done = 0;
  struct flagcxNetSocketRequest *r = (struct flagcxNetSocketRequest *)request;
  if (r == NULL) {
    WARN("NET/Socket : test called with NULL request");
    return flagcxInternalError;
  }
  if (r->op == FLAGCX_SOCK_OP_ONESHOT) {
    // One-sided work completes before iput/iputSignal return.
    if (size != NULL)
      *size = r->size;
    *done = 1;
    return flagcxSuccess;
  }
  if (r->used == 1) { /* try to send/recv size */
    int data = r->size;
    int offset = 0;
    FLAGCXCHECK(
        flagcxSocketProgress(r->op, r->ctrlSock, &data, sizeof(int), &offset));

    if (offset == 0)
      return flagcxSuccess; /* Not ready -- retry later */

    // Not sure we could ever receive less than 4 bytes, but just in case ...
    if (offset < sizeof(int))
      FLAGCXCHECK(
          flagcxSocketWait(r->op, r->ctrlSock, &data, sizeof(int), &offset));

    // Check size is less or equal to the size provided by the user
    if (r->op == FLAGCX_SOCKET_RECV && data > r->size) {
      char line[SOCKET_NAME_MAXLEN + 1];
      union flagcxSocketAddress addr;
      flagcxSocketGetAddr(r->ctrlSock, &addr);
      WARN(
          "NET/Socket : peer %s message truncated : receiving %d bytes instead of %d. If you believe your socket network is in healthy state, \
          there may be a mismatch in collective sizes or environment settings (e.g. FLAGCX_PROTO, FLAGCX_ALGO) between ranks",
          flagcxSocketToString(&addr, line), data, r->size);
      return flagcxInvalidUsage;
    }
    r->size = data;
    r->offset = 0;
    r->used = 2; // done exchanging size
    // divide into subtasks
    int chunkOffset = 0, i = 0;
    if (r->comm->nSocks > 0) {
      // each request can be divided up to nSocks tasks
      int taskSize = std::max(MIN_CHUNKSIZE, DIVUP(r->size, r->comm->nSocks));
      while (chunkOffset < r->size) {
        int chunkSize = std::min(taskSize, r->size - chunkOffset);
        FLAGCXCHECK(flagcxNetSocketGetTask(r->comm, r->op,
                                           (char *)(r->data) + chunkOffset,
                                           chunkSize, r->tasks + i++));
        chunkOffset += chunkSize;
      }
    }
    r->nSubs = i;
  }
  if (r->used == 2) { // already exchanged size
    if (r->nSubs > 0) {
      int nCompleted = 0;
      for (int i = 0; i < r->nSubs; i++) {
        struct flagcxNetSocketTask *sub = r->tasks[i];
        if (sub->result != flagcxSuccess)
          return sub->result;
        if (sub->offset == sub->size)
          nCompleted++;
      }
      if (nCompleted == r->nSubs) {
        if (size)
          *size = r->size;
        *done = 1;
        r->used = 0;
        for (int i = 0; i < r->nSubs; i++) {
          struct flagcxNetSocketTask *sub = r->tasks[i];
          sub->used = 0;
        }
      }
    } else { // progress request using main thread
      if (r->offset < r->size) {
        FLAGCXCHECK(flagcxSocketProgress(r->op, r->ctrlSock, r->data, r->size,
                                         &r->offset));
      }
      if (r->offset == r->size) {
        if (size)
          *size = r->size;
        *done = 1;
        r->used = 0;
      }
    }
  }
  return flagcxSuccess;
}

/* ==========================================================================
 * One-sided support for the socket transport.
 *
 * The socket transport has no remote memory keys, so an inter-node peer's
 * registered region is reached through an IPC mapping instead: the owner
 * exports a vendor IPC handle to /dev/shm and a peer imports it once, caching
 * the resulting mapping. Data movement and remote signal updates run through
 * flagcxSockDevTransfer() so they can make progress while the application
 * kernel is spinning in waitSignal.
 * ========================================================================== */

#define FLAGCX_SOCK_MR_MAGIC 0x524d4b53u /* "SKMR" */

struct flagcxSockMrFile {
  uint32_t magic;
  uint32_t key;
  int32_t pid;
  uint32_t handleSize;
  uint64_t addr;
  uint64_t size;
  uint64_t allocSize;
  uint64_t userOffset;
  uint8_t handle[FLAGCX_IPC_HANDLE_SIZE];
};

struct flagcxSockMr {
  uintptr_t addr;
  size_t size;
  uint32_t key;
  int isHost;
  int hasIpc;
  size_t allocSize;
  size_t userOffset;
  size_t handleSize;
  flagcxIpcHandleData handleData;
  struct flagcxSockMr *next;
};

static struct flagcxSockMr *g_sockMrs = NULL;
static pthread_mutex_t g_sockMrLock = PTHREAD_MUTEX_INITIALIZER;

struct flagcxSockPeerMr {
  uint32_t key;
  void *base;
  size_t allocSize;
  size_t userOffset;
  uint64_t ownerVa;
  int ownerPid;
  struct flagcxSockPeerMr *next;
};

static struct flagcxSockPeerMr *g_sockPeerMrs = NULL;
static pthread_mutex_t g_sockPeerMrLock = PTHREAD_MUTEX_INITIALIZER;

// Stateless completion token: one-sided work is finished before it returns, so
// every caller may share one token. `op` is the discriminator flagcxNetSocketTest
// keys on, so it must be set before the token is handed out -- a zeroed request
// would silently report "not done" and stall the FIFO completion counter.
static struct flagcxNetSocketRequest g_sockOneShotRequest;

static void *flagcxNetSocketOneShotToken(void) {
  g_sockOneShotRequest.op = FLAGCX_SOCK_OP_ONESHOT;
  g_sockOneShotRequest.used = 0;
  return (void *)&g_sockOneShotRequest;
}

#if defined(COMPILE_KERNEL_HOST) && defined(USE_ILUVATAR_ADAPTOR)
extern "C" int flagcxSockDevTransfer(void *dst, const void *src, size_t size,
                                     int copyType, void *signalPtr,
                                     unsigned long long signalValue);
extern "C" int flagcxSockDevMemRange(void *ptr, void **base, size_t *size);
#define FLAGCX_SOCK_HAS_DEV_TRANSFER 1
#endif

// Host-mediated one-sided support.
//
// Upstream leaves iput/iget/iputSignal/getMrInfo NULL for this adaptor ("not
// supported on socket").  Nine call sites probe exactly those pointers before
// letting a request into the one-sided implementation, so advertising them
// conditionally at run time would still change behaviour: the probes would pass
// and the request would fail deeper instead of being refused up front.  The
// slots are therefore advertised only when this build really has the device
// transfer helper the path needs, which is a compile-time property.
//
// On top of that compile-time switch, a build that has the helper may still turn
// the path off (FLAGCX_SOCK_ONESIDED=0) or on (FLAGCX_SOCK_ONESIDED=1); the
// default is the device family it has been validated on.
static int flagcxSockOneSidedEnabled(void) {
#ifdef FLAGCX_SOCK_HAS_DEV_TRANSFER
  const char *opt = getenv("FLAGCX_SOCK_ONESIDED");
  if (opt != NULL && opt[0] != '\0')
    return opt[0] != '0';
  return (deviceAdaptor != NULL &&
          deviceAdaptor->gdrDeviceFamily == FLAGCX_GDR_DEVICE_ILUVATAR)
             ? 1
             : 0;
#else
  return 0;
#endif
}

// Resolve the allocation that backs `buff` so the exported IPC handle and the
// user offset are consistent: cudaIpcOpenMemHandle always yields the
// allocation base, never a mid-allocation address.
static flagcxResult_t flagcxNetSocketExportRange(const void *buff, size_t size,
                                                 void **exportBase,
                                                 size_t *allocSize,
                                                 size_t *userOffset) {
#ifdef FLAGCX_SOCK_HAS_DEV_TRANSFER
  void *rangeBase = NULL;
  size_t rangeSize = 0;
  if (flagcxSockDevMemRange(const_cast<void *>(buff), &rangeBase,
                            &rangeSize) == 0 &&
      rangeBase != NULL && rangeSize >= size) {
    uintptr_t base = (uintptr_t)rangeBase;
    uintptr_t user = (uintptr_t)buff;
    if (user >= base && user - base <= rangeSize &&
        size <= rangeSize - (user - base)) {
      *exportBase = rangeBase;
      *allocSize = rangeSize;
      *userOffset = user - base;
      return flagcxSuccess;
    }
  }
#endif
  return flagcxGetIpcExportRange(buff, size, exportBase, allocSize,
                                 userOffset);
}

// Diagnostics only: read device memory on the transfer stream, which is the
// only stream that can make progress while a kernel spins in waitSignal.
static int flagcxNetSocketDevRead(void *dstHost, const void *srcDev,
                                  size_t bytes) {
#ifdef FLAGCX_SOCK_HAS_DEV_TRANSFER
  return flagcxSockDevTransfer(dstHost, srcDev, bytes, 1, NULL, 0);
#else
  (void)dstHost; (void)srcDev; (void)bytes;
  return -1;
#endif
}

static void flagcxNetSocketMrPath(uint32_t key, char *buf, size_t len) {
  snprintf(buf, len, "/dev/shm/flagcx-sockmr-%08x", (unsigned)key);
}

static void flagcxNetSocketMrPublish(const struct flagcxSockMr *mr) {
  if (mr == NULL || !mr->hasIpc || mr->handleSize == 0)
    return;
  char finalPath[64];
  char tmpPath[80];
  flagcxNetSocketMrPath(mr->key, finalPath, sizeof(finalPath));
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp%d", finalPath, (int)getpid());

  struct flagcxSockMrFile rec;
  memset(&rec, 0, sizeof(rec));
  rec.magic = FLAGCX_SOCK_MR_MAGIC;
  rec.key = mr->key;
  rec.pid = (int32_t)getpid();
  rec.handleSize = (uint32_t)mr->handleSize;
  rec.addr = (uint64_t)mr->addr;
  rec.size = (uint64_t)mr->size;
  rec.allocSize = (uint64_t)mr->allocSize;
  rec.userOffset = (uint64_t)mr->userOffset;
  memcpy(rec.handle, &mr->handleData, mr->handleSize);

  int fd = open(tmpPath, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd < 0) {
    WARN("NET/Socket : cannot publish MR key 0x%x (%s)", (unsigned)mr->key,
         strerror(errno));
    return;
  }
  ssize_t written = write(fd, &rec, sizeof(rec));
  close(fd);
  if (written != (ssize_t)sizeof(rec) || rename(tmpPath, finalPath) != 0) {
    WARN("NET/Socket : cannot publish MR key 0x%x", (unsigned)mr->key);
    unlink(tmpPath);
    return;
  }
  INFO(FLAGCX_NET, "NET/Socket : published MR key 0x%x addr=0x%lx size=%zu",
       (unsigned)mr->key, (unsigned long)mr->addr, mr->size);
}

static void flagcxNetSocketMrUnpublish(uint32_t key) {
  char path[64];
  flagcxNetSocketMrPath(key, path, sizeof(path));
  unlink(path);
}

// Owner identity published with a key (trace only).
static void flagcxNetSocketMrOwner(uint32_t key, uint64_t *ownerVa, int *pid);

static flagcxResult_t flagcxNetSocketMrImport(uint32_t key, void **baseOut,
                                              size_t *allocOut,
                                              size_t *offOut) {
  if (key == 0 || baseOut == NULL)
    return flagcxInvalidArgument;
  pthread_mutex_lock(&g_sockPeerMrLock);
  for (struct flagcxSockPeerMr *it = g_sockPeerMrs; it != NULL; it = it->next) {
    if (it->key == key) {
      *baseOut = it->base;
      if (allocOut != NULL)
        *allocOut = it->allocSize;
      if (offOut != NULL)
        *offOut = it->userOffset;
      pthread_mutex_unlock(&g_sockPeerMrLock);
      return flagcxSuccess;
    }
  }

  // A key can name one of our own registrations: one-sided all-to-all includes
  // a self entry, and cudaIpcOpenMemHandle refuses handles created by this very
  // process. Resolve it locally.
  // g_sockMrs is mutated by regMr/deregMr under g_sockMrLock; traverse it under
  // that lock too. Lock order here is peer -> registry, and no path takes the
  // two in the opposite order.
  pthread_mutex_lock(&g_sockMrLock);
  for (struct flagcxSockMr *own = g_sockMrs; own != NULL; own = own->next) {
    if (own->key != key)
      continue;
    // Copy what we need while the registry lock still guarantees the entry is
    // alive, then drop it before the allocation.
    const uintptr_t ownAddr = own->addr;
    const size_t ownSize = own->size;
    pthread_mutex_unlock(&g_sockMrLock);
    struct flagcxSockPeerMr *self =
        (struct flagcxSockPeerMr *)calloc(1, sizeof(*self));
    if (self == NULL) {
      pthread_mutex_unlock(&g_sockPeerMrLock);
      return flagcxSystemError;
    }
    self->key = key;
    self->base = (void *)ownAddr;
    self->allocSize = ownSize;
    self->userOffset = 0;
    self->ownerVa = (uint64_t)ownAddr;
    self->ownerPid = (int)getpid();
    self->next = g_sockPeerMrs;
    g_sockPeerMrs = self;
    *baseOut = self->base;
    if (allocOut != NULL)
      *allocOut = self->allocSize;
    if (offOut != NULL)
      *offOut = self->userOffset;
    pthread_mutex_unlock(&g_sockPeerMrLock);
    return flagcxSuccess;
  }

  pthread_mutex_unlock(&g_sockMrLock);

  char path[64];
  flagcxNetSocketMrPath(key, path, sizeof(path));
  struct flagcxSockMrFile rec;
  memset(&rec, 0, sizeof(rec));
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    pthread_mutex_unlock(&g_sockPeerMrLock);
    WARN("NET/Socket : MR key 0x%x is not published on this host",
         (unsigned)key);
    return flagcxInternalError;
  }
  ssize_t got = read(fd, &rec, sizeof(rec));
  close(fd);
  if (got != (ssize_t)sizeof(rec) || rec.magic != FLAGCX_SOCK_MR_MAGIC ||
      rec.key != key || rec.handleSize == 0 ||
      rec.handleSize > sizeof(rec.handle)) {
    pthread_mutex_unlock(&g_sockPeerMrLock);
    WARN("NET/Socket : MR key 0x%x has a malformed descriptor",
         (unsigned)key);
    return flagcxInternalError;
  }
  if (deviceAdaptor == NULL || deviceAdaptor->ipcMemHandleOpen == NULL) {
    pthread_mutex_unlock(&g_sockPeerMrLock);
    return flagcxNotSupported;
  }

  flagcxIpcMemHandle_t handle = (flagcxIpcMemHandle_t)&rec.handle[0];
  void *peerBase = NULL;
  flagcxResult_t res = deviceAdaptor->ipcMemHandleOpen(handle, &peerBase);
  if (res != flagcxSuccess || peerBase == NULL) {
    pthread_mutex_unlock(&g_sockPeerMrLock);
    WARN("NET/Socket : cannot open the IPC handle for MR key 0x%x (%d)",
         (unsigned)key, (int)res);
    return res == flagcxSuccess ? flagcxSystemError : res;
  }

  struct flagcxSockPeerMr *entry =
      (struct flagcxSockPeerMr *)calloc(1, sizeof(*entry));
  if (entry == NULL) {
    pthread_mutex_unlock(&g_sockPeerMrLock);
    if (deviceAdaptor->ipcMemHandleClose != NULL)
      deviceAdaptor->ipcMemHandleClose(peerBase);
    return flagcxSystemError;
  }
  entry->key = key;
  entry->base = peerBase;
  entry->allocSize = (size_t)rec.allocSize;
  entry->userOffset = (size_t)rec.userOffset;
  entry->ownerVa = (uint64_t)rec.addr;
  entry->ownerPid = (int)rec.pid;
  entry->next = g_sockPeerMrs;
  g_sockPeerMrs = entry;

  *baseOut = entry->base;
  if (allocOut != NULL)
    *allocOut = entry->allocSize;
  if (offOut != NULL)
    *offOut = entry->userOffset;
  pthread_mutex_unlock(&g_sockPeerMrLock);
  INFO(FLAGCX_NET, "NET/Socket : imported MR key 0x%x base=%p offset=%zu",
       (unsigned)key, peerBase, entry->userOffset);
  return flagcxSuccess;
}

static void flagcxNetSocketMrOwner(uint32_t key, uint64_t *ownerVa, int *pid) {
  if (ownerVa != NULL)
    *ownerVa = 0;
  if (pid != NULL)
    *pid = -1;
  pthread_mutex_lock(&g_sockPeerMrLock);
  for (struct flagcxSockPeerMr *it = g_sockPeerMrs; it != NULL; it = it->next) {
    if (it->key == key) {
      if (ownerVa != NULL)
        *ownerVa = it->ownerVa;
      if (pid != NULL)
        *pid = it->ownerPid;
      break;
    }
  }
  pthread_mutex_unlock(&g_sockPeerMrLock);
}

// Address of a peer rank's registered region inside this process.
// Import a peer mapping and verify it is the region we think it is.
static flagcxResult_t
flagcxNetSocketImportChecked(const struct flagcxOneSideHandleInfo *info,
                             int rank, void **baseOut) {
  uint32_t key = info->mrInfos[rank].rkeys[0];
  void *base = NULL;
  size_t allocSize = 0;
  size_t userOffset = 0;
  FLAGCXCHECK(flagcxNetSocketMrImport(key, &base, &allocSize, &userOffset));
  static int checked = 0;
  if (getenv("FLAGCX_MRCHK") != NULL && checked < 64) {
    checked++;
    uint64_t ownerVa = 0;
    int pid = -1;
    flagcxNetSocketMrOwner(key, &ownerVa, &pid);
    fprintf(stderr,
            "[MRCHK] pid=%d rank=%d key=0x%x ownerPid=%d ownerVa=0x%llx "
            "expectedVa=0x%lx %s\n",
            (int)getpid(), rank, (unsigned)key, pid,
            (unsigned long long)ownerVa,
            (unsigned long)info->baseVas[rank],
            (ownerVa == (uint64_t)info->baseVas[rank]) ? "MATCH" : "MISMATCH");
    fflush(stderr);
  }
  if (userOffset > allocSize)
    return flagcxInternalError;
  *baseOut = (void *)((uintptr_t)base + (uintptr_t)userOffset);
  return flagcxSuccess;
}

static flagcxResult_t
flagcxNetSocketPeerRegion(const struct flagcxOneSideHandleInfo *info, int rank,
                          uint64_t offset, size_t size, void **out) {
  if (info == NULL || info->baseVas == NULL || info->regionSizes == NULL ||
      info->mrInfos == NULL || out == NULL || rank < 0 || rank >= info->nRanks)
    return flagcxInvalidArgument;
  if (offset > info->regionSizes[rank] ||
      size > info->regionSizes[rank] - offset)
    return flagcxInvalidArgument;
  void *region = NULL;
  FLAGCXCHECK(flagcxNetSocketImportChecked(info, rank, &region));
  *out = (void *)((uintptr_t)region + (uintptr_t)offset);
  return flagcxSuccess;
}

// Address of this rank's own registered region.
static flagcxResult_t
flagcxNetSocketLocalRegion(const struct flagcxOneSideHandleInfo *info, int rank,
                           uint64_t offset, size_t size, void **out,
                           int *isHost) {
  if (info == NULL || info->baseVas == NULL || info->regionSizes == NULL ||
      out == NULL || rank < 0 || rank >= info->nRanks)
    return flagcxInvalidArgument;
  if (offset > info->regionSizes[rank] ||
      size > info->regionSizes[rank] - offset)
    return flagcxInvalidArgument;
  *out = (void *)(info->baseVas[rank] + (uintptr_t)offset);
  if (isHost != NULL) {
    struct flagcxSockMr *mr = (struct flagcxSockMr *)info->localMrHandle;
    *isHost = (mr != NULL && mr->addr != 0 && mr->isHost) ? 1 : 0;
  }
  return flagcxSuccess;
}

// copyType is a flagcxMemcpyType_t. size == 0 performs only the signal add.
static flagcxResult_t flagcxNetSocketTransfer(void *dst, const void *src,
                                              size_t size, int copyType,
                                              void *signalPtr,
                                              uint64_t signalValue) {
  if (dst == NULL && signalPtr == NULL)
    return flagcxInvalidArgument;
#ifdef FLAGCX_SOCK_HAS_DEV_TRANSFER
  int rc = flagcxSockDevTransfer(dst, src, size, copyType, signalPtr,
                                 (unsigned long long)signalValue);
  if (rc != 0) {
    WARN("NET/Socket : peer transfer failed (%d)", rc);
    return flagcxUnhandledDeviceError;
  }
  return flagcxSuccess;
#else
  (void)dst; (void)src; (void)size; (void)copyType; (void)signalPtr;
  (void)signalValue;
  return flagcxNotSupported;
#endif
}

// RDMA READ emulation: pull the peer's region into a local buffer.
flagcxResult_t flagcxNetSocketIget(void *sendComm, uint64_t srcOff,
                                   uint64_t dstOff, size_t size, int srcRank,
                                   int dstRank, void **srcHandles,
                                   void **dstHandles, void **request) {
  (void)sendComm;
  // Same answer upstream gives by leaving the slot NULL.
  if (!flagcxSockOneSidedEnabled())
    return flagcxNotSupported;
  if (request == NULL)
    return flagcxInvalidArgument;
  *request = NULL;
  if (size == 0)
    return flagcxInvalidArgument;

  void *peerSrc = NULL;
  void *localDst = NULL;
  int isHostDst = 0;
  FLAGCXCHECK(flagcxNetSocketPeerRegion(
      (const struct flagcxOneSideHandleInfo *)srcHandles, srcRank, srcOff, size,
      &peerSrc));
  FLAGCXCHECK(flagcxNetSocketLocalRegion(
      (const struct flagcxOneSideHandleInfo *)dstHandles, dstRank, dstOff, size,
      &localDst, &isHostDst));
  FLAGCXCHECK(flagcxNetSocketTransfer(
      localDst, peerSrc, size,
      isHostDst ? flagcxMemcpyDeviceToHost : flagcxMemcpyDeviceToDevice, NULL,
      0));

  if (getenv("FLAGCX_KP_TRACE") != NULL) {
    static int ig_c = 0;
    if (ig_c++ < 200) {
      fprintf(stderr,
              "[SOCKIGET] srcOff=%llu dstOff=%llu size=%zu srcRank=%d "
              "dstRank=%d peerSrc=%p localDst=%p hostDst=%d\n",
              (unsigned long long)srcOff, (unsigned long long)dstOff, size,
              srcRank, dstRank, peerSrc, localDst, isHostDst);
      fflush(stderr);
    }
  }
  *request = flagcxNetSocketOneShotToken();
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketRegMr(void *comm, void *data, size_t size,
                                    int type, int mrFlags, void **mhandle) {
  (void)comm;
  (void)mrFlags;
  if (data == NULL || size == 0 || mhandle == NULL)
    return flagcxInvalidArgument;
  struct flagcxSockMr *mr =
      (struct flagcxSockMr *)calloc(1, sizeof(struct flagcxSockMr));
  if (mr == NULL)
    return flagcxSystemError;
  mr->addr = (uintptr_t)data;
  mr->size = size;
  mr->isHost = (type == FLAGCX_PTR_HOST) ? 1 : 0;
  // Opaque, non-zero, per-process key: the one-sided layer all-gathers it and
  // hands it back as the peer's rkey.
  mr->key = (uint32_t)((((uint64_t)mr->addr >> 8) ^
                        ((uint64_t)(uint32_t)getpid() << 12)) |
                       1u);

  // Export device memory so that a peer process on this host can reach it.
  if (!mr->isHost && flagcxSockOneSidedEnabled() && deviceAdaptor != NULL &&
      deviceAdaptor->ipcMemHandleCreate != NULL &&
      deviceAdaptor->ipcMemHandleGet != NULL) {
    void *exportBase = NULL;
    size_t allocSize = 0;
    size_t userOffset = 0;
    flagcxResult_t res = flagcxNetSocketExportRange(data, size, &exportBase,
                                                   &allocSize, &userOffset);
    if (res == flagcxSuccess) {
      flagcxIpcMemHandle_t handle = NULL;
      size_t handleSize = 0;
      res = deviceAdaptor->ipcMemHandleCreate(&handle, &handleSize);
      if (res == flagcxSuccess && handle != NULL && handleSize > 0 &&
          handleSize <= sizeof(flagcxIpcHandleData)) {
        res = deviceAdaptor->ipcMemHandleGet(handle, exportBase);
        if (res == flagcxSuccess) {
          res = flagcxStoreIpcHandle(&mr->handleData, (const void *)handle,
                                     handleSize);
          if (res == flagcxSuccess) {
            mr->hasIpc = 1;
            mr->allocSize = allocSize;
            mr->userOffset = userOffset;
            mr->handleSize = handleSize;
          }
        }
      }
      if (handle != NULL && deviceAdaptor->ipcMemHandleFree != NULL)
        deviceAdaptor->ipcMemHandleFree(handle);
    }
    if (!mr->hasIpc)
      INFO(FLAGCX_NET, "NET/Socket : no IPC export for buff=%p size=%zu", data,
           size);
  }

  pthread_mutex_lock(&g_sockMrLock);
  mr->next = g_sockMrs;
  g_sockMrs = mr;
  pthread_mutex_unlock(&g_sockMrLock);
  *mhandle = (void *)mr;
  // Only a peer that uses the one-sided path needs the registry entry, and only
  // it should create files under /dev/shm. A two-sided-only deployment would
  // otherwise leave flagcx-sockmr-* files behind if it exits abnormally.
  if (flagcxSockOneSidedEnabled())
    flagcxNetSocketMrPublish(mr);

  if (getenv("FLAGCX_KP_TRACE") != NULL) {
    fprintf(stderr,
            "[SOCKREG] pid=%d addr=0x%lx size=%zu type=%d key=0x%x ipc=%d "
            "allocSize=%zu userOffset=%zu\\n",
            (int)getpid(), (unsigned long)mr->addr, mr->size, type,
            (unsigned)mr->key, mr->hasIpc, mr->allocSize, mr->userOffset);
    fflush(stderr);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketDeregMr(void *comm, void *mhandle) {
  (void)comm;
  struct flagcxSockMr *mr = (struct flagcxSockMr *)mhandle;
  if (mr == NULL)
    return flagcxSuccess;
  flagcxNetSocketMrUnpublish(mr->key);
  pthread_mutex_lock(&g_sockMrLock);
  struct flagcxSockMr **link = &g_sockMrs;
  while (*link != NULL && *link != mr)
    link = &(*link)->next;
  if (*link == mr)
    *link = mr->next;
  pthread_mutex_unlock(&g_sockMrLock);
  free(mr);
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketIsend(void *sendComm, void *data, size_t size,
                                    int tag, void *mhandle, void *phandle,
                                    void **request) {
  struct flagcxNetSocketComm *comm = (struct flagcxNetSocketComm *)sendComm;
  FLAGCXCHECK(
      flagcxNetSocketGetRequest(comm, FLAGCX_SOCKET_SEND, data, size,
                                (struct flagcxNetSocketRequest **)request));
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketIrecv(void *recvComm, int n, void **data,
                                    size_t *sizes, int *tags, void **mhandles,
                                    void **phandles, void **request) {
  struct flagcxNetSocketComm *comm = (struct flagcxNetSocketComm *)recvComm;
  if (n != 1)
    return flagcxInternalError;
  FLAGCXCHECK(
      flagcxNetSocketGetRequest(comm, FLAGCX_SOCKET_RECV, data[0], sizes[0],
                                (struct flagcxNetSocketRequest **)request));
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketIflush(void *recvComm, int n, void **data,
                                     int *sizes, void **mhandles,
                                     void **request) {
  // We don't support CUDA pointers, so we don't need a flush operation
  return flagcxInternalError;
}

flagcxResult_t flagcxNetSocketCloseListen(void *opaqueComm) {
  struct flagcxNetSocketListenComm *comm =
      (struct flagcxNetSocketListenComm *)opaqueComm;
  if (comm) {
    int ready;
    FLAGCXCHECK(flagcxSocketReady(&comm->sock, &ready));
    if (ready)
      FLAGCXCHECK(flagcxSocketClose(&comm->sock));
    free(comm);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketClose(void *opaqueComm) {
  struct flagcxNetSocketComm *comm = (struct flagcxNetSocketComm *)opaqueComm;
  if (comm) {
    for (int i = 0; i < comm->nThreads; i++) {
      struct flagcxNetSocketThreadResources *res = comm->threadResources + i;
      if (comm->helperThread[i]) {
        pthread_mutex_lock(&res->threadLock);
        res->stop = 1;
        pthread_cond_signal(&res->threadCond);
        pthread_mutex_unlock(&res->threadLock);
        pthread_join(comm->helperThread[i], NULL);
      }
      free(res->threadTaskQueue.tasks);
    }
    int ready;
    FLAGCXCHECK(flagcxSocketReady(&comm->ctrlSock, &ready));
    if (ready)
      FLAGCXCHECK(flagcxSocketClose(&comm->ctrlSock));
    for (int i = 0; i < comm->nSocks; i++) {
      FLAGCXCHECK(flagcxSocketReady(&comm->socks[i], &ready));
      if (ready)
        FLAGCXCHECK(flagcxSocketClose(&comm->socks[i]));
    }
    free(comm);
  }
  return flagcxSuccess;
}


/* --- One-sided support: peers are reached through IPC mappings --- */
flagcxResult_t flagcxNetSocketGetMrInfo(void *mhandle,
                                        struct flagcxNetMrInfo *info) {
  if (mhandle == NULL || info == NULL)
    return flagcxInvalidArgument;
  memset(info, 0, sizeof(*info));
  struct flagcxSockMr *mr = (struct flagcxSockMr *)mhandle;
  info->nKeys = 1;
  info->lkeys[0] = mr->key;
  info->rkeys[0] = mr->key;
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketIput(void *sendComm, uint64_t srcOff,
                                   uint64_t dstOff, size_t size, int srcRank,
                                   int dstRank, void **srcHandles,
                                   void **dstHandles, void **request) {
  (void)sendComm;
  // Same answer upstream gives by leaving the slot NULL.
  if (!flagcxSockOneSidedEnabled())
    return flagcxNotSupported;
  if (request == NULL)
    return flagcxInvalidArgument;
  *request = NULL;
  if (size == 0)
    return flagcxInvalidArgument;

  void *srcPtr = NULL;
  void *dstPtr = NULL;
  int isHostSrc = 0;
  FLAGCXCHECK(flagcxNetSocketLocalRegion(
      (const struct flagcxOneSideHandleInfo *)srcHandles, srcRank, srcOff, size,
      &srcPtr, &isHostSrc));
  FLAGCXCHECK(flagcxNetSocketPeerRegion(
      (const struct flagcxOneSideHandleInfo *)dstHandles, dstRank, dstOff, size,
      &dstPtr));
  FLAGCXCHECK(flagcxNetSocketTransfer(
      dstPtr, srcPtr, size,
      isHostSrc ? flagcxMemcpyHostToDevice : flagcxMemcpyDeviceToDevice, NULL,
      0));

  if (getenv("FLAGCX_KP_TRACE") != NULL) {
    static int ip_c = 0;
    if (ip_c++ < 200) {
      fprintf(stderr,
              "[SOCKIPUT] srcOff=%llu dstOff=%llu size=%zu srcRank=%d "
              "dstRank=%d src=%p dst=%p hostSrc=%d\n",
              (unsigned long long)srcOff, (unsigned long long)dstOff, size,
              srcRank, dstRank, srcPtr, dstPtr, isHostSrc);
      fflush(stderr);
    }
  }
  *request = flagcxNetSocketOneShotToken();
  return flagcxSuccess;
}

flagcxResult_t flagcxNetSocketIputSignal(void *sendComm, uint64_t srcOff,
                                         uint64_t dstOff, size_t size,
                                         int srcRank, int dstRank,
                                         void **srcHandles, void **dstHandles,
                                         uint64_t signalOff,
                                         void **signalHandles,
                                         uint64_t signalValue,
                                         void **request) {
  (void)sendComm;
  // Same answer upstream gives by leaving the slot NULL.
  if (!flagcxSockOneSidedEnabled())
    return flagcxNotSupported;
  if (request == NULL)
    return flagcxInvalidArgument;
  *request = NULL;

  void *srcPtr = NULL;
  void *dstPtr = NULL;
  int isHostSrc = 0;
  if (size > 0) {
    FLAGCXCHECK(flagcxNetSocketLocalRegion(
        (const struct flagcxOneSideHandleInfo *)srcHandles, srcRank, srcOff,
        size, &srcPtr, &isHostSrc));
    FLAGCXCHECK(flagcxNetSocketPeerRegion(
        (const struct flagcxOneSideHandleInfo *)dstHandles, dstRank, dstOff,
        size, &dstPtr));
  }

  void *signalPtr = NULL;
  if (signalHandles != NULL) {
    FLAGCXCHECK(flagcxNetSocketPeerRegion(
        (const struct flagcxOneSideHandleInfo *)signalHandles, dstRank,
        signalOff, sizeof(uint64_t), &signalPtr));
  }
  if (size == 0 && signalPtr == NULL)
    return flagcxInvalidArgument;

  if (getenv("FLAGCX_KP_TRACE") != NULL) {
    static int pre_c = 0;
    if (pre_c++ < 400) {
      fprintf(stderr, "[SIGPRE] pid=%d dstRank=%d sigOff=%llu dst=%p sig=%p\n",
              (int)getpid(), dstRank, (unsigned long long)signalOff, dstPtr,
              signalPtr);
      fflush(stderr);
    }
  }
  FLAGCXCHECK(flagcxNetSocketTransfer(
      dstPtr, srcPtr, size,
      isHostSrc ? flagcxMemcpyHostToDevice : flagcxMemcpyDeviceToDevice,
      signalPtr, signalValue));
  if (getenv("FLAGCX_KP_TRACE") != NULL) {
    static int post_c = 0;
    if (post_c++ < 400) {
      fprintf(stderr, "[SIGDONE] pid=%d dstRank=%d sigOff=%llu\n", (int)getpid(),
              dstRank, (unsigned long long)signalOff);
      fflush(stderr);
    }
  }

  if (getenv("FLAGCX_KP_TRACE") != NULL) {
    static int ips_c = 0;
    if (ips_c++ < 200) {
      fprintf(stderr,
              "[SOCKIPUTSIG] srcOff=%llu dstOff=%llu size=%zu srcRank=%d "
              "dstRank=%d sigOff=%llu sigVal=%llu dst=%p sigPtr=%p\n",
              (unsigned long long)srcOff, (unsigned long long)dstOff, size,
              srcRank, dstRank, (unsigned long long)signalOff,
              (unsigned long long)signalValue, dstPtr, signalPtr);
      fflush(stderr);
    }
  }
  if (signalPtr != NULL && getenv("FLAGCX_SIGDUMP") != NULL) {
    static int dumpN = 0;
    if (dumpN++ < 24) {
      unsigned long long peerWord = 0;
      int prc = flagcxNetSocketDevRead(&peerWord, signalPtr, sizeof(peerWord));
      fprintf(stderr, "[SIGDUMP] pid=%d dstRank=%d sigOff=%llu peerWord=%llu "
                      "rc=%d\n",
              (int)getpid(), dstRank, (unsigned long long)signalOff, peerWord,
              prc);
      struct flagcxSockMr *localMr = NULL;
      if (signalHandles != NULL)
        localMr = (struct flagcxSockMr *)((const struct
                                          flagcxOneSideHandleInfo *)signalHandles)
                      ->localMrHandle;
      if (localMr != NULL && localMr->size > 0 && localMr->size <= 65536) {
        unsigned long long *buf =
            (unsigned long long *)malloc(localMr->size);
        if (buf != NULL) {
          int rc =
              flagcxNetSocketDevRead(buf, (void *)localMr->addr, localMr->size);
          fprintf(stderr, "[SIGDUMP] pid=%d localBase=%p size=%zu rc=%d nz:",
                  (int)getpid(), (void *)localMr->addr, localMr->size, rc);
          for (size_t w = 0; w * 8 + 8 <= localMr->size; w++)
            if (buf[w] != 0)
              fprintf(stderr, " [%zu]=%llu", w, buf[w]);
          fprintf(stderr, "\n");
          free(buf);
        }
      }
      fflush(stderr);
    }
  }
  *request = flagcxNetSocketOneShotToken();
  return flagcxSuccess;
}

flagcxNetAdaptor flagcxNetSocket = {
    // Basic functions
    "Socket",
    flagcxNetSocketInit,
    flagcxNetSocketDevices,
    flagcxNetSocketGetProperties,

    // Setup functions
    flagcxNetSocketListen,
    flagcxNetSocketConnect,
    flagcxNetSocketAccept,
    flagcxNetSocketClose, // closeSend
    flagcxNetSocketClose, // closeRecv (same as closeSend for socket)
    flagcxNetSocketCloseListen,

    // Memory region functions
    flagcxNetSocketRegMr,
    NULL, // regMrDmaBuf - No DMA-BUF support
    flagcxNetSocketDeregMr,

    // Two-sided functions
    flagcxNetSocketIsend,
    flagcxNetSocketIrecv,
    flagcxNetSocketIflush,
    flagcxNetSocketTest,

    // One-sided functions
#ifdef FLAGCX_SOCK_HAS_DEV_TRANSFER
    flagcxNetSocketIput,
    flagcxNetSocketIget,
    flagcxNetSocketIputSignal,
#else
    NULL, // iput - not supported on socket
    NULL, // iget - not supported on socket
    NULL, // iputSignal - not supported on socket
#endif

    // Device name lookup
    NULL, // getDevFromName

    // Optional one-sided batch helpers and MR metadata
    NULL, // iputBatch
    NULL, // testBatch
    NULL, // igetBatch
#ifdef FLAGCX_SOCK_HAS_DEV_TRANSFER
    flagcxNetSocketGetMrInfo,
#else
    NULL, // getMrInfo
#endif

    // Latest-only VMM MR capabilities and internal metadata
    FLAGCX_VMM_MR_CAP_NONE,
    FLAGCX_NET_ADAPTOR_INTERNAL_NONE,
    FLAGCX_NET_GDR_FLUSH_NONE,
};
