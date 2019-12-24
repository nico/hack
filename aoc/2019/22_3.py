from __future__ import print_function
import fileinput

n = 10007
c = 2019

def deal_new(c, n):    return (-c - 1) % n
def deal_inc(c, n, i): return ( c * i) % n
def cut(c, n, i):      return ( c - i) % n

for l in fileinput.input():
    if l == 'deal into new stack\n':
        c = deal_new(c, n)
    elif l.startswith('deal with increment '):
        c = deal_inc(c, n, int(l[len('deal with increment '):]))
    elif l.startswith('cut '):
        c = cut(c, n, int(l[len('cut '):]))

print(c)
