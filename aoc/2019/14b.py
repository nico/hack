from __future__ import division, print_function
import collections
import math
import fileinput


def parse_edge(e):
    cost, name = e.split()
    return int(cost), name

react = {}
for l in fileinput.input():
    lhs, rhs = l.rstrip().split(' => ')
    lhs = [parse_edge(e) for e in lhs.split(', ')]
    rhs_cost, rhs_name = parse_edge(rhs)
    react[rhs_name] = rhs_cost, {lhs_name: lhs_cost
                                 for lhs_cost, lhs_name in lhs}

topo = []
dfs_visited = set(['ORE'])
def dfs(n):
    if n in dfs_visited:
        return
    dfs_visited.add(n)
    for c in react[n][1]:
        dfs(c)
    topo.append(n)
dfs('FUEL')
topo.reverse()

def n_fuel(n):
    need = collections.defaultdict(int, FUEL=n)
    for raw in topo:
        #print(need)
        for c in react[raw][1]:
            need[c] += math.ceil(need[raw] / react[raw][0]) * react[raw][1][c]
        del need[raw]
    return int(need['ORE'])

# XXX Try max-flow from ORE to FUEL? But short on time, so bin search for now.
lo = 1000000000000 // n_fuel(1)
hi = 1000000000000
while hi - lo > 1:
    mid = lo + (hi - lo) // 2
    ore = n_fuel(mid)
    if ore <= 1000000000000:
        lo = mid
    else:
        hi = mid

print(lo)
