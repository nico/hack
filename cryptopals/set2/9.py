def pkcs7_pad(m, k):
    n_pad = (k - (len(m) % k)) % k
    return m + chr(n_pad) * n_pad

assert pkcs7_pad("YELLOW SUBMARINE", 20) == "YELLOW SUBMARINE\x04\x04\x04\x04"
