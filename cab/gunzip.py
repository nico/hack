#!/usr/bin/env python3

"""a deflate implementation (in the .gz container format)

% echo 'hello hahahahahaha' | gzip | cab/gunzip.py && cat gunzip.out
"""

from datetime import datetime
import struct
import sys


if len(sys.argv) > 1:
  source = open(sys.argv[1], 'rb')
else:
  if sys.platform == 'win32':
    import os, msvcrt
    msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
  source = sys.stdin.buffer
gz = source.read()

# rfc1952 describes the gzip header and footer.
assert gz[0:2] == b'\x1f\x8b'
assert gz[2] == 8  # compression method deflate
flags = gz[3]
mtime = struct.unpack_from('<I', gz, 4)[0]
extra_flags = gz[8]
os = gz[9]
off = 10

print('flags', flags)
print('mtime', datetime.fromtimestamp(mtime).strftime('%Y-%m-%d %H:%M:%S'))
print('extra_flags', extra_flags)
print('os', os)

if flags & 4:  # extra
  extra_size = struct.unpack_from('<I', gz, off)
  off += 4 + extra_size
  print('extra size', extra_size)

if flags & 8:  # filename
  nul = gz.index('\0', off)
  assert nul != -1
  name = gz[off:nul]  # latin-1
  off = nul + 1
  print('name', name)

if flags & 16:  # comment
  nul = gz.index('\0', off)
  assert nul != -1
  comment = gz[off:nul]  # latin-1
  off = nul + 1
  print('comment', comment)

if flags & 2:  # crc16 of gzip header
  print('header crc16', struct.unpack_from('<H', gz, off))
  off += 2

# deflate data starts at off. last 8 bytes of file are crc32 of uncompressed
# data and the uncompressed size mod 2^32.

# rfc1951 describes the deflate bitstream.

class Bitstream(object):
  def __init__(self, source):
    self.curbit = 0
    self.curword, self.curword_val = 0, struct.unpack_from('<H', source, 0)[0]
    self.source = source

  def getbit(self):
    # deflate orders bits right-to-left.
    bit = (self.curword_val >> self.curbit) & 1
    self.curbit += 1
    if self.curbit > 15:
      self.curbit = 0
      self.curword += 2  # in bytes
      if self.curword < len(self.source):
       self.curword_val = struct.unpack_from('<H', self.source, self.curword)[0]
    return bit

  def getbits(self, n):  # n is at most 13, for extra_dist_bits
    # Doing this bit-by-bit is inefficient; this should try to bunch things up.
    bits = 0
    for i in range(n):
      # deflate orders bits right-to-left.
      bits = bits | (self.getbit() << i)
    return bits

  def parse_uncompressed_block(self):
    start = self.curword
    if self.curbit > 0: start += 1
    if self.curbit > 8: start += 1
    size, nsize = struct.unpack_from('<HH', self.source, start)
    data = struct.unpack_from('%ds' % size, self.source, start + 4)[0]
    self.curbit = 0
    self.curword = start + 4 + size
    if self.curword < len(self.source):
     self.curword_val = struct.unpack_from('<H', self.source, self.curword)[0]
    return data


class HuffTree(object):
  def __init__(self, nodelengths):
    self.last_code, self.codes = HuffTree._canon_tree(nodelengths)

  def readsym(self, bitstream):
    curbits, curlen = bitstream.getbit(), 1
    last_code = self.last_code
    while curbits > last_code[curlen]:
      # Note that this uses reversed bit order compared to getbits()
      curbits = (curbits << 1) | bitstream.getbit()
      curlen += 1
    # Note: 2nd index is negative to index from end of list
    return self.codes[curlen][curbits - last_code[curlen] - 1]

  # The canonical huffman trees match rfc1951.
  @staticmethod
  def _canon_tree(lengths):
    print(lengths)
    # Given the lengths of the nodes in a canonical huffman tree,
    # returns a (last_code, codes) tuple, where last_code[n] returns the
    # last prefix of length n, and codes[n] contains a list of all values of
    # prefixes with length n.
    maxlen = max(lengths)
    bl_count = [0] * (maxlen + 1)
    codes = [[] for _ in range(maxlen + 1)]
    for i, len_i in enumerate(lengths):
      if len_i != 0:
        bl_count[len_i] += 1
        codes[len_i].append(i)
    code = 0
    last_code = [0] * (maxlen + 1)
    for i in range(1, maxlen + 1):
      code = (code + bl_count[i - 1]) << 1
      last_code[i] = code + bl_count[i] - 1
    return last_code, codes


class Window(object):
  def __init__(self, window_size):
    self.win_write = 0
    self.win_size = 1 << window_size
    self.window = [0] * self.win_size

  def output_literal(self, c):
    if self.win_write >= self.win_size:
      self.win_write = 0
    self.window[self.win_write] = c
    self.win_write += 1

  def output_block(self, data):
    if len(data) > self.win_size:
      data = data[len(data) - self.win_size:]
    rightspace = self.win_size - self.win_write
    if rightspace >= len(data):
      self.window[self.win_write:self.win_write+len(data)] = data
      self.win_write += len(data)
    else:
      leftspace = len(data) - rightspace
      self.window[self.win_write:] = data[0:rightspace]
      self.window[:leftspace] = data[rightspace:]
      self.win_write = leftspace

  def copy_match(self, match_offset, match_length):
    # match_offset is relative to the end of the window.
    no_overlap = self.win_write >= match_offset >= match_length and \
                 self.win_write + match_length < self.win_size
    match_offset = self.win_write - match_offset
    if no_overlap:
      # Can use faster slice copy.
      self.window[self.win_write:self.win_write+match_length] = \
          self.window[match_offset:match_offset+match_length]
      self.win_write += match_length
      return
    if match_offset < 0:
      match_offset += self.win_size
    # Max match length is 258, win size is 32768, match will always fit.
    for i in range(match_length):
      self.output_literal(self.window[match_offset])  # FIXME: chunkier?
      match_offset += 1
      if match_offset >= self.win_size:
        match_offset -= self.win_size

  def write_from(self, outfile, win_start):
    if self.win_write >= win_start:
      outfile.write(bytes(self.window[win_start:self.win_write]))
    else:
      outfile.write(bytes(self.window[win_start:] +
                          self.window[:self.win_write]))


def deflate_decode_pretree(pretree, bitstream, num_lengths):
  i = 0
  lengths = [0] * num_lengths
  while i < num_lengths:
    code = pretree.readsym(bitstream)
    # code 0-15: Len[x] = code
    # 16: for next (3 + getbits(2)) elements, Len[x] = previous code
    # 17: for next (3 + getbits(3)) elements, Len[x] = 0
    # 18: for next (11 + getbits(7)) elements, Len[x] = 0
    if code <= 15:
      lengths[i] = code
      i += 1
    elif code == 16:
      n = 3 + bitstream.getbits(2)
      lengths[i:i+n] = [lengths[i - 1]] * n
      i += n
    elif code == 17:
      n = 3 + bitstream.getbits(3)
      lengths[i:i+n] = [0] * n
      i += n
    else:
      assert code == 18, code
      n = 11 + bitstream.getbits(7)
      lengths[i:i+n] = [0] * n
      i += n
  return lengths

# XXX explain.
extra_len_bits = [0]*4 + [i//4 for i in range(6*4)] + [0]
base_length = 3
base_lengths = []
for extra in extra_len_bits:
  base_lengths.append(base_length)
  base_length += (1 << extra)
# Code 285 is special: It's a 0-extra-bits encoding of the longest-possible
# value and it means "258", which could also (less efficiently) be coded
# as code 284 (base position 227) + 31 in the 5 extra bits. The construction
# in the loop above would assign 259 to code 285 instead.
base_lengths[-1] = 258
extra_dist_bits = [0, 0] + [i//2 for i in range(28)]
base_dist = 1
base_dists = []
for extra in extra_dist_bits:
  base_dists.append(base_dist)
  base_dist += (1 << extra)

outfile = open('gunzip.out', 'wb')
bitstream = Bitstream(gz[off:-7])  # Normally -8, but include a padding byte.
window = Window(window_size=15)  # deflate always uses 32k windows,
                                 # and the window is shared over blocks
is_last_block = False
while not is_last_block:
  is_last_block = bitstream.getbit()
  block_type = bitstream.getbits(2)
  #print(is_last_block, block_type)
  assert block_type != 3, 'invalid block'
  if block_type == 0:
    data = bitstream.parse_uncompressed_block()
    window.output_block(data)
    outfile.write(data)
    continue
  if block_type == 2:
    # dynamic huffman code, read huffman tree description
    num_literals_lengths = bitstream.getbits(5) + 257
    num_distances = bitstream.getbits(5) + 1
    num_pretree = bitstream.getbits(4) + 4
    pretree_order = [16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15]  # per rfc
    pretree_lengths = [0]*19
    for i in range(num_pretree):
      v = bitstream.getbits(3)
      pretree_lengths[pretree_order[i]] = v
    pretree = HuffTree(pretree_lengths)
    # "The code length repeat codes can cross from HLIT + 257 to the HDIST + 1
    # code lengths", so we have to use a single list for the huflengths here.
    lengths = deflate_decode_pretree(pretree, bitstream,
                                     num_literals_lengths + num_distances)
    litlengths = lengths[:num_literals_lengths]
    distlengths = lengths[num_literals_lengths:]
  else:
    assert block_type == 1
    # fixed huffman code, use fixed deflate tree
    litlengths = [8]*144 + [9]*(256-144) + [7]*(280-256) + [8]*(288-280)
    distlengths = [5]*30

  littree = HuffTree(litlengths)
  disttree = HuffTree(distlengths)

  code = littree.readsym(bitstream)
  win_start = window.win_write
  while code != 256:
    if code < 256:
      # literal
      window.output_literal(code)
      #print('lit', code)
    else:
      # match. codes 257..285 represent lengths 3..258 (hence some bits might
      # have to follow the mapped code).
      code -= 257
      match_len = base_lengths[code] + bitstream.getbits(extra_len_bits[code])
      dist = disttree.readsym(bitstream)
      match_offset = base_dists[dist] + bitstream.getbits(extra_dist_bits[dist])
      window.copy_match(match_offset, match_len)
      #print('match', match_offset, match_len)
    # We could write to disk right after each literal and match, but instead
    # just write every 16kB. Since the max match is 258 bytes, we won't miss
    # data in the window after a long match: long matches are still short
    # compared to the window size.
    if (window.win_write - win_start) & 0x7fff > 0x3fff:
      window.write_from(outfile, win_start)
      win_start = window.win_write
    code = littree.readsym(bitstream)
  window.write_from(outfile, win_start)
  # win_write is set again before the decode while loop for the next block.
