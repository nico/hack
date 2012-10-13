/*

Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:

  clang -o wpng wpng.c -Wall

*/

#include <stdint.h>
#include <stdio.h>

unsigned long update_crc(unsigned long* crc_table, unsigned long crc,
                         const unsigned char *buf, int len) {
  for (int n = 0; n < len; n++)
    crc = crc_table[(crc ^ buf[n]) & 0xff] ^ (crc >> 8);
  return crc;
}

unsigned long update_adler32(unsigned long adler,
                             const unsigned char *buf, int len) {
  const int BASE = 65521; /* largest prime smaller than 65536 */
  unsigned long s1 = adler & 0xffff;
  unsigned long s2 = (adler >> 16) & 0xffff;
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
  unsigned long* crc_table;
  uint32_t crc;
} pngblock;

void pngblock_write(const void* d, int n, pngblock* b) {
  fwrite(d, 1, n, b->f);
  b->crc = update_crc(b->crc_table, b->crc, d, n);
}

void pngblock_start(pngblock* b, uint32_t size, const char* tag) {
  fput_n_be(size, b->f, 4);
  b->crc = 0xffffffff;
  pngblock_write(tag, 4, b);
}

void pngblock_putc(int c, pngblock* b) {
  fputc(c, b->f);
  unsigned char c8 = c;
  b->crc = update_crc(b->crc_table, b->crc, &c8, 1);
}

void pngblock_put_n_be(uint32_t u, pngblock* b, int n) {
  for (int i = 0; i < n; i++) pngblock_putc((u << (8*i)) >> 24, b);
}
void pngblock_put_n_le(uint32_t u, pngblock* b, int n) {
  for (int i = 0; i < n; i++) pngblock_putc(u >> (8*i), b);
}

void pngblock_end(pngblock* b) {
  uint32_t crc32 = b->crc ^ 0xffffffff;
  fput_n_be(crc32, b->f, 4);
}

// http://www.libpng.org/pub/png/spec/1.2/PNG-Contents.html
// http://www.ietf.org/rfc/rfc1950.txt
// http://www.ietf.org/rfc/rfc1951.txt

// XXX or char*?
void wpng(int w, int h, unsigned* pix, FILE* f) {
  const char header[] = "\x89PNG\r\n\x1a\n";
  fwrite(header, 1, 8, f);

  unsigned long crc_table[256];
  for (int n = 0; n < 256; n++) {
    unsigned long c = (unsigned long) n;
    for (int k = 0; k < 8; k++)
      if (c & 1)
        c = 0xedb88320L ^ (c >> 1);
      else
        c = c >> 1;
    crc_table[n] = c;
  }

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

  // image data
  uint32_t data_size = 0;
  data_size += w * h * 4;  // image data
  data_size += h;  // one filter byte per scanline

  uint32_t idat_size = 0;
  idat_size += 2;  // zlib header
  idat_size += 1 + 2 + 2;  // uncompressed data block header
  idat_size += data_size;
  idat_size += 4;  // adler32 of compressed data

  pngblock_start(&b, idat_size, "IDAT");
  pngblock_putc(8, &b);  // zlib compression method (8: deflate), small window
  pngblock_putc(29, &b); // flags. previous byte * 256 + this % 31 should be 0

  // zlib data
  pngblock_putc(1, &b); // Final block, compression method: uncompressed
  pngblock_put_n_le(data_size, &b, 2);
  pngblock_put_n_le(~data_size, &b, 2);
  uint32_t adler = 1;
  for (int y = 0; y < h; ++y) {
    const uint8_t zero = 0;
    pngblock_putc(zero, &b);  // Filter used for this scanline
    adler = update_adler32(adler, &zero, 1);
    pngblock_write(pix + y*w, 4*w, &b);  // XXX endianess
    adler = update_adler32(adler, (uint8_t*)(pix + y*w), 4*w);
  }

  pngblock_put_n_be(adler, &b, 4);  // adler32 of uncompressed data
  pngblock_end(&b);  // IDAT crc32

  // footer
  pngblock_start(&b, 0, "IEND");
  pngblock_end(&b);  // IEND crc32
}

int main() {
  unsigned pix[] = { 0xff0000ff, 0xff00ff00, 0xffff0000, 0x8000ff00 };
  wpng(2, 2, pix, stdout);
}
