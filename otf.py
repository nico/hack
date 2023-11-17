#!/usr/bin/env python3

"""Usage:
./otf.py path/to/font.ttf
"""


import argparse
import collections
import struct


class Struct:
  def __init__(self, name, bitness, *args):
    self.fmt = bitness+ ''.join(args[0::2])
    self.type = collections.namedtuple(name, args[1::2])

  def unpack(self, data):
    return self.type(*struct.unpack(self.fmt, data))

  def pack(self, obj):
    return struct.pack(self.fmt, *obj)

  def size(self):
    return struct.calcsize(self.fmt)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', help='input file')
    args = parser.parse_args()

    font = args.input
    with open(font, 'rb') as f:
      font_data = f.read()

    Header = Struct('Header', '>',
                        'I', 'sfnt_version',
                        'H', 'num_tables',
                        'H', 'search_range',
                        'H', 'entry_selector',
                        'H', 'range_shift',
                        )
    header = Header.unpack(font_data[0:Header.size()])
    print(header)

    TableRecord = Struct('TableRecord', '>',
                        '4s', 'table_tag',
                        'I', 'checksum',
                        'I', 'offset', # From beginning of file.
                        'I', 'length',
                        )
    for i in range(header.num_tables):
        start_offset = Header.size() + i * TableRecord.size()
        record_data = font_data[start_offset:start_offset + TableRecord.size()]
        record = TableRecord.unpack(record_data)
        print(record)


if __name__ == '__main__':
    main()
