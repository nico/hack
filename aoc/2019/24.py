from __future__ import print_function
import fileinput


def count(g, x, y):
    c = 0
    if x > 0 and g[y][x - 1] == '#': c += 1
    if x < 4 and g[y][x + 1] == '#': c += 1
    if y > 0 and g[y - 1][x] == '#': c += 1
    if y < 4 and g[y + 1][x] == '#': c += 1
    return c


def step(g):
    newgrid = [['.'] * 5 for _ in range(5)]
    for y in range(5):
        for x in range(5):
            if g[y][x] == '#':
                newgrid[y][x] = '#' if count(g, x, y) == 1 else '.'
            else:
                newgrid[y][x] = '#' if count(g, x, y) in (1, 2) else '.'
    return newgrid


def rating(g):
    gridstr = ''.join(''.join(l) for l in g)[::-1]
    return int(gridstr.replace('#', '1').replace('.', '0'), 2)


grid = [list(l.rstrip()) for l in fileinput.input()]
seen = set()
while True:
    r = rating(grid)
    if r in seen:
        print(r)
        break
    seen.add(r)
    grid = step(grid)
