import math
import string


def from_hex(s):
    return s.decode('hex')


def xor(s1, s2):
    return ''.join(chr(ord(a) ^ ord(b)) for a, b in zip(s1, s2))


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
def score_char(m):
    """returns how likely m is an english text using character frequency"""
    def lnPc(c):
        if c not in printset: return min(freq.values()) - math.log(1000000)
        return freq.get(c.lower(), min(freq.values()) - math.log(1000))
    return sum(lnPc(c) for c in m)
assert score_char('Hello') > score_char('whyme') > score_char('Zzyzx')

c = from_hex(
    '1b37373331363f78151b7f2b783431333d78397828372d363c78373e783a393b3736')

def score_key(k):
    m = xor(chr(k) * len(c), c)
    print m, score_char(m)
    return score_char(xor(chr(k) * len(c), c))
key = max(range(256), key=score_key)
print xor(chr(key) * len(c), c)
