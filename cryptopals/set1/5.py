import fileinput
import itertools
import sys


def xor(s1, s2):
    return ''.join(chr(ord(a) ^ ord(b)) for a, b in zip(s1, s2))


key = sys.argv[1] if len(sys.argv) > 1 else 'ICE'
key = itertools.cycle(key)


for line in fileinput.input():
    xored = xor(line, key)
    sys.stdout.write(xored.encode('hex'))
