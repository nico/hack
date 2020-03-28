#!/usr/bin/env python
from __future__ import print_function

import os
import signal
import time

def main():
    time.sleep(10)

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        # Alternatively, could use signal.signal(signal.SIGINT, handler).
        #time.sleep(1)
        print('sigint python')
        # See https://www.cons.org/cracauer/sigint.html
        signal.signal(signal.SIGINT, signal.SIG_DFL)
        os.kill(os.getpid(), signal.SIGINT)
