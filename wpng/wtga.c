/* tga writer. Build like:
  clang -o wtga wtga.c -Wall
*/

#include <stdint.h>
#include <stdio.h>

// pix: bgra in memory, bottom-most scanline first
void wtga(uint16_t w, uint16_t h, const uint8_t* pix, FILE* f) {
  uint8_t head[] = { 0,0,2,0,0,0,0,0,0,0,0,0,w,w>>8,h,h>>8,32,0 };
  fwrite(head, 1, sizeof(head), f);
  fwrite(pix, 1, w*h*4, f);
}

int main() {
  uint8_t pix[] = {0xff,0,0,0xff, 0,0xff,0,0xff,  0,0,0xff,0xff, 0xff,0,0,0x80};
  wtga(2, 2, pix, stdout);
}
