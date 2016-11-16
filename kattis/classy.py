t = int(raw_input())
for _ in xrange(t):
  n = int(raw_input())
  p = []
  for _ in xrange(n):
    name, rest = raw_input().split(': ')
    r = { 'upper': '0', 'middle': '1', 'lower': '2' }
    key = ''.join(reversed([r[s] for s in rest.split(' ')[0].split('-')]))
    key = key.ljust(10, '1')
    p.append((key, name))
  for _, name in sorted(p):
    print name
  print '=' * 30
