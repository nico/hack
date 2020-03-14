# See https://www.guballa.de/implementierung-eines-vigenere-solvers for a
# different approach.

import base64
import itertools
import math
import string


bits = [bin(i).count('1') for i in range(256)]
def hamming_bits(s1, s2):
    if len(s1) != len(s2): raise ValueError
    n = 0
    for i in xrange(len(s1)):
        n += bits[ord(s1[i]) ^ ord(s2[i])]
    return n
assert hamming_bits('this is a test', 'wokka wokka!!!') == 37


def guess_key_len(s, maxkey=50):
    """Returns list of key len candidates in likelihood order (best first)."""
    def autocorrelation(l):
        a = 0
        for i in range(2*l, len(s), 2*l):
            a += hamming_bits(s[i - 2*l:i - l], s[i - l:i])  # best
        #a = (hamming_bits(s[:l], s[l:2*l]) +
             #hamming_bits(s[2*l:3*l], s[3*l:4*l])) / float(l)
        #a = hamming_bits(s[:l], s[l:2*l]) / float(l)  # worst
        #print l, a
        return a
    #print sorted(range(2, maxkey+1), key=autocorrelation)
    return sorted(range(2, maxkey+1), key=autocorrelation)


printset = set(string.printable)
freq = {
    'e':math.log(0.127), 't':math.log(0.094), 'a':math.log(0.082),
    'o':math.log(0.075), 'i':math.log(0.070), 'n':math.log(0.067),
    's':math.log(0.063), 'h':math.log(0.061), 'r':math.log(0.060),
    'd':math.log(0.043), 'l':math.log(0.040), 'u':math.log(0.028),
    'w':math.log(0.026), 'm':math.log(0.024), 'f':math.log(0.023),
    'c':math.log(0.022), 'g':math.log(0.020), 'y':math.log(0.020),
    'p':math.log(0.019), 'b':math.log(0.015), 'k':math.log(0.013),
    'v':math.log(0.010), 'j':math.log(0.002), 'x':math.log(0.002),
    'q':math.log(0.001), 'z':math.log(0.001),
    #' ': 0, '.': 0, '?': 0, ',': 0, '"': 0, '\n': 0,
}
min_freq = min(freq.values())
def score_char(m):
    """returns how likely m is an english text using character frequency"""
    def lnPc(c):
        if c not in printset: return min_freq - math.log(1000000)
        return freq.get(c.lower(), min_freq - math.log(10000))
    return sum(lnPc(c) for c in m)
assert score_char('Hello') > score_char('whyme') > score_char('Zzyzx')


def xor(s1, s2):
    return ''.join(chr(ord(a) ^ ord(b)) for a, b in zip(s1, s2))


def guess_key_part(column):
    def score_key(k):
        return score_char(xor(chr(k) * len(column), column))
    return max(range(256), key=score_key)


def guess_key(s, key_len):
    return ''.join([chr(guess_key_part(s[i::key_len])) for i in range(key_len)])


c = base64.b64decode(open('6.txt').read())

candidates = []
for key_len in guess_key_len(c)[:8]:
    key = guess_key(c, key_len)
    m = xor(itertools.cycle(key), c)
    candidates.append((score_char(m), key, m))
    #print
    #print key_len, key
    #print repr(m[:450])

score, key, m =  max(candidates)
print score
print key
print m
