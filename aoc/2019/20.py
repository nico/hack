from __future__ import print_function
import collections
import fileinput

grid = [s.rstrip('\n') for s in fileinput.input()]
w, h = len(grid[0]), len(grid)
print('\n'.join(grid))

# Find portals.
portals = collections.defaultdict(list)

for y in range(0, h-1):
    for x in range(0, w-1):
        if not grid[y][x].isupper():
            continue
        if grid[y+1][x].isupper():
            c = grid[y][x] + grid[y+1][x]
            if y > 0 and grid[y-1][x] == '.':     portals[c].append((x, y-1))
            elif y < h-2 and grid[y+2][x] == '.': portals[c].append((x, y+2))
        if grid[y][x+1].isupper():
            c = grid[y][x] + grid[y][x+1]
            if x > 0 and grid[y][x-1] == '.':     portals[c].append((x-1, y))
            elif x < w-2 and grid[y][x+2] == '.': portals[c].append((x+2, y))

portals = dict(portals)

src = portals['AA'][0]
dst = portals['ZZ'][0]

# Find path.
q = collections.deque([(src, 0)])
seen = set()
d = ( (-1, 0), (1, 0), (0, -1), (0, 1) )
while q:
    #print(q)
    pos, l = q.popleft()
    if pos == dst:
        break

    for dx, dy in d:
        nx, ny = (pos[0] + dx, pos[1] + dy)
        if grid[ny][nx].isupper():
            c = (grid[min(ny, ny + dy)][min(nx, nx+dx)] +
                 grid[max(ny, ny + dy)][max(nx, nx + dx)])
            if c not in ('AA', 'ZZ'):
                if pos == portals[c][0]:
                    nx, ny = portals[c][1]
                else:
                    nx, ny = portals[c][0]

        if ((nx, ny)) in seen:
            continue
        seen.add((nx, ny))

        c = grid[ny][nx]
        if c == '.':
            q.append(((nx, ny), l + 1))

print(portals)
print(l)
