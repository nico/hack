#!/usr/bin/env python3
# vim:set fileencoding=utf-8:

# https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Operating-System-Commands
# Relevant bits:
# ESC: '\033' ('\e' in C files)
# OSC: ESC ']'
# BEL: '\a'
# ST: ESC '\' (ie '\033\\' in python strings)
# OSC Ps ; Pt ST
# OSC Ps ; Pt BEL
#         Set Text Parameters. Both ST or BEL terminators work.
#         ST is considered preferred but BEL is one byte less and less typing,
#         so this uses BEL.
# Ps = 1 0  ⇒  Change VT100 text foreground color to Pt.
# Ps = 1 1  ⇒  Change VT100 text background color to Pt.

from math import sin
import sys
import time

def main():
    while True:
        r, g, b = [int(sin(3*time.time() + t)*127 + 128) for t in [0, 2, 4]]
        sys.stdout.write(f'\033]11;#{r:02x}{g:02x}{b:02x}\a')
        sys.stdout.flush()
        time.sleep(0.01)


if __name__ == '__main__':
    try:
        main()
    finally:
        #    Ps = 1 1 0  ⇒  Reset VT100 text foreground color.
        #    Ps = 1 1 1  ⇒  Reset VT100 text background color.
        sys.stdout.write('\033]111\a') # Reset default background color
        pass
