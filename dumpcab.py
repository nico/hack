#!/usr/bin/env python

"""Dumps some information about files produced by Windows's makecab.exe"""

import collections
import struct
import sys


cab = open(sys.argv[1], 'rb').read()

# https://msdn.microsoft.com/en-us/library/bb417343.aspx#cabinet_format
CFHEADER = [
  ('4s', 'signature'),
  ('I', 'reserved1'),
  ('I', 'cbCabinet'),  # Size of file in bytes.
  ('I', 'reserved2'),
  ('I', 'coffFiles'),  # Offset of first CFFILE.
  ('I', 'reserved3'),
  ('b', 'versionMinor'),
  ('b', 'versionMajor'),
  ('H', 'cFolders'),  # Number of CFFOLDER entries.
  ('H', 'cFiles'),    # Number of CFFILE entries.
  ('H', 'flags'),
  ('H', 'setID'),
  ('H', 'iCabinet'),
]
# Followed by:
# - if flags & 4: reserved data sizes:
#     ('H', 'cbCFHeader'),
#     ('b', 'cbCFFolder'),
#     ('b', 'cbCFData'),
# - cbCFHeader-many bytes per-cabinet reserved data
# - if flags & 1: szCabinetPrev, szDiskPrev
# - if flags & 2: szCabinetNext, szDiskNext

header_types = ''.join(e[0] for e in CFHEADER)
header = collections.OrderedDict(
    zip([e[1] for e in CFHEADER], struct.unpack_from(header_types, cab)))
assert header['signature'] == "MSCF"
assert header['flags'] == 0, 'only no-flag .cab files supported'
off = struct.calcsize(header_types)

print header

CFFOLDER = [
  ('I', 'coffCabStart'),  # Offset of first CFDATA block in this folder.
  ('H', 'cCFData'),       # Number of CFDATA blocks in this folder.
  ('H', 'typeCompress'),
]
# If CFHEADER.flags & 4 followed by CFHEADER.cbCFFolder per-folder reserved data

for i in range(header['cFolders']):
  folder_types = ''.join(e[0] for e in CFFOLDER)
  folder = collections.OrderedDict(
      zip([e[1] for e in CFFOLDER], struct.unpack_from(folder_types, cab, off)))
  print folder
  off += struct.calcsize(folder_types)
