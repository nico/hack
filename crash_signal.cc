// Demo program for catching crashes and re-running the program again with
// different args.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <intrin.h>
#else
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xmmintrin.h>
#endif

char** g_argv;
extern char **environ;

static void interrupted(int signum) {
  // We're in a signal handler, the program state is unknown.
  // Can't do most things in signal headers already (cf `man sigaction`),
  // and if we're here due to e.g. a SIGSEGV all pointers might have been
  // overwritten. Let's hope g_argv and environ are still valid.
  // If they aren't, `write()` is ok to call. Since it's only passed literals,
  // hopefully the error message goes through at least.
  // Generally using argv[0] as "path to self" doesn't work, but it's good
  // enough for a demo program.
  if (execve(g_argv[0], g_argv, environ) < 0) {
    write(2, "failed to exec\n", sizeof("failed to exec\n") - 1);
    _exit(signum);
  }
}

int main(int argc, char* argv[]) {

  // Make a copy of argv for eventual execve()ing later on.
  // Add a "don't crash" arg as last arg.
  // The signal handler will use this to execve() ourselves.
  g_argv = (char**)malloc((argc + 2)*sizeof(char*));
  for (int i = 0; i < argc; ++i)
    g_argv[i] = argv[i];
  g_argv[argc] = strdup("none");
  g_argv[argc + 1] = 0;


  enum SigType {
    kSigNone,
    kSigAbort,
#if !defined(_WIN32)
    kSigBus,
#endif
    kSigFPE,
    kSigIll,
    kSigSegv,
#if !defined(_WIN32)
    kSigTerm,
#endif
  } type = kSigSegv;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "none") == 0)
      type = kSigNone;
    else if (strcmp(argv[i], "abort") == 0)
      type = kSigAbort;
#if !defined(_WIN32)
    else if (strcmp(argv[i], "bus") == 0)
      type = kSigBus;
#endif
    else if (strcmp(argv[i], "fpe") == 0)
      type = kSigFPE;
    else if (strcmp(argv[i], "ill") == 0)
      type = kSigIll;
    else if (strcmp(argv[i], "segv") == 0)
      type = kSigSegv;
#if !defined(_WIN32)
    else if (strcmp(argv[i], "term") == 0)
      type = kSigTerm;
#endif
    else if (strcmp(argv[i], "args") == 0) {
      for (int i = 0; i < argc; ++i)
        printf("argv[%d] = %s\n", i, argv[i]);
    }
    else {
      fprintf(stderr, "unknown %s\n", argv[i]);
      return 1;
    }
  }

#if !defined(_WIN32)
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = interrupted;
  if (sigaction(SIGABRT, &act, /*oact=*/0) != 0) {
    fprintf(stderr, "failed sigaction\n");
    return 1;
  }
  if (sigaction(SIGBUS, &act, /*oact=*/0) != 0) {
    fprintf(stderr, "failed sigaction\n");
    return 1;
  }
  if (sigaction(SIGFPE, &act, /*oact=*/0) != 0) {
    fprintf(stderr, "failed sigaction\n");
    return 1;
  }
  if (sigaction(SIGILL, &act, /*oact=*/0) != 0) {
    fprintf(stderr, "failed sigaction\n");
    return 1;
  }
  if (sigaction(SIGSEGV, &act, /*oact=*/0) != 0) {
    fprintf(stderr, "failed sigaction\n");
    return 1;
  }
  if (sigaction(SIGTERM, &act, /*oact=*/0) != 0) {
    fprintf(stderr, "failed sigaction\n");
    return 1;
  }
#endif

  switch (type) {
    case kSigNone: break;
    case kSigAbort: abort(); break;
#if !defined(_WIN32)
    case kSigBus: {
      // Unaligned reads aren't sigbus on x86, but write-past-mmap usually is.
      void* ptr = mmap(/*addr=*/0, /*length=*/10, PROT_READ,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      /*fd=*/-1, /*offset=*/0);
      if (ptr == MAP_FAILED) {
        fprintf(stderr, "failed mmap\n");
        return 1;
      }
      *((volatile char*)ptr + 20) = 0;
    }
#endif
    case kSigFPE: {
      volatile int a = 0x80000000;
      volatile int b = -1;
      int c = a / b;
      return c;
    }
    case kSigIll: asm("ud2");;
    case kSigSegv: *(volatile char*)0 = 0; break;
#if !defined(_WIN32)
    case kSigTerm: kill(getpid(), SIGTERM); break;
#endif
  }
}
