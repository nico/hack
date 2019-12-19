from __future__ import print_function
import collections
import itertools
import fileinput
import heapq

# Kind of slow, takes 7s to run :/


grid = [s.rstrip() for s in fileinput.input()]

lingrid = list(itertools.chain.from_iterable(grid))
w, h = len(grid[0]), len(grid)
n = lingrid.index('@')
x, y = n % w, n // w
allkeys = set(c for c in lingrid if c.islower())


def reachable_keys(sx, sy, keys):
    q = collections.deque([(sx, sy, 0)])
    #print(q)
    seen = set()
    d = ( (-1, 0), (1, 0), (0, -1), (0, 1) )
    while q:
        cx, cy, l = q.popleft()
        if grid[cy][cx].islower() and grid[cy][cx] not in keys:
            yield l, cx, cy, grid[cy][cx]
            continue
        for dx, dy in d:
            nx, ny = cx + dx, cy + dy
            if ((nx, ny)) in seen:
                continue
            seen.add((nx, ny))

            c = grid[ny][nx]
            if c != '#' and (not c.isupper() or c.lower() in keys):
                q.append((nx, ny, l + 1))


# Shortest path from node "no keys" to node "all keys".
q = [(0, x, y, frozenset())]
seen = set()
while q:
    #print(q)
    d, cx, cy, keys = heapq.heappop(q)
    if keys == allkeys:
        print(d)
        break

    if (cx, cy, keys) in seen:
        continue
    seen.add((cx, cy, keys))

    for l, nx, ny, key in reachable_keys(cx, cy, keys):
        heapq.heappush(q, (d + l, nx, ny, keys | frozenset([key])))
