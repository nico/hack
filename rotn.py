#!/usr/bin/env python3
# Takes n from argv, then rot-n encodes stdin.
import sys
n = int(sys.argv[1])
source = sys.stdin if len(sys.argv) <= 2 else [s + '\n' for s in sys.argv[2:]]
for line in source:
    for c in line:
        if c.isalpha():
            c = chr(((ord(c) & 0x1f) - 1 + n) % 26 + (ord(c) & 0xe0) + 1)
        sys.stdout.write(c)
