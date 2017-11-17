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
  ('B', 'versionMinor'),
  ('B', 'versionMajor'),
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

folders = []
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
  folders.append(folder)
  off += struct.calcsize(folder_types)


assert off == header['coffFiles']
files = []
CFFILE = [
  ('I', 'cbFile'),  # Uncompressed size of this file in bytes.
  ('I', 'uoffFolderStart'),  # Uncompressed offset of this file in folder.
  ('H', 'iFolder'),  # Index into CFFOLDER area; 0xFFFD, 0xFFFE, 0xFFFF special
  ('H', 'date'),     # In the format ((year-1980) << 9)+(month << 5)+(day),
                     # where month={1..12} and day={1..31}.
  ('H', 'time'),     # In the format (hour << 11)+(minute << 5)+(seconds/2),
                     # where hour={0..23}.
  ('H', 'attribs'),  # 1: read-only
                     # 2: hidden
                     # 4: system file
                     # 0x20: file modified since last backup
                     # 0x40: run after extraction
                     # 0x80: name contains UTF
]
# Followed by szFile, the file's name. Can contain directory separators, and
# if 0x80 is set, is UTF-encoded. (FIXME: details)

for i in range(header['cFiles']):
  file_types = ''.join(e[0] for e in CFFILE)
  file_entry = collections.OrderedDict(
      zip([e[1] for e in CFFILE], struct.unpack_from(file_types, cab, off)))
  off += struct.calcsize(file_types)
  # Read zero-terminated file name folowing CFFILE record.
  assert file_entry['attribs'] & 0x80 == 0, 'unicode filenames not supported'
  assert file_entry['iFolder'] not in [0xfffd, 0xfffe, 0xffff], \
         'unsupported iFolder special case'
  nul = cab.index('\0', off)
  assert nul != -1
  name = cab[off:nul]
  off = nul + 1
  files.append((name, file_entry))

# Print list of included files.
for name, file_entry in files:
  folder = folders[file_entry['iFolder']]
  year = (file_entry['date'] >> 9) + 1980
  month = (file_entry['date'] >> 5) & 0xf
  day = file_entry['date'] & 0x1f
  hour = (file_entry['time'] >> 11)
  minute = (file_entry['time'] >> 5) & 0x3f
  second = (file_entry['time'] & 0x1f) * 2
  typeCompress = folder['typeCompress'] & 0xf
  compression = { 0: 'uncompressed', 1: 'MS-zipped', 2: 'quantum-compressed',
                  3: 'LZX-compressed' }[typeCompress]
  if typeCompress == 3:
    window_size = (folder['typeCompress'] & 0x1f00) >> 8
    compression += ' with window size %d' % window_size

  print '%-20s %d-%02d-%02d %02d:%02d:%02d, %d bytes, %s, in %d blocks' % (
      name, year, month, day, hour, minute, second, file_entry['cbFile'],
      compression, folder['cCFData'])
