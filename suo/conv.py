import random
from math import *

# This tries a few things for how to compute the number of FAT blocks
# (ignoring DIFAT blocks for now) in a ms-cfb file.

# An ms-cfb file contains of 512-byte sectors (for v3), and the FAT stores
# the "next" sector for each sector, so that the file can contain several
# interleaving streams. The FAT is stored in sectors too. Each sector can
# contain 128 uint32_ts.

# Three ways to think about this:
# 1. One FAT sector can store 128 uint32_ts, but 1 of these is needed to store
#    the ID of that FAT sector, giving us 127 effective sectors per FAT sector,
#    so if we have k sectors we need ceil(k/127) FAT sectors to link them all
#    up.
# 2. We need n = ceil(k/128) FAT sectors, but then we need to store their IDs
#    in the FAT too, so we keep iterating n' = ceil((k + n/128), n = n' until
#    we converge.
# 3. We require `n == ceil((k+n)/128)` and solve for n, which (maybe?)
#    results in ceil(k/127) as well.

# 10923 is the first time the loop runs more than once

mi = 0
for k in xrange(0, 1000000):
    n, i = 0, 0
    while True:
        n_prime = ceil((k + n) / 128.0)
        if n == n_prime:
            break;
        n = n_prime
        i += 1
    mi = max(i, mi)
    if n != ceil(k / 127.0):
        print k, i, n, ceil(k / 127.0)

print mi
