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

int main(int argc, char* argv[]) {
  enum SigType {
    kSigNone,
    kSigAbort,
#if !defined(_WIN32)
    kSigBus,
#endif
    kSigFPE,
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
    else if (strcmp(argv[i], "segv") == 0)
      type = kSigSegv;
#if !defined(_WIN32)
    else if (strcmp(argv[i], "term") == 0)
      type = kSigTerm;
#endif
    else {
      fprintf(stderr, "unknown %s\n", argv[i]);
      return 1;
    }
  }


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
    case kSigSegv: *(volatile char*)0 = 0; break;
#if !defined(_WIN32)
    case kSigTerm: kill(getpid(), SIGTERM); break;
#endif
  }
}
