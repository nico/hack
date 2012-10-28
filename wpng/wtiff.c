/* Bare-bones tiff writer. Just uncompressed rgba tiffs. Build like:
     clang -o wtiff wtiff.c -Wall
*/

#include <stdint.h>
#include <stdio.h>

void wtiff(int w, int h, const uint8_t* pix, FILE* f) {  // pix: rgba in memory
  union { uint16_t i; uint8_t c[2]; } endian_check = {42};
  fwrite(endian_check.c[0] == 0 ? "MM" : "II", 1, 2, f);
  fwrite(&endian_check.i, 2, 1, f);  // magic 42
  uint32_t offset = 8; fwrite(&offset, 4, 1, f);

  // 2 bytes tag, 2 bytes type, 4 bytes count, 4 bytes value or offset
  // type 1 = byte, 3 = short, 4 = int
  // 0x100: width, 0x101: height
  // 0x102: bits per sample (8,8,8,8 for rgba :-/)
  // 0x103: compression (1: none, default)
  // 0x106: photometric interpretation (short value 2 = rgb)
  // 0x111: strip offsets, 0x116: rows per strip, 0x117: StripByteCounts
  // 0x152: extrasamples. (2 = unpremul alpha)

  // 2 bytes number of fields, fields, offset to next ifd (0)
  uint16_t n_fields = 8;
  fwrite(&n_fields, 2, 1, f);

  uint16_t tag, type, sval; uint32_t count, val;

  tag = 0x100; type = 4; count = 1; val = w;
  fwrite(&tag, 2, 1, f); fwrite(&type, 2, 1, f);
  fwrite(&count, 4, 1, f); fwrite(&val, 4, 1, f);

  tag = 0x101; type = 4; count = 1; val = h;
  fwrite(&tag, 2, 1, f); fwrite(&type, 2, 1, f);
  fwrite(&count, 4, 1, f); fwrite(&val, 4, 1, f);

  tag = 0x102; type = 1; count = 4; val = 0x08080808;  // bits per sample
  fwrite(&tag, 2, 1, f); fwrite(&type, 2, 1, f);
  fwrite(&count, 4, 1, f); fwrite(&val, 1, 4, f);

  tag = 0x106; type = 3; count = 1; sval = 2;  // rgb
  fwrite(&tag, 2, 1, f); fwrite(&type, 2, 1, f);
  fwrite(&count, 4, 1, f); fwrite(&sval, 2, 1, f); fwrite("\0\0", 1, 2, f);

  tag = 0x111; type = 4; count = h; val = 0x6e; // strip offsets from file start
  fwrite(&tag, 2, 1, f); fwrite(&type, 2, 1, f);
  fwrite(&count, 4, 1, f); fwrite(&val, 4, 1, f);

  tag = 0x116; type = 4; count = 1; val = 1;  // rows per strip
  fwrite(&tag, 2, 1, f); fwrite(&type, 2, 1, f);
  fwrite(&count, 4, 1, f); fwrite(&val, 4, 1, f);

  // "for max compat, have less than 64k strips, and less than 64kB data/strip"

  // "For each strip, the number of bytes in that strip after any compression"
  //tag = 0x117; type = 1; count = 1; val = w * 4;  // bytes per strip
  tag = 0x117; type = 3; count = h; val = 0x6e + h*4;  // bytes per strip
  fwrite(&tag, 2, 1, f); fwrite(&type, 2, 1, f);
  fwrite(&count, 4, 1, f); fwrite(&val, 4, 1, f);

  tag = 0x152; type = 3; count = 1; sval = 2;  // unpremul alpha
  fwrite(&tag, 2, 1, f); fwrite(&type, 2, 1, f);
  fwrite(&count, 4, 1, f); fwrite(&sval, 2, 1, f); fwrite("\0\0", 1, 2, f);

  fwrite("\0\0\0\0", 1, 4, f);

  // strip data
  for (int y = 0; y < h; ++y) {
    val = 0x6e + h*6 + y * w * 4; fwrite(&val, 4, 1, f);
  }
  // strip sizes
  for (int y = 0; y < h; ++y) {
    sval = w * 4; fwrite(&sval, 2, 1, f);
  }
  // image data
  fwrite(pix, 4, w*h, f);
}

int main() {
  //uint8_t pix[256*125*4];
  //for (size_t i = 0; i < sizeof(pix); ++i) pix[i] = i*i;
  //wtiff(125, 256, pix, stdout);
  uint8_t pix[] = {0xff,0,0,0xff, 0,0xff,0,0xff,  0,0,0xff,0xff, 0xff,0,0,0x80};
  wtiff(2, 2, pix, stdout);
}

