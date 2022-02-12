#!/usr/bin/env python3

"""Usage:
./fontsay.py message path/to/serenity.font
"""

import collections
import struct
import sys


class Struct:
  def __init__(self, name, *args):
    self.fmt = '<' + ''.join(args[0::2])
    self.type = collections.namedtuple(name, args[1::2])

  def unpack_from(self, buffer, offset=0):
    return self.type(*struct.unpack_from(self.fmt, buffer, offset))

  def size(self):
    return struct.calcsize(self.fmt)


def main():
  font = sys.argv[2]
  with open(font, 'rb') as f:
    font_data = f.read()

  # Serenity bitmap font format:
  # 1. File header
  # 2. Bitmap that stores which 256-blocks of glyphs are present in the font
  #    ("range mask")
  # 3. Glyph bitmap data, one uint32_t per row
  # 4. The width of each glyph
  # Ref: https://github.com/SerenityOS/serenity/blob/96895cd22c0/Userland/Libraries/LibGfx/BitmapFont.cpp#L220

  # 1. File header
  Header = Struct('Header',
                  '4s', 'magic', # '!Fnt'
                  'B', 'glyph_width',
                  'B', 'glyph_height',
                  'H', 'range_mask_size',
                  'B', 'is_variable_width',
                  'B', 'glyph_spacing',
                  'B', 'baseline',
                  'B', 'mean_line',
                  'B', 'presentation_size',
                  'H', 'weight',
                  'B', 'slope',
                  '32s', 'name',
                  '32s', 'family',
                  )
  header = Header.unpack_from(font_data)

  # 2. Bitmap that stores which 256-blocks of glyphs are present in the font

  # First bit is set if the font contains the first 256 glyphs, the second bit
  # if it contains the next 256 glphys, etc.
  # So one bit signals presence of 8 * 256 = 2048 = 0x800 glyphs.
  # There are at most 0x10ffff + 1 glyphs, so there are at most 544 bytes.
  range_mask = font_data[Header.size():Header.size() + header.range_mask_size]

  range_index = []
  current_page = 0
  for mask in range_mask:
    for i in range(8):
      if mask & (1 << i):
        range_index.append(current_page)
        current_page += 1
      else:
        range_index.append(None)
  glyph_count = current_page * 256

  # 3. Glyph bitmap data

  # One glyph is at most 32 bits wide. One glyph row is always an uint32_t,
  # where the first header.glyph_width bits are the pixels in that row.
  glyph_rows = struct.unpack_from((glyph_count * header.glyph_height) * 'I',
                                  font_data,
                                  Header.size() + header.range_mask_size)

  # 4. The width of each glyph
  glyph_widths = struct.unpack_from(glyph_count * 'B',
                                    font_data,
                                    Header.size() + header.range_mask_size +
                                      len(glyph_rows) * struct.calcsize('I'))

  # Do something with the data!
  for y in range(header.glyph_height):
    for c in sys.argv[1]:
      codepoint = ord(c)

      has_page = range_index[codepoint // 256] is not None
      if has_page:
        glyph_index = range_index[codepoint // 256] * 256 + codepoint % 256
      if not has_page or glyph_widths[glyph_index] == 0:
        codepoint = glyph_index = ord('?') # Assume every font has latin.

      row_index = glyph_index * header.glyph_height + y
      for x in range(glyph_widths[glyph_index]):
        if glyph_rows[row_index] & (1 << x):
          print('█', end='')
        else:
          print(' ', end='')
      print(' ' * header.glyph_spacing, end='')
    print()


if __name__ == '__main__':
  main()
