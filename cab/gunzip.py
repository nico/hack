#!/usr/bin/env python

"""a deflate implementation (in the .gz container format)"""

import struct
import sys


gz = open(sys.argv[1], 'rb').read()

assert gz[0:2] == '\x1f\x8b'
assert ord(gz[2]) == 8  # compression method deflate
flags = ord(gz[3])
mtime = struct.unpack_from('<I', gz, 4)
extra_flags = ord(gz[8])
os = ord(gz[9])
off = 10

if flags & 4:  # extra
  extra_size = struct.unpack_from('<I', gz, off)
  off += 4 + extra_size

if flags & 8:  # filename
  nul = gz.index('\0', off)
  assert nul != -1
  name = gz[off:nul]  # latin-1
  off = nul + 1

if flags & 16:  # comment
  nul = gz.index('\0', off)
  assert nul != -1
  comment = gz[off:nul]  # latin-1
  off = nul + 1

if flags & 2:  # crc16 of gzip header
  off += 2

# deflate data starts at off. last 8 bytes of file are crc32 of uncompressed
# data and the uncompressed size mod 2^32.
