#!/usr/bin/env python

"""a deflate implementation (in the .gz container format)"""

from datetime import datetime
import struct
import sys


if len(sys.argv) > 1:
  source = open(sys.argv[1], 'rb')
else:
  if sys.platform == 'win32':
    import os, msvcrt
    msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
  source = sys.stdin
gz = source.read()

# rfc1952 describes the gzip header and footer.
assert gz[0:2] == '\x1f\x8b'
assert ord(gz[2]) == 8  # compression method deflate
flags = ord(gz[3])
mtime = struct.unpack_from('<I', gz, 4)[0]
extra_flags = ord(gz[8])
os = ord(gz[9])
off = 10

print 'flags', flags
print 'mtime', datetime.fromtimestamp(mtime).strftime('%Y-%m-%d %H:%M:%S')
print 'extra_flags', extra_flags
print 'os', os

if flags & 4:  # extra
  extra_size = struct.unpack_from('<I', gz, off)
  off += 4 + extra_size
  print 'extra size', extra_size

if flags & 8:  # filename
  nul = gz.index('\0', off)
  assert nul != -1
  name = gz[off:nul]  # latin-1
  off = nul + 1
  print 'name', name

if flags & 16:  # comment
  nul = gz.index('\0', off)
  assert nul != -1
  comment = gz[off:nul]  # latin-1
  off = nul + 1
  print 'comment', comment

if flags & 2:  # crc16 of gzip header
  print 'header crc16', struct.unpack_from('<H', gz, off)
  off += 2

# deflate data starts at off. last 8 bytes of file are crc32 of uncompressed
# data and the uncompressed size mod 2^32.

# rfc1951 describes the deflate bitstream.

class Bitstream(object):
  def __init__(self, source):
    self.curbit = 7
    self.curword, self.curword_val = 0, struct.unpack_from('B', source, 0)[0]
    self.source = source

  def getbit(self):
    # deflate orders bits right-to-left.
    bit = (self.curword_val >> (7 - self.curbit)) & 1
    self.curbit -= 1
    if self.curbit < 0:
      self.curbit = 7
      self.curword += 1  # in bytes
      if self.curword < len(self.source):
        self.curword_val = struct.unpack_from('B', self.source, self.curword)[0]
    return bit

  def getbits(self, n):
    # Doing this bit-by-bit is inefficient; this should try to bunch things up.
    bits = 0
    for i in range(n):
      # deflate orders bits right-to-left.
      bits = bits | (self.getbit() << i)
    return bits


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
    # Given the lengths of the nodes in a canonical huffman tree,
    # returns a (last_code, codes) tuple, where last_code[n] returns the
    # last prefix of length n, and codes[n] contains a list of all values of
    # prefixes with length n.
    maxlen = max(lengths)
    bl_count = [0] * (maxlen + 1)
    codes = [[] for _ in xrange(maxlen + 1)]
    for i, len_i in enumerate(lengths):
      if len_i != 0:
        bl_count[len_i] += 1
        codes[len_i].append(i)
    code = 0
    last_code = [0] * (maxlen + 1)
    for i in xrange(1, maxlen + 1):
      code = (code + bl_count[i - 1]) << 1
      last_code[i] = code + bl_count[i] - 1
    return last_code, codes



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


bitstream = Bitstream(gz[off:-8])
is_last_block = False
while not is_last_block:
  is_last_block = bitstream.getbit()
  block_type = bitstream.getbits(2)
  print is_last_block, block_type
  assert block_type != 3, 'invalid block'
  assert block_type != 0, 'unsupported uncompressed block'
  if block_type == 2:
    assert False, 'not yet implemented'
    # dynamic huffman code, read huffman tree description
    num_literals_lengths = bitstream.getbits(5) + 257
    num_distances = bitstream.getbits(5) + 1
    num_pretree = bitstream.getbits(4) + 4
    print num_literals_lengths, num_distances, num_pretree
    pretree_order = [16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15]  # per rfc
    pretree_lengths = [0]*19
    for i in xrange(num_pretree):
      v = bitstream.getbits(3); print 'v', v
      pretree_lengths[pretree_order[i]] = v
    print pretree_lengths
    pretree = HuffTree(pretree_lengths)
    # "The code length repeat codes can cross from HLIT + 257 to the HDIST + 1
    # code lengths", so we have to use a single list for the huflengths here.
    lengths = deflate_decode_pretree(pretree, bitstream,
                                     num_literals_lengths + num_distances)
    print lengths
    litlengths = lengths[:num_literals_lengths]
    distlengths = lengths[num_literals_lengths:]
  else:
    assert block_type == 1
    # fixed huffman code, used fixed deflate tree
    litlengths = [8]*144 + [9]*(256-144) + [7]*(280-256) + [8]*(288-8)
    distlengths = [5]*30

  littree = HuffTree(litlengths)
  disttree = HuffTree(distlengths)

  code = littree.readsym(bitstream)
  while code != 256:
    if code < 256:
      # literal
      sys.stdout.write(chr(code))
    else:
      # match
      assert False, 'not yet implemented'
    code = littree.readsym(bitstream)
