/*
 * stackdump.c - dump user-space call stacks of a running process without gdb.
 *
 * Build:
 *   gcc -shared -fPIC -O2 -g -o /tmp/flagcx_stackdump.so stackdump.c -ldl
 *
 * Use (the library only installs signal handlers, it changes nothing else):
 *   LD_PRELOAD=/tmp/flagcx_stackdump.so ./your_program ...
 *
 * Under mpirun, forward it to the ranks:
 *   mpirun -np 2 -x LD_PRELOAD=/tmp/flagcx_stackdump.so ... ./your_program ...
 *
 * Trigger a dump of every thread, from another shell, while the job is hung:
 *   for p in $(pgrep -f your_program); do
 *     for t in /proc/$p/task/*; do kill -USR1 "$(basename "$t")"; done
 *   done
 *   # SIGUSR1 does not get through? use SIGRTMIN+3 (34) instead.
 *
 * Output: /tmp/stackdump.<pid>.txt, one section per thread, appended.
 *
 * Symbolize the reported offsets:
 *   addr2line -e <module> -f -C -i 0x<offset>
 *
 * Notes:
 *   - Needs no ptrace, so it also works inside containers where gdb/eu-stack
 *     cannot attach.
 *   - The first frames of each section belong to this signal handler; the
 *     interesting frames start right after "__restore_rt".
 *   - backtrace() inside a signal handler is safe in practice (glibc
 *     backtrace_symbols_fd does not malloc); if a dump ever comes out empty,
 *     just send the signal again.
 */
#define _GNU_SOURCE
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define DUMP_DEPTH 96

static void dumpHandler(int sig, siginfo_t *info, void *ucontext) {
  (void)info;
  (void)ucontext;
  char path[128];
  char hdr[192];
  void *frames[DUMP_DEPTH];
  int frameCount;
  int fd;
  int len;

  snprintf(path, sizeof(path), "/tmp/stackdump.%d.txt", (int)getpid());
  fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) {
    return;
  }

  len = snprintf(hdr, sizeof(hdr), "\n===== signal=%d pid=%d tid=%ld =====\n",
                 sig, (int)getpid(), (long)syscall(SYS_gettid));
  if (len > 0) {
    ssize_t written = write(fd, hdr, (size_t)len);
    (void)written;
  }

  frameCount = backtrace(frames, DUMP_DEPTH);
  backtrace_symbols_fd(frames, frameCount, fd);
  close(fd);
}

__attribute__((constructor)) static void installDumpHandler(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = dumpHandler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  sigaction(SIGRTMIN + 3, &sa, NULL);
}
