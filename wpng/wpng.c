/* Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:
     clang -o wpng wpng.c -Wall
*/

#include <stdint.h>
#include <stdio.h>

void wpng(int w, int h, const uint8_t* pix, FILE* f) {  // pix: rgba in memory
  uint32_t crc_table[256];
  for (int n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? 0xedb88320L ^ (c >> 1) : c >> 1;
    crc_table[n] = c;
  }
#define CRCWRITE(d, len) fwrite(d, 1, len, f); for (int n = 0; n < len; n++) \
    crc = crc_table[(crc ^ (d)[n]) & 0xff] ^ (crc >> 8)
  uint8_t B[4];
#define U32BE(u) B[0] = (u) >> 24; B[1] = (u) >> 16; B[2] = (u) >> 8; B[3] = (u)
  fwrite("\x89PNG\r\n\x1a\n\0\0\0\xdIHDR", 1, 16, f);
  uint32_t crc = 0x575e51f5;  // == update_crc(0xffffffff, "IHDR")
  U32BE(w); CRCWRITE(B, 4);
  U32BE(h); CRCWRITE(B, 4);
  CRCWRITE("\x8\6\0\0\0", 5);
  U32BE(crc ^ 0xffffffff); fwrite(B, 1, 4, f); // IHDR crc32

  uint16_t scanline_size = w*4 + 1;
  U32BE(6 + (5 + scanline_size)*h); fwrite(B, 1, 4, f);
  crc = 0xffffffff;
  CRCWRITE("IDAT\x8\x1d", 6);
  uint32_t a1 = 1, a2 = 0;
  for (int y = 0; y < h; ++y, pix += 4*w) {
    uint32_t s = scanline_size | (~scanline_size << 16);
    uint8_t le[] = { y == h - 1 ? 1 : 0, s, s >> 8, s >> 16, s >> 24, 0 };
    CRCWRITE(le, 6);
    CRCWRITE(pix, 4*w);
    const int P = 65521;
    a2 = (a1 + a2) % P;
    for (int n = 0; n < 4*w; n++) { a1 = (a1+pix[n]) % P; a2 = (a1+a2) % P; }
  }
  U32BE((a2 << 16) + a1); CRCWRITE(B, 4); // adler32 of uncompressed data
  U32BE(crc ^ 0xffffffff); fwrite(B, 1, 4, f); // IDAT crc32
#undef CRCWRITE
#undef U32BE
  fwrite("\0\0\0\0IEND\xae\x42\x60\x82", 1, 12, f);  // IEND + crc32
}

int main() {
  uint8_t pix[256*125*4];
  for (size_t i = 0; i < sizeof(pix); ++i) pix[i] = i*i;
  wpng(125, 256, pix, stdout);
}
