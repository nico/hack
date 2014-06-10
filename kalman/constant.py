#!/usr/bin/env python

# Simple constant state with small measurement noise:
# x_k = x_{k-1}
# z_k = x_k + N(0, r)
# (i.e. F_k = 1, B_k = 0, Q_k = 0, H_k = 1)

import random

N = 25
truex = 8
r = 1.5

trueX = N * [truex]
Z = [x + random.gauss(0, r) for x in trueX]

x = Z[0]
p = 10000000

estX = []
estP = []
for z in Z:
  k = p / (p + r)

  x = x + k * (z - x)
  p = (1 - k)*p

  estX.append(x)
  estP.append(p)

avgs = [sum(Z[0:n]) / n for n in range(1, N+1)]
print ['%.2f' % x for x in trueX]
print ['%.2f' % z for z in Z]
print ['%.2f' % x for x in estX]
print ['%.2f' % x for x in avgs]
print ['%.2f' % p for p in estP]

import pylab
pylab.plot(trueX, label='truth')
pylab.plot(Z, label='measurements')
pylab.plot(estX, label='kalman estimate')
pylab.plot(avgs, label='avg estimate')
pylab.plot(estP, label='kalman error')
pylab.legend(loc=4)
pylab.show()
