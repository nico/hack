from __future__ import print_function, division
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

grid[y-1] = grid[y-1][:x]   +  '#'  + grid[y-1][x+1:]
grid[  y] = grid[y  ][:x-1] + '###' + grid[y  ][x+2:]
grid[y+1] = grid[y+1][:x]   +  '#'  + grid[y+1][x+1:]
#print('\n'.join(grid))

pos = (
  (x-1, y-1),
  (x+1, y-1),
  (x-1, y+1),
  (x+1, y+1),
)


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
q = [(0, pos, frozenset())]
seen = set()
while q:
    #print(q)
    #print(q[0])
    d, cpos, keys = heapq.heappop(q)
    #print(sorted(keys))
    if keys == allkeys:
        print(d)
        break

    if (cpos, keys) in seen:
        continue
    seen.add((cpos, keys))

    for i, (cx, cy) in enumerate(cpos):
        for l, nx, ny, key in reachable_keys(cx, cy, keys):
            npos = cpos[0:i] + ((nx, ny),) + cpos[i+1:]
            heapq.heappush(q, (d + l, npos, keys | frozenset([key])))
