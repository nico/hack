#!/usr/bin/env python3

"""Usage:
./otf.py path/to/font.ttf
"""


import argparse
import collections
import struct


class Struct:
  def __init__(self, name, endianness, *args):
    self.fmt = endianness + ''.join(args[0::2])
    self.type = collections.namedtuple(name, args[1::2])

  def unpack(self, data):
    return self.type(*struct.unpack(self.fmt, data))

  def pack(self, obj):
    return struct.pack(self.fmt, *obj)

  def size(self):
    return struct.calcsize(self.fmt)


def dump_post(data):
    Header = Struct('Header', '>',
                        'I', 'version',
                        'I', 'italicAngle',
                        'h', 'underlinePosition',
                        'h', 'underlineThickness',
                        'I', 'isFixedPitch',
                        'I', 'minMemType42',
                        'I', 'maxMemType42',
                        'I', 'minMemType1',
                        'I', 'maxMemType1',
                        )
    header = Header.unpack(data[0:Header.size()])
    print(f'    {header}')

    if header.version == 0x00020000:
        numGlyphs, = struct.unpack_from('>H', data, Header.size())

        string_table = []
        string_table_data = data[Header.size() + 2 * (1 + numGlyphs):]
        while string_table_data:
            length = string_table_data[0]
            string_table.append(string_table_data[1:1 + length])
            string_table_data = string_table_data[1 + length:]

        print(f'    {numGlyphs}')
        glyphNameIndex = struct.unpack_from('>' + 'H' * numGlyphs, data,
                                            Header.size() + 2)
        print(f'    {glyphNameIndex}')
        print(f'    {len(string_table)} strings:')
        print(f'    {string_table}')
    elif header.version == 0x00025000:
        print(f'    TODO: v2.5 data')
    elif header.version != 0x00010000 and header.version != 0x00030000:
        print(f'    Unexpected version!')


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
        data = font_data[record.offset:record.offset + record.length]
        if record.table_tag == b'post':
            dump_post(data)


if __name__ == '__main__':
    main()
