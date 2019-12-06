from __future__ import print_function
import fileinput

dag = {}
for l in fileinput.input():
    p, o = l.rstrip().split(')')
    dag.setdefault(p, []).append(o)

def count(p, d):
    return d + sum(count(o, d + 1) for o in dag.get(p, []))

print(count('COM', 0))
