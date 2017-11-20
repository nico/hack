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

CFDATA = [
  ('I', 'checksum'),
  ('H', 'cbData'),       # Number of compressed bytes in this block.
  ('H', 'cbUncomp'),     # Size after decompressing.
]
# If CFHEADER.flags & 4 followed by CFHEADER.cbCFData per-data reserved data.
# Then cbData many bytes payload.

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

  if typeCompress != 3:
    continue

  MIN_MATCH = 2
  MAX_MATCH = 257
  NUM_CHARS = 256
  WINDOW_SIZE = window_size
  NUM_POSITION_SLOTS = 2 * window_size
  MAIN_TREE_ELEMENTS = NUM_CHARS + NUM_POSITION_SLOTS * 8
  NUM_SECONDARY_LENGTHS = 249
  # LZX documentation:
  # https://msdn.microsoft.com/en-us/library/bb417343.aspx#lzxdatacompressionformat
  # https://msdn.microsoft.com/en-us/library/cc483133.aspx is newer and has
  # most mistakes fixed.
  # Also:
  # https://github.com/vivisect/dissect/blob/master/dissect/algos/lzx.py
  # https://github.com/kyz/libmspack/blob/master/libmspack/mspack/lzxd.c
  # The latter has an explicit list of mistakes in the official docs.
  off = folder['coffCabStart']
  data_types = ''.join(e[0] for e in CFDATA)
  data_frames = []
  for i in xrange(folder['cCFData']):
    data = collections.OrderedDict(
        zip([e[1] for e in CFDATA], struct.unpack_from(data_types, cab, off)))
    off += struct.calcsize(data_types)
    data_frames.append(cab[off:off+data['cbData']])
    off += data['cbData']
    print 'block %3d: checksum %8x, %d bytes uncompressed, %d compressed' % (
        i, data['checksum'], data['cbUncomp'], data['cbData'])

  r0, r1, r2 = 1, 1, 1
  curbit = 15
  curword = 0
  curblock = 0
  def getbit():
    global curbit, curword, curblock
    bit = (struct.unpack_from('H',data_frames[curblock],curword)[0]>>curbit) & 1
    curbit -= 1
    if curbit < 0:
      curbit = 15
      curword += 2  # in bytes
    if curword >= data_frames[curblock]:
      curword = 0
      curblock += 1
    return bit
  def getbits(n):
    bits = 0
    for i in range(n):
      bits = (bits << 1) | getbit()
    return bits
  #print struct.unpack_from('<HIII', data_frames[0])
  has_x86_jump_transform =  getbit()
  if has_x86_jump_transform == 1:
    x86_trans_size = getbits(32)  # XXX use? also, bitness right?
    #print '%08x' % x86_trans_size
  kind, size = getbits(3), getbits(24)
  print kind, size
  # makecab.exe uses an uncompressed block for 'f' followed by 149 'o', but
  # a kind 1 block for 'f' followed by 150 'o'.
  if kind == 1:  # verbatim
    assert False, 'unimplemented verbatim'
  elif kind == 2:  # aligned offset
    assert False, 'unimplemented aligned offset'
  elif kind == 3:  # uncompressed
    # 0-15 padding bits, then 12 bytes III r0, r1, r2, then size raw bytes,
    # then 0-1 padding bytes
    if curbit + 1 != 16:
      getbits(curbit + 1)
    # XXX: this gets uncompressed packets split across CFDATA blocks wrong.
    # '<' is important to set padding to 0
    r0, r1, r2 = struct.unpack_from('<III', data_frames[curblock], curword)
    curword += 12
    print struct.unpack_from('%ds' % size, data_frames[curblock], curword)[0]
    curword += size
    if curword % 2 != 0:
      curword += 1
  else:
    assert False, 'undefined block kind'
