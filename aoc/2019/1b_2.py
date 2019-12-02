from __future__ import division, print_function
import fileinput
def fuel(n):
    while n != 0:
        n = max(n // 3 - 2, 0)
        yield n
print(sum(sum(fuel(int(l.rstrip()))) for l in fileinput.input()))

# Haskell:
# fuel n = sum (takeWhile (>0) (tail (iterate (\x -> div x 3 - 2) n))
