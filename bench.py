#!/usr/bin/env python

r"""Small tool to produce input data for https://github.com/thorduri/ministat

Example:
    bench.py -o before.txt out/gn/bin/ld64.lld.darwinnew @response.txt
    # ...rebuild lld with some change...
    bench.py -o after.txt out/gn/bin/ld64.lld.darwinnew @response.txt

    ministat before.txt after.txt
"""

from __future__ import print_function
import argparse, subprocess, sys, time

parser = argparse.ArgumentParser(
             epilog=__doc__,
             formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('-n', help='number of repetitions', default=5, type=int)
parser.add_argument('--output', '-o', help='write timing output to this file')
parser.add_argument('cmd', nargs=argparse.REMAINDER, help='command to time')
args = parser.parse_args()

if args.cmd and args.cmd[0] == '--': # Make `bench.py -o foo -- cmd` work
    del args.cmd[0]
if not args.cmd:
    parser.print_help()
    sys.exit(1)

out = open(args.output, 'w') if args.output else sys.stdout

subprocess.call(args.cmd)  # Warmup
for _ in range(args.n):
    t = time.time();
    subprocess.call(args.cmd)
    print(time.time() - t, file=out)

