#!/usr/bin/env python3

# A tiny script that prints numbers in binary and hex,
# and also as bit_cast<i32>(static_cast<u32>(number)).
# Based on https://bernsteinbear.com/blog/simple-python-repl/

import code
import readline
import struct


class Repl(code.InteractiveConsole):
    def runsource(self, source, filename="<input>", symbol="single"):
        try:
            num = int(source, 0) # `, 0` infers base from 0x prefix
        except Exception as e:
            print(f'error: {e}')
            return False
        length = len(format(num, '#b'))
        print(f'bin: {num: >#{length}b}')
        print(f'dec: {num: >#{length}}')
        print(f'hex: {num: >#{length}x}')
        i32 = struct.unpack("i", struct.pack("I", num))[0]
        print(f'i32: {i32: >#{length}}')
        print(f'x32: {i32: >#{length}x}')


repl = Repl()
repl.interact(banner='integer conversion repl', exitmsg='')
