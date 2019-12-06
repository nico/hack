import fileinput

g = {}
for l in fileinput.input():
    p, o = l.rstrip().split(')')
    g.setdefault(p, []).append(o)
    g.setdefault(o, []).append(p)

def walk(src, cur, dst, n):
    if cur == dst: return n
    return min((walk(cur, o, dst, n + 1) for o in g[cur] if o != src),
               default=len(g))

print(walk(None, 'YOU', 'SAN', 0) - 2)
