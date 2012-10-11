/*

Bare-bones png writer. Just uncompressed rgba png, no frills. Build like:

  clang -o wpng wpng.c -Wall

*/

#include <stdint.h>

// png spec
// http://www.libpng.org/pub/png/spec/1.2/PNG-Contents.html
// http://www.w3.org/TR/PNG/

/* Table of CRCs of all 8-bit messages. */
unsigned long crc_table[256];

/* Flag: has the table been computed? Initially false. */
int crc_table_computed = 0;

/* Make the table for a fast CRC. */
void make_crc_table(void) {
  unsigned long c;
  int n, k;

  for (n = 0; n < 256; n++) {
    c = (unsigned long) n;
    for (k = 0; k < 8; k++) {
      if (c & 1)
        c = 0xedb88320L ^ (c >> 1);
      else
        c = c >> 1;
    }
    crc_table[n] = c;
  }
  crc_table_computed = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
   should be initialized to all 1's, and the transmitted value
   is the 1's complement of the final running CRC (see the
   crc() routine below)). */

unsigned long update_crc(unsigned long crc, unsigned char *buf,
                         int len) {
  unsigned long c = crc;
  int n;

  if (!crc_table_computed)
    make_crc_table();
  for (n = 0; n < len; n++) {
    c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
  }
  return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
unsigned long crc(unsigned char *buf, int len) {
  return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}


// Header: 137 80 78 71 13 10 26 10
// hex: \x89 P N G \r \n \x1a \n

// chunk:
// uint32_t length (not including itself, type, crc)
// uint32_t chunk type
// data
// uint32_t crc code

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

  // 
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
#define BASE 65521 /* largest prime smaller than 65536 */

/*
   Update a running Adler-32 checksum with the bytes buf[0..len-1]
 and return the updated checksum. The Adler-32 checksum should be
 initialized to 1.

 Usage example:

   unsigned long adler = 1L;

   while (read_buffer(buffer, length) != EOF) {
     adler = update_adler32(adler, buffer, length);
   }
   if (adler != original_adler) error();
*/
unsigned long update_adler32(unsigned long adler,
   unsigned char *buf, int len) {
  unsigned long s1 = adler & 0xffff;
  unsigned long s2 = (adler >> 16) & 0xffff;
  int n;

  for (n = 0; n < len; n++) {
    s1 = (s1 + buf[n]) % BASE;
    s2 = (s2 + s1)     % BASE;
  }
  return (s2 << 16) + s1;
}

/* Return the adler32 of the bytes buf[0..len-1] */
unsigned long adler32(unsigned char *buf, int len) {
  return update_adler32(1L, buf, len);
}



// IEND is empty
