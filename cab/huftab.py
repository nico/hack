def makehuftab(lengths):
  # XXX comment
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
  tab = [0] * (1 << maxlen)
  for i, len_i in enumerate(lengths):
    if len_i != 0:
      c = next_code[len_i]
      val = c << (maxlen - len_i)
      # This keeps the relevant bits on the left and padding bits on the right.
      for j in range(val, val + (1 << (maxlen - len_i))):
        tab[j] = (len_i, i)
      next_code[len_i] += 1
  return tab


def makehufdict(lengths):
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


def main():
  print makehuftab([1, 1]), makehufdict([1, 1])
  print makehuftab([2, 1, 2]), makehufdict([2, 1, 2])

if __name__ == '__main__':
  main()
