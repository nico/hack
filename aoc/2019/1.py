from __future__ import division, print_function
import fileinput
print(sum(int(l.rstrip()) // 3 - 2 for l in fileinput.input()))
