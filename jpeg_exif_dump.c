#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
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

static void dump_app_id(uint8_t* begin, uint8_t* end, bool has_size,
                        uint16_t size) {
  if (!has_size) {
    printf("  no size?!\n");
    return;
  }

  if (end - begin < size) {
    printf("  size is %u, but only %zu bytes left\n", size, end - begin);
    return;
  }

  char app_id[80];
  if (snprintf(app_id, sizeof(app_id), "%s", begin + 2) >= sizeof(app_id)) {
    printf("  app id longer than %zu bytes, ignoring\n", sizeof(app_id));
    return;
  }

  printf("  app id: '%s'\n", app_id);
}

static void dump(uint8_t* begin, uint8_t* end) {
  uint8_t* cur = begin;
  while (cur < end) {
    uint8_t b0 = *cur++;
    if (b0 != 0xff)
      continue;

    if (cur >= end)
      break;

    uint8_t b1 = *cur;
    if (b1 == 0xff)
      continue;
    cur++;

    if (b1 == 0)
      continue;

    printf("%02x%02x at offset %ld", b0, b1, cur - begin - 2);

    bool has_size = b1 != 0xd8 && b1 != 0xd9 && end - cur >= 2;
    uint16_t size = 0;
    if (has_size) {
      size = cur[0] << 8 | cur[1];
      printf(", size %u", size);
    }

    switch (b1) {
      case 0xc0:
        printf(": Start Of Frame, baseline DCT (SOF0)\n");
        break;
      case 0xc2:
        printf(": Start Of Frame, progressive DCT (SOF2)\n");
        break;
      case 0xc4:
        printf(": Define Huffman Tables (DHT)\n");
        break;
      case 0xd0:
      case 0xd1:
      case 0xd2:
      case 0xd3:
      case 0xd4:
      case 0xd5:
      case 0xd6:
      case 0xd7:
        printf(": Restart (RST%d)\n", b1 - 0xd0);
        break;
      case 0xd8:
        printf(": Start Of Image (SOI)\n");
        break;
      case 0xd9:
        printf(": End Of Image (EOI)\n");
        break;
      case 0xda:
        printf(": Start Of Scan (SOS)\n");
        break;
      case 0xdb:
        printf(": Define Quantization Table(s) (DQT)\n");
        break;
      case 0xdd:
        printf(": Define Restart Interval (DRI)\n");
        break;
      case 0xe0:
        printf(": JPEG/JFIF Image segment (APP0)\n");
        dump_app_id(cur, end, has_size, size);
        break;
      case 0xe1:
        printf(": EXIF Image segment (APP1)\n");
        dump_app_id(cur, end, has_size, size);
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
        printf(": Application Segment (APP%d)\n", b1 - 0xe0);
        dump_app_id(cur, end, has_size, size);
        break;
      case 0xfe:
        printf(": Comment (COM)\n");
        break;
      default:
        printf("\n");
    }
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
