from __future__ import print_function
from Crypto.Cipher import AES

import base64

# From 9.py (pad)
def pkcs7_pad(m, k):
    n_pad = (k - (len(m) % k)) % k
    return m + chr(n_pad) * n_pad

# From 2.py
def xor(s1, s2):
    return ''.join(chr(ord(a) ^ ord(b)) for a, b in zip(s1, s2))
    #return bytes(a ^ b for a, b in zip(s1, s2))  # Py3


block_len = 16

def cbc_encode(m, key):
    # From 7.py (ECB)
    m = pkcs7_pad(m, block_len)
    chunks = [m[i*block_len:(i+1)*block_len] for i in range(len(m) / block_len)]

    IV = '\0' * block_len
    aes = AES.new(key, AES.MODE_ECB)
    enc_chunks = [ aes.encrypt(IV) ]
    for c in chunks:
        enc_chunks.append(aes.encrypt(xor(c, enc_chunks[-1])))
    return ''.join(enc_chunks)

def cbc_decode(c, key):
    # From 7.py (ECB)
    chunks = [c[i*block_len:(i+1)*block_len] for i in range(len(c) / block_len)]

    aes = AES.new(key, AES.MODE_ECB)
    dec_chunks = []
    for i in range(1, len(chunks)):
        dec_chunks.append(xor(aes.decrypt(chunks[i]), chunks[i - 1]))
    return ''.join(dec_chunks)


key = 'YELLOW SUBMARINE'
def test(test_message):
    assert len(test_message) % block_len == 0
    assert cbc_decode(cbc_encode(test_message, key), key) == test_message
test('')
test("hello, what's up")
test("i don't know if you noticed, but")

print(cbc_decode(base64.b64decode(open('10.txt').read()), key))
