from __future__ import division, print_function
import fileinput

def parse_line(l):
    dls = [(p[0], int(p[1:])) for p in l.split(',')]

    dirs = { 'R': (1, 0), 'L': (-1, 0), 'D': (0, -1), 'U': (0, 1) }
    x, y = 0, 0
    points = set()  # Don't include 0, 0.
    for d, l in dls:
        d = dirs[d]
        for i in range(l):
            x += d[0]
            y += d[1]
            points.add((x, y))

    return points

line1, line2 = list(fileinput.input())

common = parse_line(line1.rstrip()) & parse_line(line2.rstrip())
print(min(abs(x[0]) + abs(x[1]) for x in common))
