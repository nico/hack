/* Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:
     clang -o wpng wpng.c -Wall
*/

#include <stdint.h>
#include <stdio.h>

uint32_t update_adler32(uint32_t adler, const uint8_t* buf, int len) {
  const int BASE = 65521; /* largest prime smaller than 65536 */
  uint32_t s1 = adler & 0xffff;
  uint32_t s2 = (adler >> 16) & 0xffff;
  for (int n = 0; n < len; n++) {
    s1 = (s1 + buf[n]) % BASE;
    s2 = (s2 + s1)     % BASE;
  }
  return (s2 << 16) + s1;
}

void fput_n_be(uint32_t u, FILE* f, int n) {
  for (int i = 0; i < n; i++) fputc((u << (8*i)) >> 24, f);
}

typedef struct {
  FILE* f;
  uint32_t* crc_table;
  uint32_t crc;
} pngblock;

void pngblock_update_crc(const uint8_t* buf, int len, pngblock* b) {
  for (int n = 0; n < len; n++)
    b->crc = b->crc_table[(b->crc ^ buf[n]) & 0xff] ^ (b->crc >> 8);
}

void pngblock_write(const void* d, int n, pngblock* b) {
  fwrite(d, 1, n, b->f);
  pngblock_update_crc(d, n, b);
}

void pngblock_start(pngblock* b, uint32_t size, const char* tag) {
  fput_n_be(size, b->f, 4);
  b->crc = 0xffffffff;
  pngblock_write(tag, 4, b);
}

void pngblock_putc(uint8_t c, pngblock* b) {
  fputc(c, b->f);
  pngblock_update_crc(&c, 1, b);
}

void pngblock_put_n_be(uint32_t u, pngblock* b, int n) {
  for (int i = 0; i < n; i++) pngblock_putc((u << (8*i)) >> 24, b);
}
void pngblock_put_n_le(uint32_t u, pngblock* b, int n) {
  for (int i = 0; i < n; i++) pngblock_putc(u >> (8*i), b);
}

void pngblock_end(pngblock* b) {
  fput_n_be(b->crc ^ 0xffffffff, b->f, 4);
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

  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  // header
  pngblock b = { f, crc_table };
  pngblock_start(&b, 13, "IHDR");  // size: IHDR has two uint32 + 5 bytes = 13
  pngblock_put_n_be(w, &b, 4);
  pngblock_put_n_be(h, &b, 4);
  pngblock_putc(8, &b);  // bits per channel
  pngblock_putc(6, &b);  // color type: truecolor rgba
  pngblock_putc(0, &b);  // compression method: deflate
  pngblock_putc(0, &b);  // filter method: 1 byte subfilter before each scanline
  pngblock_putc(0, &b);  // interlace method: no interlace
  pngblock_end(&b);  // IHDR crc32

  // image zlib data
  uint32_t data_size = w*h*4 + h;  // image data + one filter byte per scanline
  pngblock_start(&b, 11 + data_size, "IDAT");
  pngblock_putc(8, &b);  // zlib compression method (8: deflate), small window
  pngblock_putc(29, &b); // flags. previous byte * 256 + this % 31 should be 0
  pngblock_putc(1, &b); // final block, compression method: uncompressed
  pngblock_put_n_le(data_size, &b, 2);
  pngblock_put_n_le(~data_size, &b, 2);
  uint32_t adler = 1;
  for (int y = 0; y < h; ++y) {
    const uint8_t zero = 0;
    pngblock_putc(zero, &b);  // filter used for this scanline (0: no filter)
    adler = update_adler32(adler, &zero, 1);
    pngblock_write(pix + y*4*w, 4*w, &b);
    adler = update_adler32(adler, pix + y*4*w, 4*w);
  }

  pngblock_put_n_be(adler, &b, 4);  // adler32 of uncompressed data
  pngblock_end(&b);  // IDAT crc32

  // footer
  pngblock_start(&b, 0, "IEND");
  pngblock_end(&b);  // IEND crc32
}

int main() {
  unsigned pix[] = { 0xff0000ff, 0xff00ff00, 0xffff0000, 0x8000ff00 };
  wpng(2, 2, (uint8_t*)pix, stdout);  // XXX endianess
}
