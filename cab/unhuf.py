class Bitstream(object):
  def __init__(self, source):
    self.curbit = 31
    # Note: '<I' instead of 'H' in dumpcab.py. This uses 16-bit-at-a-time
    # solely for convenience; the stream is per-byte -- but the bits go
    # from lowest-bit-to-highest-bit order, hence the subtraction in getbit()
    self.curword, self.curword_val = 0, struct.unpack_from('<I', source, 0)[0]
    self.source = source

  def getbit(self):
    bit = (self.curword_val >> (31 - self.curbit)) & 1
    self.curbit -= 1
    if self.curbit < 0:
      self.curbit = 31
      self.curword += 4  # in bytes
      if self.curword < len(self.source):
       self.curword_val = struct.unpack_from('<I', self.source, self.curword)[0]
    return bit

  def getbits(self, n):
    bits = 0
    for i in range(n):
      bits = (bits << 1) | self.getbit()
    return bits


class HuffTree(object):
  def __init__(self, nodelengths):
    self.codes = HuffTree._canon_tree(nodelengths)

  def readsym(self, bitstream):
    curbits, curlen = 0, 0
    code = None
    while code is None:
      curbits = (curbits << 1) | bitstream.getbit()
      curlen += 1
      code = self.codes.get((curlen, curbits))
    return code

  # The canonical huffman trees match rfc1951.
  @staticmethod
  def _canon_tree(lengths):
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
    # XXX go reverse?
    for i, len_i in enumerate(lengths):
      if len_i != 0:
        # Using a dict for this is very inefficient.
        codes[(len_i, next_code[len_i])] = i
        next_code[len_i] += 1
    return codes


import struct
import sys

huf = open(sys.argv[1], 'rb').read()
comp, uncomp = struct.unpack_from('II', huf, 0)
# More traditional is to store the length of each value.  This instead stores
# how many nodes are at each level, and a value assignment.
codelengths = struct.unpack_from('11I', huf, 8)
print codelengths
bytevals = struct.unpack_from('256B', huf, (2+11)*4)
assert sorted(bytevals) == range(256)
# Convert to the usual format (which _canon_tree() then kind of undoes again)
lengths = [None] * 256
cur = 255
for i, v in enumerate(codelengths):
  for j in xrange(v):
    if cur < 0: break
    lengths[bytevals[cur]] = i+1
    cur -= 1

tree = HuffTree(lengths)
print tree.codes
bitstream = Bitstream(huf[(2+11)*4+256:])
for i in xrange(uncomp):
  b = tree.readsym(bitstream)
  print hex(b)
  if i > 10:
    sys.exit(1)
