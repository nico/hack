from operator import add, mul, truediv, sub
n = int(raw_input())
for i in xrange(n):
  a, op, b, eq, c = raw_input().split()
  bases = []
  for base in xrange(1, 37):
    try:
      if base == 1:
        if (a.count('1') != len(a) or
            b.count('1') != len(b) or
            c.count('1') != len(c)):
          continue
        na = len(a); nb = len(b); nc = len(c)
      else:
        na = int(a, base); nb = int(b, base); nc = int(c, base)
    except: continue
    if { '+': add, '*': mul, '/': truediv, '-': sub}[op](na, nb) == nc:
      if base <= 9:    bases.append(str(base))
      elif base <= 35: bases.append(chr(ord('a') + base - 10))
      else:            bases.append('0')
  if bases: print ''.join(bases)
  else:     print 'invalid'
