// clang dump_backtrace.c && ./a.out  # prints a trace

#include <execinfo.h>
static void DumpBacktrace(int skip_frames) {
  void* stack[256];
  int size = backtrace(stack, 256);
  ++skip_frames;  // Skip ourselves as well.
  backtrace_symbols_fd(stack + skip_frames, size - skip_frames, 2);
}

void demo(int n) { return n == 0 ? DumpBacktrace(0) : demo(n - 1); }
int main() { demo(4); }
