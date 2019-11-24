import random
from math import *

# 10923 is the first time the loop runs more than once

for k in xrange(0, 1000000):
    n, i = 0, 0
    while True:
        n_prime = ceil((4 * (k + n)) / 512.0)
        if n == n_prime:
            break;
        n = n_prime
        i += 1
    if n != ceil(4 * k / 508.0):
        print k, i, n, ceil(4 * k / 508.0)
