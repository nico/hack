from __future__ import print_function
import fileinput

n = 10007
c = 2019

def deal_new(c, n): return n - 1 - c
def deal_inc(c, n, i): return (c * i) % n

def cut(c, n, i):
    if i < 0:
        i = n + i
    if c < i:
        return c + n - i
    return c - i

for l in fileinput.input():
    if l == 'deal into new stack\n':
        c = deal_new(c, n)
    elif l.startswith('deal with increment '):
        c = deal_inc(c, n, int(l[len('deal with increment '):]))
    elif l.startswith('cut '):
        c = cut(c, n, int(l[len('cut '):]))

print(c)
