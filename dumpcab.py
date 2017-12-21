#!/usr/bin/env python

"""Dumps some information about files produced by Windows's makecab.exe"""

import collections
import os
import struct
import sys

# Interesting inputs, compress with "makecab /D CompressionType=LZX in.txt":
# foo.txt containing "foo\r\n", simple type 3 (uncompressed) block.
# foooooo.txt containing "f", 150 "o"s, \r\n" easy type 1 (verbatim) block.
# mac.txt from http://www.folgerdigitaltexts.org/download/txt/Mac.txt.
#   4 blocks.
#
# Interesting parameters:
# * /D CompressionMemory=15 sets LZX compression window size.

# things to try and measure:
# * how much space do all the tweaks (double-huffman, aligned blocks,
#   e8 transform, length tree, repeated offsets, etc) really save?
# * speedup from good lookup-based dehuff
# * speedup from copying less data

cab = open(sys.argv[1], 'rb').read()
outfile = sys.argv[2] if len(sys.argv) > 2 \
                      else os.path.basename(sys.argv[1]) + '.out'


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


class Bitstream(object):
  def __init__(self, source):
    self.curbit = 15
    self.curword, self.curword_val = 0, struct.unpack_from('H', source, 0)[0]
    self.source = source

  def getbit(self):
    bit = (self.curword_val >> self.curbit) & 1
    self.curbit -= 1
    if self.curbit < 0:
      self.curbit = 15
      self.curword += 2  # in bytes
      if self.curword < len(self.source):
        self.curword_val = struct.unpack_from('H', self.source, self.curword)[0]
    return bit

  def getbits(self, n):
    # Naive chunking doesn't seem to help:
    #global curbit, curword, curword_val
    #if n <= curbit + 1:
      #bits = (curword_val >> (curbit - n + 1)) & ((1 << n) - 1)
      #curbit -= n
      #if curbit < 0:
        #curbit = 15
        #curword += 2  # in bytes
        #if curword < len(data_frames[curblock]):
          #curword_val= struct.unpack_from('H',data_frames[curblock],curword)[0]
      #return bits
    # Doing this bit-by-bit is inefficient; this should try to bunch things up.
    bits = 0
    for i in range(n):
      bits = (bits << 1) | self.getbit()
    return bits


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
  NUM_POSITION_SLOTS = {15:30,16:32,17:34,18:36,19:38,20:42,21:50}[window_size]
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
  data_frame_uncomp_sizes = []
  for i in xrange(folder['cCFData']):
    data = collections.OrderedDict(
        zip([e[1] for e in CFDATA], struct.unpack_from(data_types, cab, off)))
    off += struct.calcsize(data_types)
    data_frames.append(cab[off:off+data['cbData']])
    data_frame_uncomp_sizes.append(data['cbUncomp'])
    off += data['cbData']
    #print 'block %3d: checksum %8x, %d bytes uncompressed, %d compressed' % (
        #i, data['checksum'], data['cbUncomp'], data['cbData'])

  r0, r1, r2 = 1, 1, 1
  curblock = 0
  bitstream = Bitstream(data_frames[curblock])
  has_x86_jump_transform =  bitstream.getbit()
  if has_x86_jump_transform == 1:
    # makecab.exe seems to always include this if the input size is at least
    # 6 bytes, even for text files.
    x86_trans_size = bitstream.getbits(32)

  outfile = open(outfile, 'wb')
  maintree = [0] * MAIN_TREE_ELEMENTS
  lengthstree = [0] * NUM_SECONDARY_LENGTHS
  win_write, win_count, win_size = 0, 0, 1 << window_size
  # Using bytearray(win_size) here reduces mem use from 10.6MB to 8.4MB
  # but increases runtime from 13.6s to 16.4s for some reason.
  window = [0] * win_size
  while curblock < len(data_frames):
    kind, size = bitstream.getbits(3), bitstream.getbits(24)
    print kind, size
    # makecab.exe uses an uncompressed block for 'f' followed by 149 'o', but
    # a kind 1 block for 'f' followed by 150 'o'.
    if kind in [1, 2]:  # 1: verbatim, 2: aligned
      # A pretree is a huffman tree for the 20 tree codes, which are then used
      # to encode the "main" huffmann tree. There are 3 trees, each preceded by
      # its pretree.
      # The canonical huffman trees match rfc1951.
      def canon_tree(lengths):
        # Given the lengths of the nodes in a canonical huffman tree,
        # returns a (len, code) -> value map for each node.
        maxlen = max(lengths)
        bl_count = [0] * (maxlen + 1)
        for e in lengths:
          bl_count[e] += 1
        code = 0
        bl_count[0] = 0
        next_code = [0] * (maxlen + 1)
        for i in xrange(1, maxlen + 1):
          code = (code + bl_count[i - 1]) << 1
          next_code[i] = code
        codes = {}
        for i, len_i in enumerate(lengths):
          if len_i != 0:
            # Using a dict for this is very inefficient.
            codes[(len_i, next_code[len_i])] = i
            next_code[len_i] += 1
        return codes
      if kind == 2:
        alignedoffsettree = [bitstream.getbits(3) for i in range(8)]
        alignedoffsetcodes = canon_tree(alignedoffsettree)
      # Read pretree of 256 elt main tree.
      pretree = [bitstream.getbits(4) for i in range(20)]
      codes = canon_tree(pretree)
      # Read main tree for the 256 elts.
      def readtree(codes, tree, maxi, starti=0):
        curlen, curbits = 0, 0
        i = starti
        while i < maxi:
          curbits = (curbits << 1) | bitstream.getbit()
          curlen += 1
          code = codes.get((curlen, curbits))
          if code is None: continue
          curlen, curbits = 0, 0
          # code 0-16: Len[x] = (prev_len[x] - code + 17) mod 17
          # 17: for next (4 + getbits(4)) elements, Len[X] = 0
          # 18: for next (20 + getbits(5)) elements, Len[X] = 0
          # 19: for next (4 + getbits(1)) elements, Len[X] += readcode()
          if code <= 16:
            tree[i] = (tree[i] + 17 - code) % 17
            i += 1
          elif code == 17:
            n = 4 + bitstream.getbits(4)
            tree[i:i+n] = [0] * n
            i += n
          elif code == 18:
            n = 20 + bitstream.getbits(5)
            tree[i:i+n] = [0] * n
            i += n
          else:
            assert code == 19, code
            n = 4 + bitstream.getbit()
            code = None
            while code is None:
              curbits = (curbits << 1) | bitstream.getbit()
              curlen += 1
              code = codes.get((curlen, curbits))
            curlen, curbits = 0, 0
            code = (tree[i] + 17 - code) % 17
            tree[i:i+n] = [code] * n
            i += n
      readtree(codes, maintree, NUM_CHARS)
      assert len(maintree) == MAIN_TREE_ELEMENTS
      # Read pretree of slot elts of main tree.
      pretree = [bitstream.getbits(4) for i in range(20)]
      codes = canon_tree(pretree)
      # Read slots of main tree.
      readtree(codes, maintree, MAIN_TREE_ELEMENTS, starti=NUM_CHARS)
      maincodes = canon_tree(maintree)
      # Read pretree of lengths tree.
      pretree = [bitstream.getbits(4) for i in range(20)]
      codes = canon_tree(pretree)
      # Read lengths tree.
      readtree(codes, lengthstree, NUM_SECONDARY_LENGTHS)
      lengthcodes = canon_tree(lengthstree)

      # Huffman trees have been read, now read the actual data.
      def output(s):
        global win_write, win_count, win_size, outfile
        # Max match length is 257, min win size is 32768, match will always fit.
        assert len(s) < win_size
        right_space = win_size - win_write
        if right_space >= len(s):
          window[win_write:win_write+len(s)] = s
          assert len(window) == win_size
          win_write += len(s)
        else:
          # We know len(s) < win_write, so the reminder will fit on the left.
          window[win_write:win_size] = s[0:right_space]
          assert len(window) == win_size
          window[0:len(s) - right_space] = s[right_space:]
          assert len(window) == win_size
          win_write = len(s) - right_space
        win_count = min(win_size, win_count + len(s))

      num_decompressed = 0
      curlen, curbits = 0, 0
      while num_decompressed < size:
        curframesize = data_frame_uncomp_sizes[curblock]
        curbits = (curbits << 1) | bitstream.getbit()
        curlen += 1
        code = maincodes.get((curlen, curbits))
        if code is None: continue
        curlen, curbits = 0, 0
        if code < 256:
          output([code])
          num_decompressed += 1
        else:
          length_header = (code - 256) & 7
          if length_header == 7:
            lencode = None
            while lencode is None:
              curbits = (curbits << 1) | bitstream.getbit()
              curlen += 1
              lencode = lengthcodes.get((curlen, curbits))
            curlen, curbits = 0, 0
            match_length = lencode + 7 + 2
          else:
            match_length = length_header + 2
          position_slot = (code - 256) >> 3
          # Check for repeated offsets in positions 0, 1, 2
          if position_slot == 0:
            match_offset = r0
          elif position_slot == 1:
            match_offset = r1
            r0, r1 = r1, r0
          elif position_slot == 2:
            match_offset = r2
            r0, r2 = r2, r0
          else:
            # XXX why is 17 the max?
            extra_bits = min((position_slot - 2) / 2, 17)

            # XXX explain. (farther offsets need more bits; slots).
            # also, precompute
            base_position = 0
            for i in xrange(position_slot - 2):
              base_position += (1 << min(i / 2, 17))

            if kind == 2:
              if extra_bits >= 3:
                verbatim_bits = bitstream.getbits(extra_bits - 3) << 3
                aligned_bits = None
                while aligned_bits is None:
                  curbits = (curbits << 1) | bitstream.getbit()
                  curlen += 1
                  aligned_bits = alignedoffsetcodes.get((curlen, curbits))
                curlen, curbits = 0, 0
              else:
                verbatim_bits = bitstream.getbits(extra_bits)
                aligned_bits = 0
              match_offset = base_position + verbatim_bits + aligned_bits
            else:
              verbatim_bits = bitstream.getbits(extra_bits)
              match_offset = base_position + verbatim_bits
            r0, r1, r2 = match_offset, r0, r1

          # match_offset is relative to the end of the window.
          match_offset = win_write - match_offset
          if match_offset < 0:
            match_offset += win_size
          for i in xrange(match_length):
            output([window[match_offset]])  # FIXME: chunkier?
            match_offset += 1
            if match_offset >= win_size:
              match_offset -= win_size
          num_decompressed += match_length
        # Consider that the 2nd lzx block (with new huffman trees at the start)
        # occurs in the middle of a CFDATA block.  To make sure the CFDATA block
        # ends on a byte boundary, there has to be padding at the end of the
        # CFDATA block, not at the end of the lzx block. Hence, we have to check
        # for the number of total bytes written in all lzx blocks so far, not
        # number of uncompressed bytes from the current lzx block (which is
        # num_decompressed).  We don't have a variable containing number of
        # total bytes written, but since each CFDATA (except for the last)
        # contains 32768 bytes uncompressed, since the window size is always a
        # multiple of that, and since matches must not cross 32768 boundaries,
        # checking the window write pointer should achieve the same thing.
        if (win_write % 32768) % curframesize == 0:
          # Move bit input pointer to next block.
          # Also aligns to 16-bit boundary after every cfdata block.
          curblock += 1
          if curblock < len(data_frames):
            bitstream = Bitstream(data_frames[curblock])

          # Do x86 jump transform if necessary.
          outdata = window[win_write-curframesize:win_write]
          if curblock <= 32768 and curframesize > 10 and has_x86_jump_transform:
            i = 0
            while i < len(outdata) - 10:
              if outdata[i] != 0xe8:
                i += 1
                continue
              current_pointer = (curblock - 1) * 32768 + i
              d = struct.unpack('<i', ''.join(map(chr, outdata[i+1:i+5])))[0]
              if d >= -current_pointer and d < x86_trans_size:
                d = d - current_pointer if d >= 0 else d + x86_trans_size
                outdata[i + 1] = d & 0xff
                outdata[i + 2] = (d >> 8) & 0xff
                outdata[i + 3] = (d >> 16) & 0xff
                outdata[i + 4] = (d >> 24) & 0xff
              i += 5
          outfile.write(''.join(map(chr, outdata)))
    elif kind == 3:  # uncompressed
      assert False, 'this needs to be reimplemented'
      # 0-15 padding bits, then 12 bytes III r0, r1, r2, then size raw bytes,
      # then 0-1 padding bytes
      if bitstream.curbit + 1 != 16:
        bitstream.getbits(curbit + 1)
      # XXX: this gets uncompressed packets split across CFDATA blocks wrong.
      # '<' is important to set padding to 0
      r0, r1, r2 = struct.unpack_from('<III', data_frames[curblock], curword)
      curword += 12
      sys.stdout.write(
          struct.unpack_from('%ds' % size, data_frames[curblock], curword)[0])
      curword += size
      if curword % 2 != 0:
        curword += 1
    else:
      assert False, 'undefined block kind'
