import base64

def b64_from_hex(s):
    return base64.b64encode(s.decode('hex'))
    #return base64.b64encode(bytes.fromhex(s))  # Py3

assert b64_from_hex(
    '49276d206b696c6c696e6720796f757220627261696e206c'
    '696b65206120706f69736f6e6f7573206d757368726f6f6d') == \
        b'SSdtIGtpbGxpbmcgeW91ciBicmFpbiBsaWtlIGEgcG9pc29ub3VzIG11c2hyb29t'
