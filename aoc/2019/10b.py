from __future__ import print_function
import fileinput
import fractions
import math

m = list(fileinput.input())
points = [ (x, y) for y, l in enumerate(m) for x, c in enumerate(l) if c == '#']
seen = []
for p in points:
    dirs = {}
    for p2 in points:
        d = (p2[0] - p[0], p2[1] - p[1])
        g = abs(fractions.gcd(d[0], d[1]))
        if g != 0:
            dirs.setdefault((d[0]/g, d[1]/g), []).append(
                (math.hypot(d[0], d[1]), p2))
    seen.append((len(dirs), p, dirs))

m, p, dirmap = max(seen)
for d in dirmap:
    dirmap[d] = sorted(dirmap[d])

print(m, p)
dirs = sorted(dirmap.keys(), key=lambda x: -math.atan2(x[0], x[1]))
for i, d in enumerate(dirs):
    print(i + 1, d, dirmap[d])
