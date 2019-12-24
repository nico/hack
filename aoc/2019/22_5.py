from __future__ import print_function
import fileinput

n = 10007
c = 2019

def lin(a, b, x, n): return (a*x + b) % n

def deal_new(c, n):    return lin(-1, -1, c, n)
def deal_inc(c, n, i): return lin(i, 0, c, n)
def cut(c, n, i):      return lin(1, -i, c, n)


a, b = 1, 0

for l in fileinput.input():
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
for i in range(30):
    print(i, (a ** i) % n)
print((a * c + b) % n)
