// http://www.iquilezles.org/www/articles/sse/sse.htm
// clang -O2 sse_mandel.c && ./a.out  # writes "mandel.png"

#include <stdint.h>

uint32_t IterateMandelbrot(float a, float b) {
  int i;
  float x, y, x2, y2;

  x = x2 = 0.0f;
  y = y2 = 0.0f;

  // iterate f(Z) = Z^2 + C,  Z0 = 0
  for (i = 0; i < 512; i++) {
    y = 2.0f*x*y+b;
    x = x2-y2+a;

    x2 = x*x;
    y2 = y*y;

    const float m2 = x2+y2;
    if (m2 > 4.0f)
      break;
  }

  // create color
  return 0xff000000|(i<<16)|(i<<8)|i;
}

void drawMandelbrot(uint32_t* buffer, int xres, int yres) {
  const float ixres = 1.0f/(float)xres;
  const float iyres = 1.0f/(float)yres;

  for (int j=0; j < yres; j++)
  for (int i=0; i < xres; i++) {
      const float a = -2.25f + 3.00f*(float)i*ixres;
      const float b =  1.12f - 2.24f*(float)j*iyres;

      *buffer++ = IterateMandelbrot(a, b);
  }
}

#include <xmmintrin.h>

__m128i IterateMandelbrot_sse(__m128 a, __m128 b) {
    __m128  x, y, x2, y2, m2;
    __m128  co, ite;

    unsigned int i;

    //const simd4f one = _mm_set1_ps(1.0f);
    //const simd4f th  = _mm_set1_ps(4.0f);
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 th  = _mm_set1_ps(4.0f);

    x   = _mm_setzero_ps();
    y   = _mm_setzero_ps();
    x2  = _mm_setzero_ps();
    y2  = _mm_setzero_ps();
    co  = _mm_setzero_ps();
    ite = _mm_setzero_ps();

    // iterate f(Z) = Z^2 + C,  Z0 = 0
    for (i = 0; i < 512; i++) {
      y  = _mm_mul_ps(x, y);
      y  = _mm_add_ps(_mm_add_ps(y,y),   b);
      x  = _mm_add_ps(_mm_sub_ps(x2,y2), a);

      x2 = _mm_mul_ps(x, x);
      y2 = _mm_mul_ps(y, y);

      m2 = _mm_add_ps(x2,y2);
      co = _mm_or_ps(co, _mm_cmpgt_ps(m2, th));


      ite = _mm_add_ps(ite, _mm_andnot_ps(co, one));
      if (_mm_movemask_ps(co) == 0x0f) 
          //i=nites;
          break;
    }

    // create color
    const __m128i aa = _mm_set1_epi32(0xff000000);
    const __m128i bb = _mm_cvtps_epi32(ite);
    const __m128i gg = _mm_slli_si128(bb, 1);
    const __m128i rr = _mm_slli_si128(bb, 2);
    const __m128i color = _mm_or_si128(_mm_or_si128(_mm_or_si128(rr,gg),bb),aa);

    return color;
}

void drawMandelbrot_sse(uint32_t* buffer, int xres, int yres) {
  __m128i *buffer4 = (__m128i*)buffer;
  const __m128 ixres = _mm_set1_ps(1.0f/(float)xres);
  const __m128 iyres = _mm_set1_ps(1.0f/(float)yres);

  for (int j=0; j < yres; j++)
  for (int i=0; i < xres; i+=4) {
    __m128  a, b;
    a = _mm_set_ps(i+3, i+2, i+1, i+0);
    a = _mm_mul_ps(a, ixres);
    a = _mm_mul_ps(a, _mm_set1_ps( 3.00f));
    a = _mm_add_ps(a, _mm_set1_ps(-2.25f));

    b = _mm_set1_ps((float)j);
    b = _mm_mul_ps(b, iyres);
    b = _mm_mul_ps(b, _mm_set1_ps(-2.24f));
    b = _mm_add_ps(b, _mm_set1_ps( 1.12f));

    _mm_store_si128(buffer4++, IterateMandelbrot_sse(a, b));
  }
}

#include <stdio.h>

void wpng(int w, int h, const uint8_t* pix, FILE* f) {  // pix: rgba in memory
  uint32_t crc_table[256];
  for (uint32_t n = 0, c = 0; n < 256; n++, c = n) {
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320L ^ (c >> 1) : c >> 1;
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
  for (int y = 0; y < h; ++y, pix += w*4) {
    uint32_t s = scanline_size | (~scanline_size << 16);
    uint8_t le[] = { y == h - 1 ? 1 : 0, s, s >> 8, s >> 16, s >> 24, 0 };
    CRCWRITE(le, 6);
    CRCWRITE(pix, w*4);
    const int P = 65521;
    a2 = (a1 + a2) % P;
    for (int n = 0; n < w*4; n++) { a1 = (a1+pix[n]) % P; a2 = (a1+a2) % P; }
  }
  U32BE((a2 << 16) + a1); CRCWRITE(B, 4); // adler32 of uncompressed data
  U32BE(crc ^ 0xffffffff); fwrite(B, 1, 4, f); // IDAT crc32
#undef CRCWRITE
#undef U32BE
  fwrite("\0\0\0\0IEND\xae\x42\x60\x82", 1, 12, f);  // IEND + crc32
}

int main(int argc, char* argv[]) {
  const int w = 2000, h = 1400;
  uint32_t* pix = malloc(w * h * 4);
  if (argc > 1)
    drawMandelbrot(pix, w, h);  // 2000 x 1400 takes about 3.9s / 1.3s at O2
  else
    drawMandelbrot_sse(pix, w, h);  // 2000 x 1400 takes about 1.3s / 0.4 at O2

  // Takes about 0.1s / 0.08s at O2
  FILE* f = fopen("mandel.png", "wb");
  wpng(w, h, (uint8_t*)pix, f);  // assumes little-endian, but so does SSE
  fclose(f);

  free(pix);
}
