from __future__ import print_function
import itertools
import fileinput


def step(state):
    # Set up so that sum(state[k:k+n]) == cumsum[k+n] - cumsum[k]
    # accumulate()'s initial= was added in 3.8, so have to manually prepend :/
    cumsum = [0] + list(itertools.accumulate(state))

    start, step, sign = 0, 1, 1
    new_state = [0] * len(state)
    for i in range(len(state)):
        s = 0
        for k in range(start, len(state), 2*step):
            s += sign * (cumsum[min(k+step, len(cumsum)-1)] - cumsum[k])
            sign = -sign
        start += 1
        step += 1
        sign = 1
        new_state[i] = abs(s) % 10

    return new_state


n = [int(n) for n in fileinput.input()[0].rstrip()] * 10000
offset = int(n[0:7], 10)
for i in range(100):
    n = step(n)
print(''.join(str(i) for i in n[offset:offset+8]))
