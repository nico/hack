/* Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:
     clang -o wpng wpng.c -Wall
*/

#include <stdint.h>
#include <stdio.h>

void fput_n_be(uint32_t u, FILE* f, int n) {
  for (int i = 0; i < n; i++) fputc((u << (8*i)) >> 24, f);
}

typedef struct { FILE* f; uint32_t* crc_table; uint32_t crc; } pngblock;

void pngblock_write(const void* d, int len, pngblock* b) {
  fwrite(d, 1, len, b->f);
  for (int n = 0; n < len; n++)
    b->crc = b->crc_table[(b->crc ^ ((uint8_t*)d)[n]) & 0xff] ^ (b->crc >> 8);
}

void pngblock_start(pngblock* b, uint32_t size, const char* tag) {
  fput_n_be(size, b->f, 4);
  b->crc = 0xffffffff;
  pngblock_write(tag, 4, b);
}

void pngblock_putc(uint8_t c, pngblock* b) {
  fputc(c, b->f);
  b->crc = b->crc_table[(b->crc ^ c) & 0xff] ^ (b->crc >> 8);
}

void pngblock_put_n_be(uint32_t u, pngblock* b, int n) {
  for (int i = 0; i < n; i++) pngblock_putc((u << (8*i)) >> 24, b);
}
void pngblock_put_n_le(uint32_t u, pngblock* b, int n) {
  for (int i = 0; i < n; i++) pngblock_putc(u >> (8*i), b);
}

// http://www.libpng.org/pub/png/spec/1.2/PNG-Contents.html
// http://www.ietf.org/rfc/rfc1950.txt
// http://www.ietf.org/rfc/rfc1951.txt

void wpng(int w, int h, const uint8_t* pix, FILE* f) {  // pix: rgba in memory
  uint32_t crc_table[256];
  for (int n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? 0xedb88320L ^ (c >> 1) : c >> 1;
    crc_table[n] = c;
  }

  pngblock b = { f, crc_table };
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  // header
  pngblock_start(&b, 13, "IHDR");  // size: IHDR has two uint32 + 5 bytes = 13
  pngblock_put_n_be(w, &b, 4);
  pngblock_put_n_be(h, &b, 4);
  pngblock_write("\x8\6\0\0\0", 5, &b);  // 8bpp rgba, default flags
  fput_n_be(b.crc ^ 0xffffffff, f, 4);  // IHDR crc32

  // image zlib data
  uint32_t data_size = w*h*4 + h;  // image data + one filter byte per scanline
  pngblock_start(&b, 11 + data_size, "IDAT");
  pngblock_write("\x8\x1d\1", 3, &b);  // deflate data, in one single block
  pngblock_put_n_le(data_size, &b, 2);
  pngblock_put_n_le(~data_size, &b, 2);
  uint32_t a1 = 1, a2 = 0;
  for (int y = 0; y < h; ++y) {
    pngblock_putc(0, &b);  // filter used for this scanline (0: no filter)
    pngblock_write(pix + y*4*w, 4*w, &b);
    const int BASE = 65521;  // largest prime smaller than 65536
    a2 = (a1 + a2) % BASE;
    for (int n = 0; n < 4*w; n++) {
      a1 = (a1 + pix[y*4*w + n]) % BASE;
      a2 = (a1 + a2) % BASE;
    }
  }
  pngblock_put_n_be((a2 << 16) + a1, &b, 4);  // adler32 of uncompressed data
  fput_n_be(b.crc ^ 0xffffffff, f, 4);  // IDAT crc32

  // footer
  pngblock_start(&b, 0, "IEND");
  fput_n_be(b.crc ^ 0xffffffff, f, 4);  // IEND crc32
}

int main() {
  unsigned pix[] = { 0xff0000ff, 0xff00ff00, 0xffff0000, 0x8000ff00 };
  wpng(2, 2, (uint8_t*)pix, stdout);  // XXX endianess
}
