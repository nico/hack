from __future__ import print_function
from Crypto.Cipher import AES

import base64


key = 'YELLOW SUBMARINE'
c = base64.b64decode(open('7.txt').read())
m = AES.new(key, AES.MODE_ECB).decrypt(c)
print(m)
