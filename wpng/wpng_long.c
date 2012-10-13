/*

Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:

  clang -o wpng wpng.c -Wall

*/

#include <stdint.h>
#include <stdio.h>

unsigned long crc_table[256];
void make_crc_table(void) {
  for (int n = 0; n < 256; n++) {
    unsigned long c = (unsigned long) n;
    for (int k = 0; k < 8; k++)
      if (c & 1)
        c = 0xedb88320L ^ (c >> 1);
      else
        c = c >> 1;
    crc_table[n] = c;
  }
}
unsigned long update_crc(unsigned long crc, const unsigned char *buf, int len) {
  unsigned long c = crc;
  for (int n = 0; n < len; n++)
    c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
  return c;
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
  uint32_t crc;
} pngblock;

void pngblock_write(const void* d, int n, pngblock* b) {
  fwrite(d, 1, n, b->f);
  b->crc = update_crc(b->crc, d, n);
}

void pngblock_start(pngblock* b, uint32_t size, const char* tag) {
  // chunk:
  // uint32_t length (not including itself, type, crc)
  // uint32_t chunk type
  // data
  // uint32_t crc code (including type and data, but not length)

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
  // png spec
  // http://www.libpng.org/pub/png/spec/1.2/PNG-Contents.html
  // http://www.w3.org/TR/PNG/

  const char header[] = "\x89PNG\r\n\x1a\n";
  fwrite(header, 1, 8, f);

  make_crc_table();

  // header
  pngblock b = { f };
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
  // the uncompressed "size" field for a zlib stream is just 2 byte. to
  // support images bigger than 65kB, use one zlib stream block per scanline.
  // 65kB per scanline should be enough for everybody.
  uint32_t scanline_size = 0;
  scanline_size += w * 4;  // image data
  scanline_size += 1;  // one filter byte per scanline

  uint32_t idat_size = 0;
  idat_size += 2;  // zlib header
  idat_size += (1 + 2 + 2) * h;  // uncompressed data block header
  idat_size += scanline_size * h;
  idat_size += 4;  // adler32 of compressed data

  pngblock_start(&b, idat_size, "IDAT");
  // IDAT
  // zlib format:
  // http://www.ietf.org/rfc/rfc1950.txt
  // http://www.ietf.org/rfc/rfc1951.txt
  // 1 byte zlib compression method, shall be "8 deflate" for png
  // 1 byte additional flags, shall not specify preset dict
  // n bytes compressed data
  // 4 bytes adler32 check value

  // http://www.libpng.org/pub/png/spec/1.2/PNG-Compression.html:
  // """It is important to emphasize that the boundaries between IDAT chunks
  //    are arbitrary and can fall anywhere in the zlib datastream. There is
  //    not necessarily any correlation between IDAT chunk boundaries and
  //    deflate block boundaries or any other feature of the zlib data. For
  //    example, it is entirely possible for the terminating zlib check value
  //    to be split across IDAT chunks.
  //
  //    In the same vein, there is no required correlation between the
  //    structure of the image data (i.e., scanline boundaries) and deflate
  //    block boundaries or IDAT chunk boundaries. The complete image data is
  //    represented by a single zlib datastream that is stored in some number
  //    of IDAT chunks; a decoder that assumes any more than this is incorrect.
  //    (Of course, some encoder implementations may emit files in which some
  //    of these structures are indeed related. But decoders cannot rely on
  //    this.)"""
  pngblock_putc(0x08, &b);  // zlib compression method (8: deflate) and
                            // window size (0: 256 bytes, as small as possible)
  pngblock_putc(29, &b);  // flags. previous byte * 256 + this % 31 should be 0

  // zlib data
  uint32_t adler = 1;
  for (int y = 0; y < h; ++y) {
    pngblock_putc(y == h - 1 ? 1 : 0, &b); // Final block?
    pngblock_put_n_le(scanline_size, &b, 2);
    pngblock_put_n_le(~scanline_size, &b, 2);
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
