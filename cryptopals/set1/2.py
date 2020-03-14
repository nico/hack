def from_hex(s):
    return s.decode('hex')
    #return bytes.fromhex(s)  # Py3


def xor(s1, s2):
    return ''.join(chr(ord(a) ^ ord(b)) for a, b in zip(s1, s2))
    #return bytes(a ^ b for a, b in zip(s1, s2))  # Py3


assert xor(from_hex('1c0111001f010100061a024b53535009181c'),
           from_hex('686974207468652062756c6c277320657965')) == \
       from_hex('746865206b696420646f6e277420706c6179')
               
