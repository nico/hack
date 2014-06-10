#!/usr/bin/env python

# Simple constant state with small measurement noise:
# x_k = x_{k-1}
# z_k = x_k + N(0, r)
# (i.e. F_k = 1, B_k = 0, Q_k = 0, H_k = 1)

import random

N = 25
truex = 8
r = 0.5

trueX = N * [truex]
Z = [x + random.gauss(0, r) for x in trueX]

x = 0
p = 10

estX = []
estP = []
for z in Z:
  y = z - x
  s = p + r
  k = p / s

  x += k * y
  p = (1 - k)*p

  estX.append(x)
  estP.append(p)

print ['%.2f' % x for x in trueX]
print ['%.2f' % z for z in Z]
print ['%.2f' % x for x in estX]
print sum(Z) / len(Z)
print ['%.2f' % p for p in estP]
