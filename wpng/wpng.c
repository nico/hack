/* Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:
     clang -o wpng wpng.c -Wall
*/

#include <stdint.h>
#include <stdio.h>

void wpng(int w, int h, const uint8_t* pix, FILE* f) {  // pix: rgba in memory
  uint32_t crc_table[256], crc = ~0;
  for (uint32_t n = 0, c = 0; n < 256; n++, c = n) {
    for (int k = 0; k < 8; k++) c = -(c & 1) & 0xedb88320L ^ (c >> 1);
    crc_table[n] = c;
  }
#define CRCWRITE(d, len) fwrite(d, 1, len, f); for (int n = 0; n < len; n++) \
    crc = crc_table[(crc ^ (d)[n]) & 0xff] ^ (crc >> 8)
#define U32BE(b, u) b[0] = (u)>>24; b[1] = (u)>>16; b[2] = (u)>>8; b[3] = (u)
  uint8_t I[] = "\x89PNG\r\n\x1a\n\0\0\0\xdIHDRwid0hyt0\x8\6\0\0\0", B[4];
  fwrite(I, 1, 12, f);
  U32BE((I + 16), w); U32BE((I + 20), h); CRCWRITE(I+12, 17);
  U32BE(B, ~crc); fwrite(B, 1, 4, f);  // IHDR crc32
  uint16_t scanl = w*4 + 1;
  U32BE(B, 6 + (5 + scanl)*h); fwrite(B, 1, 4, f);
  crc = ~0; CRCWRITE("IDAT\x8\x1d", 6);
  uint32_t a1 = 1, a2 = 0;
  for (int y = 0; y < h; ++y, pix += w*4) {
    uint8_t le[] = { y == h - 1, scanl, scanl >> 8, ~scanl, ~scanl >> 8, 0 };
    CRCWRITE(le, 6);
    CRCWRITE(pix, w*4);
    const int P = 65521;
    a2 = (a1 + a2) % P;
    for (int n = 0; n < w*4; n++) { a1 = (a1+pix[n]) % P; a2 = (a1+a2) % P; }
  }
  U32BE(B, (a2 << 16) + a1); CRCWRITE(B, 4);  // adler32 of uncompressed data
  U32BE(B, ~crc); fwrite(B, 1, 4, f);  // IDAT crc32
#undef CRCWRITE
#undef U32BE
  fwrite("\0\0\0\0IEND\xae\x42\x60\x82", 1, 12, f);  // IEND + crc32
}

int main() {
  uint8_t pix[256*125*4];
  for (size_t i = 0; i < sizeof(pix); ++i) pix[i] = i*i;
  wpng(125, 256, pix, stdout);
}
