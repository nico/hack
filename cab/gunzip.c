/*
clang -O2 cab/gunzip.c
*/
#define _CRT_SECURE_NO_WARNINGS
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);  // FIXME: add to clang's lib/clang/Basic/Builtins.def?
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
}

uint32_t little_uint32t(uint8_t* d) {
  return d[3] << 24 | d[2] << 16 | d[1] << 8 | d[0];
}

int main(int argc, char* argv[]) {
  if (argc <= 1)
    fatal("need filename\n");
  FILE* in = fopen(argv[1], "rb");
  if (!in)
    fatal("failed to open %s\n", argv[1]);

  fseek(in, 0, SEEK_END);
  long size = ftell(in);
  rewind(in);

  uint8_t* gz = (uint8_t*)malloc(size);
  fread(gz, size, 1, in);

  if (size < 10) fatal("file too small\n");

  if (memcmp("\x1f\x8b", gz, 2)) fatal("invalid file header\n");
  if (gz[2] != 8) fatal("unexpected compression method %d\n", gz[2]);
  uint8_t flags = gz[3];
  time_t mtime = little_uint32t(gz + 4);
  uint8_t extra_flags = gz[8];
  uint8_t os = gz[9];

  printf("flags %d\n", flags);
  printf("mtime %s", ctime(&mtime));  // ctime() result contains trailing \n
  printf("extra_flags %d\n", extra_flags);
  printf("os %d\n", os);

  free(gz);
  fclose(in);
}
