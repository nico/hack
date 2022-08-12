#include "draw_line.h"
#include "framebuffer.h"

#include <chrono>
#include <random>

// pix: bgra in memory, bottom-most scanline first
static void wtga(uint16_t w, uint16_t h, const uint8_t* pix, FILE* f) {
  uint8_t wl = static_cast<uint8_t>(w), wh = static_cast<uint8_t>(w >> 8);
  uint8_t hl = static_cast<uint8_t>(h), hh = static_cast<uint8_t>(h >> 8);
  uint8_t head[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, wl, wh, hl, hh, 32, 0};
  fwrite(head, 1, sizeof(head), f);
  fwrite(pix, 1, w * h * 4, f);
}

static void write_tga(const char* name, Framebuffer& fb) {
  FILE* f = fopen(name, "wb");
  if (!f) return;

  // FIXME: un-premultiply alpha.
  wtga(fb.width, fb.height, reinterpret_cast<uint8_t*>(fb.pixels.get()), f);

  fclose(f);
}

static void draw_line_bench(int seed) {
  Framebuffer fb{1200, 800};
  Surface s = fb.surface();

  std::mt19937 r;
  r.seed(seed);
  std::uniform_int_distribution<int> x(0, fb.width - 1);
  std::uniform_int_distribution<int> y(0, fb.height - 1);
  std::uniform_int_distribution<Pixel> col(0, UINT_MAX);

  const int N = 1'000'000;

  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < N; ++i)
    draw_line(s, x(r), y(r), x(r), y(r), col(r));
  auto end = std::chrono::steady_clock::now();

  std::chrono::duration<double, std::milli> ms = end - start;
  printf("%d draw_line calls took %.2f ms\n", N, ms.count());

  //write_tga("bench.tga", fb);
}

int main(int argc, char* argv[]) {
  draw_line_bench(argc);

  Framebuffer fb{1200, 800};

  Surface s = fb.surface();
  for (int i = 0; i < 400; i += 3)
    draw_line(s, 100, i, 100 + 399, i * 2, rgb(255, i / 2, 0));

  write_tga("out.tga", fb);
}
