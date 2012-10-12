/*

Test program to write uncompressed gzip file.

  clang -o gz gz.c -Wall
  ./gz | gunzip

*/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

// http://www.ietf.org/rfc/rfc1952.txt
uint32_t crc_table[256];
void make_crc_table(void) {
  for (int n = 0; n < 256; n++) {
    uint32_t c = (uint32_t) n;
    for (int k = 0; k < 8; k++)
      if (c & 1)
        c = 0xedb88320u ^ (c >> 1);
      else
        c = c >> 1;
    crc_table[n] = c;
  }
}
uint32_t update_crc(uint32_t crc, const unsigned char *buf, int len) {
  uint32_t c = crc ^ 0xffffffffu;
  for (int n = 0; n < len; n++)
    c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}
uint32_t crc(const unsigned char *buf, int len) {
  return update_crc(0, buf, len);
}

int main() {
  // XXX happens to work on little-endian x86s; make portable.
  uint32_t d;
  uint16_t h;

  make_crc_table();

  const unsigned char data[] = "Hello gzip\n";

  // header
  fputc(31, stdout);   // magic number 1
  fputc(139, stdout);  // magic number 2
  fputc(8, stdout);    // compression method: deflate
  fputc(0, stdout);    // flags
  d = 0; fwrite(&d, 4, 1, stdout);  // mtime
  fputc(0, stdout);    // extra flags
  fputc(0xff, stdout); // OS

  // data
  fputc(1, stdout); // Final block, compression method: uncompressed
  h = sizeof(data); fwrite(&h, 2, 1, stdout);
  h = ~h; fwrite(&h, 2, 1, stdout);
  fwrite(data, 1, sizeof(data), stdout);

  // footer
  d = crc(data, sizeof(data)); fwrite(&d, 4, 1, stdout);  // crc32
  d = sizeof(data); fwrite(&d, 4, 1, stdout);  // ISIZE
}
