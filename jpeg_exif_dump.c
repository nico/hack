#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
}

static void dump(uint8_t* begin, uint8_t* end) {
  uint8_t* cur = begin;
  while (cur < end) {
    uint8_t b0 = *cur++;
    if (b0 != 0xff)
      continue;

    if (cur >= end)
      break;

    uint8_t b1 = *cur++;
    if (b1 == 0)
      continue;

    printf("%02x%02x at offset %ld", b0, b1, cur - begin - 2);
    switch (b1) {
      case 0xc0:
        printf(": Start Of Frame, baseline DCT (SOF0)");
        break;
      case 0xc2:
        printf(": Start Of Frame, progressive DCT (SOF2)");
        break;
      case 0xc4:
        printf(": Define Huffman Tables (DHT)");
        break;
      case 0xd0:
      case 0xd1:
      case 0xd2:
      case 0xd3:
      case 0xd4:
      case 0xd5:
      case 0xd6:
      case 0xd7:
        printf(": Restart (RST%d)", b1 - 0xd0);
        break;
      case 0xd8:
        printf(": Start Of Image (SOI)");
        break;
      case 0xd9:
        printf(": End Of Image (EOI)");
        break;
      case 0xda:
        printf(": Start Of Scan (SOS)");
        break;
      case 0xdb:
        printf(": Define Quanitization Table(s) (DQT)");
        break;
      case 0xdd:
        printf(": Define Restart Interval (DRI)");
        break;
      case 0xe0:
        printf(": JPEG/JFIF Image segment (APP0)");
        break;
      case 0xe1:
        printf(": EXIF Image segment (APP1)");
        break;
      case 0xe2:
      case 0xe3:
      case 0xe4:
      case 0xe5:
      case 0xe6:
      case 0xe7:
      case 0xe8:
      case 0xe9:
      case 0xea:
      case 0xeb:
      case 0xec:
      case 0xed:
      case 0xee:
      case 0xef:
        printf(": Application Segment (APP%d)", b1 - 0xe0);
        break;
      case 0xfe:
        printf(": Comment (COM)");
        break;
    }
    printf("\n");
  }
}

int main(int argc, char* argv[]) {
  const char* in_name = argv[1];

  // Read input.
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  void* contents = mmap(
      /*addr=*/0, in_stat.st_size, PROT_READ, MAP_SHARED, in_file,
      /*offset=*/0);
  if (contents == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  dump(contents, contents + in_stat.st_size);

  munmap(contents, in_stat.st_size);
  close(in_file);
}
