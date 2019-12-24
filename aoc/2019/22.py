from __future__ import print_function
import fileinput

deck = range(10007)

def deal_new(d): return list(reversed(d))
def deal_inc(d, n):
  nd = [-1] * len(d)
  for i in range(len(d)):
      nd[(i*n) % len(d)] = d[i]
  return nd
def cut(d, i): return d[i:] + d[:i]

for l in fileinput.input():
    if l == 'deal into new stack\n':
        deck = deal_new(deck)
    elif l.startswith('deal with increment '):
        deck = deal_inc(deck, int(l[len('deal with increment '):]))
    elif l.startswith('cut '):
        deck = cut(deck, int(l[len('cut '):]))

print(deck.index(2019))
