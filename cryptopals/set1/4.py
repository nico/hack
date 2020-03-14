import math
import string

def from_hex(s):
    return s.decode('hex')
    #return bytes.fromhex(s)  # Py3


def xor(s1, s2):
    return ''.join(chr(ord(a) ^ ord(b)) for a, b in zip(s1, s2))
    #return bytes(a ^ b for a, b in zip(s1, s2))  # Py3


def score_char(m):
    """returns how likely m is an english text using character frequency"""
    def Pc(c):
        freq = {
            'e':0.127, 't':0.094, 'a':0.082, 'o':0.075, 'i':0.070, 'n':0.067,
            #'s':0.063, 'h':0.061, 'r':0.060, 'd':0.043, 'l':0.040, 'u':0.028,
        }
        return freq.get(c.lower(), min(freq.values()) / 100)
    return sum([math.log(Pc(c)) for c in m])


def wordlist():
    return [s.strip() for s in open('/usr/share/dict/words').readlines()]


def score_word(m, words):
    """returns how likely m is an english text using a list of known words"""
    def Pw(w):
        if w.lower() in words: return 1.1  # Not even word frequency
        #if w.isalpha(): return 0.1
        return 0.01
    return sum([math.log(Pw(w)) for w in m.split()])


words = set(wordlist())
def score(m):
    """returns how likely m is an english text"""
    # Options: Character frequency, character ngrams, word frequency, word
    # ngrams, ...
    # This uses a list of known words.
    #return score_word(m, words)
    return score_char(m)

#ctrl_to_space = string.maketrans(
    #''.join([chr(i) for i in range(0, 31) + range(128, 256)]),
    #' ' * 31 + '?' * 128)
ctrl_to_space = string.maketrans(
    ''.join([chr(i) for i in range(0, 31)]), ' ' * 31)

candidates = []
for l in open('4.txt'):
    c = from_hex(l.rstrip())
    for i in range(256):
        m = xor(chr(i) * len(c), c)
        #candidates.append((score(m.translate(ctrl_to_space, '\'?.')), i, m))
        candidates.append((score(m.translate(ctrl_to_space)), i, m))
for c in sorted(candidates, reverse=True)[0:10]: print c

