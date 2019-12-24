from __future__ import print_function
import fileinput

n = 10007
c = 2019

moves = list(fileinput.input())

a, b = 1, 0
for l in moves:
    if l == 'deal into new stack\n':
        la, lb = -1, -1
    elif l.startswith('deal with increment '):
        la, lb = int(l[len('deal with increment '):]), 0
    elif l.startswith('cut '):
        la, lb = 1, -int(l[len('cut '):])
    # la * (a * x + b) + lb == la * a * x + la*b + lb
    # The `% n` doesn't change the result, but keeps the numbers small.
    a = (la * a) % n
    b = (la * b + lb) % n

print(a, b)
print((a * c + b) % n)

M = 1

def deal_new(d): return list(reversed(d))
def deal_inc(d, n):
  nd = [-1] * len(d)
  for i in range(len(d)):
      nd[(i*n) % len(d)] = d[i]
  return nd
def cut(d, i): return d[i:] + d[:i]

deck = range(n)
for _ in range(M):
    for l in moves:
        if l == 'deal into new stack\n':
            deck = deal_new(deck)
        elif l.startswith('deal with increment '):
            deck = deal_inc(deck, int(l[len('deal with increment '):]))
        elif l.startswith('cut '):
            deck = cut(deck, int(l[len('cut '):]))
print('witness0', deck[c])

def deal_new(c, n):    return (-c - 1) % n
def deal_inc(c, n, i): return ( c * i) % n
def cut(c, n, i):      return ( c - i) % n

lc = c
for _ in range(M):
    for l in moves:
        if l == 'deal into new stack\n':
            lc = deal_new(lc, n)
        elif l.startswith('deal with increment '):
            lc = deal_inc(lc, n, int(l[len('deal with increment '):]))
        elif l.startswith('cut '):
            lc = cut(lc, n, int(l[len('cut '):]))
print('witness1', lc)

Ma, Mb = 1, 0
for _ in range(M):
    for l in moves:
        if l == 'deal into new stack\n':
            la, lb = -1, -1
        elif l.startswith('deal with increment '):
            la, lb = int(l[len('deal with increment '):]), 0
        elif l.startswith('cut '):
            la, lb = 1, -int(l[len('cut '):])
        # la * (a * x + b) + lb == la * a * x + la*b + lb
        # The `% n` doesn't change the result, but keeps the numbers small.
        Ma = (la * Ma) % n
        Mb = (la * Mb + lb) % n
print('witness', Ma, Mb, '=>', (Ma * c + Mb) % n)


def pow_mod(a, b, n):
    r = 1
    while True:
        if b % 2 == 1: r = (r * a) % n
        b //= 2
        if b == 0: break
        a = (a * a) % n
    return r

def inv(a, n): return pow_mod(a, n-2, n)

Ma = pow_mod(a, M, n)
Mb = (b * (Ma - 1) * inv(a-1, n)) % n
print('fast   ', Ma, Mb, '=>', (Ma * c + Mb) % n)
