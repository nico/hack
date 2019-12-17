from __future__ import print_function
import fileinput


# Like 16.py, but about 10x faster on the example input.
def step(state):
    start, step, sign = 0, 1, 1
    new_state = [0] * len(state)
    for i in range(len(state)):
        s = 0
        for k in range(start, len(state), 2*step):
            s += sign * sum(state[k:k+step])
            sign = -sign
        start += 1
        step += 1
        sign = 1
        new_state[i] = abs(s) % 10

    return new_state

n = [int(n) for n in fileinput.input()[0].rstrip()]
for i in range(100):
    n = step(n)
print(''.join(str(i) for i in n[0:8]))
