bits = [bin(i).count('1') for i in range(256)]
def hamming_bits(s1, s2):
    if len(s1) != len(s2): raise ValueError
    # 10% slower than the explicit loop below:
    #return sum(bits[ord(c1) ^ ord(c2)] for c1, c2 in zip(s1, s2))
    n = 0
    for i in xrange(len(s1)):
        n += bits[ord(s1[i]) ^ ord(s2[i])]
    return n

if __name__ == '__main__':
    import timeit
    print(timeit.timeit("hamming_bits('a' * 20, 'b' * 20)",
          setup="from __main__ import hamming_bits"))
