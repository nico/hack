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

def xor(s1, s2):
    return ''.join(chr(ord(a) ^ ord(b)) for a, b in zip(s1, s2))
    #return bytes(a ^ b for a, b in zip(s1, s2))  # Py3

def ecb_encode(m, key):
    m = pkcs7_pad(m, block_len)
    return AES.new(key, AES.MODE_ECB).encrypt(m)

def cbc_encode(m, key, IV):
    m = pkcs7_pad(m, block_len)
    chunks = [m[i*block_len:(i+1)*block_len] for i in range(len(m) / block_len)]

    aes = AES.new(key, AES.MODE_ECB)
    enc_chunks = [ aes.encrypt(IV) ]
    for c in chunks:
        enc_chunks.append(aes.encrypt(xor(c, enc_chunks[-1])))
    return ''.join(enc_chunks)


def random_aes_key():
    return os.urandom(block_len)


def encryption_oracle(m, key=None):
    if not key: key = random_aes_key()
    def randpad(): return os.urandom(random.randint(5, 10))
    m = randpad() + m + randpad()
    if ord(os.urandom(1)) & 1 == 0:
        print('pick:    ecb')
        return ecb_encode(m, key)
    print('pick:    cbc')
    return cbc_encode(m, key, os.urandom(block_len))


def predict():
    N = 5
    m = 'a' * (N * block_len)
    e = encryption_oracle(m)  # Adds 1-2 blocks

    chunks = [e[i*block_len:(i+1)*block_len] for i in range(len(e) / block_len)]
    c = collections.Counter(chunks)
    if len(c) <= 4: # 1 front pad, 1 middle bits, up to 2 back pad
        print('predict: ecb')
    elif len(c) > N:
        print('predict: cbc')
    else:
        print('predict: ??', len(c))


for i in range(10):
    predict()
