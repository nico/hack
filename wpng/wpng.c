/* Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:
     clang -o wpng wpng.c -Wall
*/

#include <stdint.h>
#include <stdio.h>

typedef struct { FILE* f; uint32_t crc; uint32_t crc_table[256]; } pngblock;

void pngblock_write(const void* d, int len, pngblock* b) {
  fwrite(d, 1, len, b->f);
  for (int n = 0; n < len; n++)
    b->crc = b->crc_table[(b->crc ^ ((uint8_t*)d)[n]) & 0xff] ^ (b->crc >> 8);
}

void pngblock_putu32_be(uint32_t u, pngblock* b) {
  uint8_t d[] = { u >> 24, u >> 16, u >> 8, u }; pngblock_write(d, 4, b);
}

void wpng(int w, int h, const uint8_t* pix, FILE* f) {  // pix: rgba in memory
  // http://www.libpng.org/pub/png/spec/1.2/PNG-Contents.html
  pngblock b = { f, 0x575e51f5 };  // == update_crc(0xffffffff, "IHDR")
  for (int n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? 0xedb88320L ^ (c >> 1) : c >> 1;
    b.crc_table[n] = c;
  }

  fwrite("\x89PNG\r\n\x1a\n\0\0\0\xdIHDR", 1, 16, f);
  pngblock_putu32_be(w, &b);
  pngblock_putu32_be(h, &b);
  pngblock_write("\x8\6\0\0\0", 5, &b);  // 8bpp rgba, default flags
  pngblock_putu32_be(b.crc ^ 0xffffffff, &b);  // IHDR crc32

  uint16_t scanline_size = w*4 + 1;  // one filter byte per scanline
  pngblock_putu32_be(6 + (5 + scanline_size)*h, &b);
  b.crc = 0xffffffff;
  pngblock_write("IDAT", 4, &b);
  pngblock_write("\x8\x1d", 2, &b);  // deflate data, 255 byte sliding window
  uint32_t a1 = 1, a2 = 0;
  for (int y = 0; y < h; ++y) {
    pngblock_write(y == h - 1 ? "\1" : "\0", 1, &b);
    uint32_t s = scanline_size | (~scanline_size << 16);
    uint8_t le[] = { s, s >> 8, s >> 16, s >> 24 }; pngblock_write(le, 4, &b);
    pngblock_write(&b.crc_table, 1, &b);  // filter for scanline (0: no filter)
    pngblock_write(pix + y*4*w, 4*w, &b);
    const int BASE = 65521;  // largest prime smaller than 65536
    a2 = (a1 + a2) % BASE;
    for (int n = 0; n < 4*w; n++) {
      a1 = (a1 + pix[y*4*w + n]) % BASE;
      a2 = (a1 + a2) % BASE;
    }
  }
  pngblock_putu32_be((a2 << 16) + a1, &b);  // adler32 of uncompressed data
  pngblock_putu32_be(b.crc ^ 0xffffffff, &b);  // IDAT crc32

  fwrite("\0\0\0\0IEND\xae\x42\x60\x82", 1, 12, f);  // IEND + crc32
}

int main() {
  uint8_t pix[256*125*4];
  for (size_t i = 0; i < sizeof(pix); ++i) pix[i] = i*i;
  wpng(125, 256, pix, stdout);
}
