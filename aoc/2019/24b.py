from __future__ import print_function
import collections
import fileinput
import itertools


def count(grids, l, x, y):
    g = grids[l]
    c = 0
    if x == 0 and l > 0 and grids[l-1][2][1] == '#': c += 1
    if x == 4 and l > 0 and grids[l-1][2][3] == '#': c += 1

    if y == 0 and l > 0 and grids[l-1][1][2] == '#': c += 1
    if y == 4 and l > 0 and grids[l-1][3][2] == '#': c += 1

    b = l < len(grids) - 1
    if x == 1 and y == 2 and b:
        for i in range(5):
            if grids[l+1][i][0] == '#': c += 1
    if x == 3 and y == 2 and b:
        for i in range(5):
            if grids[l+1][i][4] == '#': c += 1
    if x == 2 and y == 1 and b:
        for i in range(5):
            if grids[l+1][0][i] == '#': c += 1
    if x == 2 and y == 3 and b:
        for i in range(5):
            if grids[l+1][4][i] == '#': c += 1

    # The center tile is always '.', so don't need special checks for not
    # counting it.
    if x > 0 and g[y][x - 1] == '#': c += 1
    if x < 4 and g[y][x + 1] == '#': c += 1
    if y > 0 and g[y - 1][x] == '#': c += 1
    if y < 4 and g[y + 1][x] == '#': c += 1
    return c


def step(grids, l):
    newgrid = [['.'] * 5 for _ in range(5)]
    for y in range(5):
        for x in range(5):
            if x == 2 and y == 2:
                continue
            if grids[l][y][x] == '#':
                newgrid[y][x] = '#' if count(grids, l, x, y) == 1 else '.'
            else:
                newgrid[y][x] = '#' if count(grids, l, x, y) in (1, 2) else '.'
    return newgrid


def rating(g):
    gridstr = ''.join(''.join(l) for l in g)[::-1]
    return int(gridstr.replace('#', '1').replace('.', '0'), 2)


# Initially, just one level contains bugs, and it takes 2 steps for bugs
# to cross a level, N/2 on each side is enough.
N = 200
grids = [[['.'] * 5 for _ in range(5)] for _ in range(N + 1)]
grids[N/2] = [list(l.rstrip()) for l in fileinput.input()]

for i in range(N):
    grids = [step(grids, j) for j in range(len(grids))]

#print('\n\n'.join(['\n'.join([''.join(line) for line in grid]) for grid in grids]))
c = collections.Counter()
print(sum(collections.Counter(itertools.chain.from_iterable(grid))['#']
          for grid in grids))

