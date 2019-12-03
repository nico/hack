from __future__ import division, print_function
import fileinput

def parse_line(l):
    dls = [(p[0], int(p[1:])) for p in l.split(',')]

    dirs = { 'R': (1, 0), 'L': (-1, 0), 'D': (0, -1), 'U': (0, 1) }
    p, n = [0, 0], 0
    points = set()  # Don't include 0, 0.
    dists = {}
    for d, l in dls:
        d = dirs[d]
        for i in range(l):
            p[0] += d[0]
            p[1] += d[1]
            n += 1
            points.add(tuple(p))
            dists.setdefault(tuple(p), n)

    return points, dists

line1, line2 = list(fileinput.input())

l1p, l1d = parse_line(line1.rstrip())
l2p, l2d = parse_line(line2.rstrip())
common = l1p & l2p
print(min(l1d[x] + l2d[x] for x in common))
