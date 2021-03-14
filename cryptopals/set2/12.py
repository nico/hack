from __future__ import print_function
from Crypto.Cipher import AES

import base64
import collections
import random
import os

block_len = 16

def pkcs7_pad(m, k):
    n_pad = (k - (len(m) % k)) % k
    return m + chr(n_pad) * n_pad

def ecb_encode(m, key):
    m = pkcs7_pad(m, block_len)
    return AES.new(key, AES.MODE_ECB).encrypt(m)


SECRET = '''\
Um9sbGluJyBpbiBteSA1LjAKV2l0aCBteSByYWctdG9wIGRvd24gc28gbXkg
aGFpciBjYW4gYmxvdwpUaGUgZ2lybGllcyBvbiBzdGFuZGJ5IHdhdmluZyBq
dXN0IHRvIHNheSBoaQpEaWQgeW91IHN0b3A/IE5vLCBJIGp1c3QgZHJvdmUg
YnkK'''

def encryption_oracle(m, key):
    m = m + base64.b64decode(SECRET)
    return ecb_encode(m, key)


def random_aes_key():
    return os.urandom(block_len)

key = random_aes_key()

# 1

def find_block_size(enc):
    # If encryption_oracle() would randpad(), take min over a few runs instead.
    s = len(enc('', key))
    for i in range(1, 64):
        ns = len(enc('a' * i, key))
        if ns > s: return ns - s
    raise Error('too long')

assert find_block_size(encryption_oracle) == block_len

# 2

def uses_ecb(enc):
    def count_chunks(e):
        chunks = [e[i*block_len:(i+1)*block_len]
                  for i in range(len(e) / block_len)]
        return len(collections.Counter(chunks))
    n = count_chunks(enc('', key))
    n2 = count_chunks(enc('a' * (8 * block_len), key))
    return n2 - n <= 2

assert uses_ecb(encryption_oracle)

# 3

one_short = 'a' * (block_len - 1)

# 4

d = {}
for i in range(256):
    m = one_short + chr(i)
    d[encryption_oracle(m, key)[:block_len]] = chr(i)

# 5

decrypted = d[encryption_oracle(one_short, key)[:block_len]]
print('first byte:', decrypted)

# 6

num_enc = len(encryption_oracle('', key))

for i in range(1, num_enc):
    assert i == len(decrypted)
    block_index = i / block_len
    in_block_index = i % block_len

    prefix = b'a' * (block_len - 1 - in_block_index)

    d = {}
    for j in range(256):
        m = prefix + decrypted + chr(j)
        d[encryption_oracle(m, key)[
            block_index * block_len:(block_index + 1)*block_len]] = chr(j)

    # The pkcs7_pad padding bytes are length-dependent and we pass
    # a string with a different length here and when computing the lookup table.
    # So we'll get a KeyError once we get to the final padding.
    try:
        decrypted += d[encryption_oracle(prefix, key)[
                         block_index * block_len:(block_index + 1)*block_len]]
    except KeyError:
        break

print('recovered:', decrypted)
