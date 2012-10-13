/*

Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:

  clang -o wpng wpng.c -Wall

*/

#include <stdint.h>
#include <stdio.h>

// png spec
// http://www.libpng.org/pub/png/spec/1.2/PNG-Contents.html
// http://www.w3.org/TR/PNG/

unsigned long crc_table[256];
int crc_table_computed = 0;
void make_crc_table(void) {
  for (int n = 0; n < 256; n++) {
    unsigned long c = (unsigned long) n;
    for (int k = 0; k < 8; k++) {
      if (c & 1)
        c = 0xedb88320L ^ (c >> 1);
      else
        c = c >> 1;
    }
    crc_table[n] = c;
  }
  crc_table_computed = 1;
}
unsigned long update_crc(unsigned long crc, const unsigned char *buf,
                         int len) {
  unsigned long c = crc;
  if (!crc_table_computed)
    make_crc_table();
  for (int n = 0; n < len; n++) {
    c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
  }
  return c;
}

// Header: 137 80 78 71 13 10 26 10
// hex: \x89 P N G \r \n \x1a \n

// chunk:
// uint32_t length (not including itself, type, crc)
// uint32_t chunk type
// data
// uint32_t crc code (including type and data, but not length)

// IHDR
typedef struct {
  uint32_t width, height;
  uint8_t bits;

  // 0: grayscale (allowed bits: 1, 2, 4, 8, 16)
  // 2: truecolor rgb (allowed bits: 8, 16)
  // 3: indexed (allowed bits: 1, 2, 4, 8)
  // 4: grayalpha (allowed bits: 8, 16)
  // 6: truecolor rgba (allowed bits: 8, 16)
  uint8_t color_type;

  // 0: inflate/deflate with window size <= 32768 bytes
  uint8_t compression_method;

  // 0: Every scanline is prefixed with a filter subtype for that scanline
  // Subtypes:
  //    0       None
  //    1       Sub  (difference to left)
  //    2       Up   (difference to up)
  //    3       Average  (difference to (up + left)/2)
  //    4       Paeth    (difference to paeth(left, up, upleft))
  uint8_t filter_method;

  // 0: no interlace
  // 1: Adam7 interlace
  uint8_t interlace_method;
} IHDR;

// IDAT
// zlib format:
// http://www.ietf.org/rfc/rfc1950.txt
// http://www.ietf.org/rfc/rfc1951.txt

// 1 byte zlib compression method, shall be "8 deflate" for png
// 1 byte additional flags, shall not specify preset dict
// n bytes compressed data
// 4 bytes adler32 check value

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


// IEND is empty

void fput_n_be(uint32_t u, FILE* f, int n) {
  for (int i = 0; i < n; i++) fputc((u << (8*i)) >> 24, f);
}

typedef struct {
  FILE* f;
  uint32_t crc;
} pngblock;

void pngblock_write(const void* d, int n, pngblock* b) {
  fwrite(d, 1, n, b->f);
  b->crc = update_crc(b->crc, d, n);
}

void pngblock_start(pngblock* b, uint32_t size, const char* tag) {
  fput_n_be(size, b->f, 4);
  b->crc = 0xffffffff;
  pngblock_write(tag, 4, b);
}

void pngblock_putc(int c, pngblock* b) {
  fputc(c, b->f);
  unsigned char c8 = c;
  b->crc = update_crc(b->crc, &c8, 1);
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

// XXX or char*?
void wpng(int w, int h, unsigned* pix, FILE* f) {
  const char header[] = "\x89PNG\r\n\x1a\n";
  fwrite(header, 1, 8, f);

  // header
  pngblock b = { f };
  pngblock_start(&b, 13, "IHDR");
  pngblock_put_n_be(w, &b, 4);
  pngblock_put_n_be(h, &b, 4);
  pngblock_putc(8, &b);  // bits per channel
  pngblock_putc(6, &b);  // color type: truecolor rgba
  pngblock_putc(0, &b);  // compression method: deflate
  pngblock_putc(0, &b);  // filter method: XXX
  pngblock_putc(0, &b);  // interlace method: no interlace
  pngblock_end(&b);  // IHDR crc32  XXX

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
  pngblock_putc(0x08, &b);  // zlib compression method (8: deflate) and
                            // window size (0: 256 bytes, as small as possible)
  pngblock_putc(29, &b);  // flags. previous byte * 256 + this % 31 should be 0

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
  pngblock_end(&b);

  // footer
  pngblock_start(&b, 0, "IEND");
  pngblock_end(&b);
}

int main() {
  unsigned pix[] = { 0xff0000ff, 0xff00ff00, 0xffff0000, 0x8000ff00 };
  wpng(2, 2, pix, stdout);
}
