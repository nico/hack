from __future__ import print_function

import base64
import collections


key = 'YELLOW SUBMARINE'
counters = []
for i, l in enumerate(open('8.txt')):
    l = base64.b64decode(l.rstrip())
    chunks = [l[i*16:(i+1)*16] for i in range(len(l) / 16)]

    c = collections.Counter(chunks)
    counters.append(c)
    if len(c) < 15:
        print(i, chunks, c)

#print(min(len(c) for c in counters))
