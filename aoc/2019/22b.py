from __future__ import division, print_function
import fileinput

n = 119315717514047
# n - 1 == 2 * 59657858757023
c = 2020

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

M = 101741582076661
# Now want to morally run:
# la, lb = a, b
# a = 1, b = 0
# for i in range(M):
#     a, b = (a * la) % n, (la * b + lb) % n

# For a, this is same as computing (a ** M) % n, which is in the computable
# realm with fast exponentiation.
# For b, this is same as computing ... + a**2 * b + a*b + b
# == b * (a**(M-1) + a**(M) + ... + a + 1) == b * (a**M - 1)/(a-1)
# That's again computable, but we need the inverse of a-1 mod n.

def pow_mod(a, b, n):
    r = 1
    while True:
        if b % 2 == 1: r = (r * a) % n
        b //= 2
        if b == 0: break
        a = (a * a) % n
    return r

print(a, b)

## Returns gcd, x, y such that a*x + b*y = gcd
def ext_gcd(a, b):
    if b == 0:
        return a, 1, 0
    gcd, x, y = ext_gcd(b, a % b)
    return gcd, y, x - (a//b) * y

def inv_0(a, n):
  g, x, y = ext_gcd(n, a)
  assert g == 1  # n is prime
  return y % n

# Derp, since n is prime, Fermat's little theorem gives a much simpler inv:
def inv(a, n): return pow_mod(a, n-2, n)

print('inv', inv(a-1, n), inv_0(a-1, n))

print((inv(a-1, n) * (a-1)) % n)
Ma = pow_mod(a, M, n)
Mb = (b * (Ma - 1) * inv(a-1, n)) % n
#print((Ma - 1) % (a-1))
#Mb = (b * (Ma - 1) / (a-1)) % n
print(Ma, Mb)

# Fermat's little theorem says that a ** -1 = a ** (n - 2), e.g.
# a ** (n - 1) == 1
# So want to compute (2 ** M) mod n - 1
# (2 ** (M-1)) ** (P - 1) == 1 mod P 
# 2 ** ((M-1) * (P - 1)) == 1 mod P 
# So want (M-1) * (P-1) mod M => 42083723319639

# This computes "where does 2020 end up", but I want "what is at 2020".
print((Ma * c + Mb) % n)

# So need to invert (2020 - MB) * inv(Ma)
print(((c - Mb) * inv(Ma, n)) % n)
