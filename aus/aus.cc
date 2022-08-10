#include "draw_line.h"
#include "framebuffer.h"

// pix: bgra in memory, bottom-most scanline first
void wtga(uint16_t w, uint16_t h, const uint8_t* pix, FILE* f) {
  uint8_t wl = static_cast<uint8_t>(w), wh = static_cast<uint8_t>(w >> 8);
  uint8_t hl = static_cast<uint8_t>(h), hh = static_cast<uint8_t>(h >> 8);
  uint8_t head[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, wl, wh, hl, hh, 32, 0};
  fwrite(head, 1, sizeof(head), f);
  fwrite(pix, 1, w * h * 4, f);
}

void write_tga(const char* name, Framebuffer& fb) {
  FILE* f = fopen(name, "wb");
  if (!f) return;

  // FIXME: un-premultiply alpha.
  wtga(fb.width, fb.height, reinterpret_cast<uint8_t*>(fb.pixels.get()), f);

  fclose(f);
}

int main() {
  Framebuffer fb{1200, 800};

  Surface s = fb.surface();
  for (int i = 0; i < 400; i += 3)
    draw_line(s, 100, i, 100 + 399, i * 2, rgb(255, i / 2, 0));

  write_tga("out.tga", fb);
}
