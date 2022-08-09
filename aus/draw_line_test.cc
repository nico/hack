#include "draw_line.h"
#include "framebuffer.h"

#include <string>

struct TestSurface {
  TestSurface(size_t w, size_t h, const char* name) : fb{w, h}, name(name) {}

  TestSurface& draw_line(int x1, int y1, int x2, int y2);
  void should_be(std::string raw_expected);

private:
  static constexpr Pixel kFill = rgb(255, 255, 255);
  std::string fb_to_string() const;
  std::string filter_spaces(std::string s);

  Framebuffer fb;
  const char* name;
};

int main() {
  TestSurface(9, 5, "octant 1").
    draw_line(4, 2, 4 + 4, 2 + 2).
    should_be(R"(.........
                 ......##.
                 ....##...
                 .........
                 .........)");

}

TestSurface& TestSurface::draw_line(int x1, int y1, int x2, int y2) {
  ::draw_line(fb.surface(), x1, y1, x2, y2, kFill);
  return *this;
}

void TestSurface::should_be(std::string raw_expected) {
  std::string actual = fb_to_string();
  std::string expected = filter_spaces(std::move(raw_expected));

  if (actual == expected)
    return;

  fprintf(stderr, "Test '%s' failed.\n", name);
  fprintf(stderr, "Expected:\n%s\n", expected.c_str());
  fprintf(stderr, "Actual:\n%s\n", actual.c_str());
}

std::string TestSurface::fb_to_string() const {
  std::string s;
  for (int y = fb.height - 1; y >= 0; --y) {
    for (int x = 0; x < fb.width; ++x)
      s += fb.scanline(y)[x] == kFill ? '#' : '.';
    if (y != 0)
      s += '\n';
  }
  return s;
}

std::string TestSurface::filter_spaces(std::string s) {
  std::string filtered;
  for (char c : s)
    if (c != ' ')
      filtered += c;
  return filtered;
}
