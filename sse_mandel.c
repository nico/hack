// http://www.iquilezles.org/www/articles/sse/sse.htm
// clang -c sse_mandel.c

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
    const __m128i bb = _mm_cvtps_epi32(ite);
    const __m128i gg = _mm_slli_si128(bb, 1);
    const __m128i rr = _mm_slli_si128(bb, 2);
    const __m128i color = _mm_or_si128(_mm_or_si128(rr,gg),bb);

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
