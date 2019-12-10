from __future__ import print_function
import fileinput
import fractions

m = list(fileinput.input())
points = [ (x, y) for y, l in enumerate(m) for x, c in enumerate(l) if c == '#']
seen = []
for p in points:
    dirs = set()
    for p2 in points:
        d = (p2[0] - p[0], p2[1] - p[1])
        g = abs(fractions.gcd(d[0], d[1]))
        if g != 0:
            dirs.add((d[0]/g, d[1]/g))
    seen.append(len(dirs))
print(max(seen))
