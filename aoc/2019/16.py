from __future__ import print_function
import itertools
import fileinput


def pattern(i):
    it = itertools.cycle(itertools.chain(
        itertools.repeat(0, i),
        itertools.repeat(1, i),
        itertools.repeat(0, i),
        itertools.repeat(-1, i)))
    next(it)
    return it


def step(state):
    return [abs(sum([a*b for a, b in zip(state, pattern(i+1))])) % 10
            for i in range(len(state))]


print_pattern = True
if print_pattern:
    n = 160
    for i in range(1, n + 1):
        c = { 1: '1', 0: '.', -1: '!' }
        print('%3d' % i, ''.join(c[k] for k in itertools.islice(pattern(i), n)))

n = [int(n) for n in fileinput.input()[0].rstrip()]
for i in range(100):
    n = step(n)
print(''.join(str(i) for i in n[0:8]))
