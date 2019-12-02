from __future__ import division, print_function
import fileinput
def fuel(n):
    total = 0
    while n != 0:
        n = max(n // 3 - 2, 0)
        total += n
    return total
print(sum(fuel(int(l.rstrip())) for l in fileinput.input()))
