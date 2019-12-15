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

#print(topo)
need = collections.defaultdict(int, FUEL=1)
for raw in topo:
    #print(need)
    for c in react[raw][1]:
        need[c] += math.ceil(need[raw] / react[raw][0]) * react[raw][1][c]
    del need[raw]

print(int(need['ORE']))
