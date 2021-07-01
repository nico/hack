#!/usr/bin/env python3

"""Takes Mach-O object files and prints unwind information for all symbols.

Prints information about compact unwind and eh_frame unwind information."""

import argparse
import inspect
import re
import subprocess
import sys
import textwrap


def run(cmd):
    return subprocess.check_output(cmd).decode('utf-8')


def get_defined_symbols(path):
    """Returns sorted [(addr, 0, name)] for the file's symbols"""
    # One line is e.g. `0000000000000000 T __ZN1AD2Ev`
    nm_output = run(['nm', '-nU', path])
    matches = re.findall(r'(\S+) . (\S+)\n', nm_output)
    return [(int(addr, 16), sym) for addr, sym in matches]


def get_compact_unwind(path):
    """Returns sorted [(addr, end, enc)] for the file's compact unwind info"""
    unw_output = run(['xcrun', 'unwinddump', path])
    # One entry in the output looks like:
    #   0x000000A0:
    #     start:        0x00000010   __ZN1AD1Ev
    #     end:          0x00000020   (len=0x00000010)
    #     unwind info:  0x02020000   stack size=16, no registers saved
    #     personality:
    #     lsda:
    # `addr` is an address, `encoding` an encoding from unwind_info.h
    matches = re.findall(inspect.cleandoc(r'''\
        \S+:
          start:        0x(\S+) .*
          end:          0x(\S+) .*
          unwind info:  0x(\S+) .*
          personality:.*
          lsda:.*
        '''), unw_output)
    return [(int(addr, 16), int(end, 16), int(enc, 16))
            for addr, end, enc in matches]


def get_eh_frame(path):
    """Returns sorted [(addr, end, ops)] for the file's dwarf unwind info"""
    # dwarfdump prints a lot of output. Lines we care about look like:
    # `00000000 00000014 ffffffff CIE` or
    # `00000018 0000001c 0000001c FDE cie=0000001c pc=fffffea0...fffffea6`
    # - The 1st number is the offset in the __eh_frame section.
    # - The 2nd number is the length of the entry in the __eh_frame section.
    # - The 3rd number is ffffffff for CIEs. If it's not ffffffff this is an
    #   FDE and the FDE's CIE is found at offset (1st number) + 4 - 3rd number.
    # For FDES:
    # - `cie=...` repeats the 3rd number before https://reviews.llvm.org/D74613
    #   (currently shipped with Xcode), or it's the offset of the CIE after
    #   that change.
    # - pc=start...end is the function address covered by the FDE. Sadly,
    #   Xcode's dwarfdump doesn't have these two changes yet:
    #   1. https://reviews.llvm.org/D100328
    #   2. https://reviews.llvm.org/rG3ab0f53ef3c994
    #   This makes Xcode dwarfdump's output unusable, and you have to have an
    #   llvm-dwarfdump built after these changes on path.
    eh_output = run(['llvm-dwarfdump', '--eh-frame', path])
    matches = re.findall(r'\S+ \S+ \S+ FDE cie=\S+ pc=([^.]+)\.\.\.(\S+)\n'
                         r'(.*?)(?=\n\d)',
                         eh_output, re.DOTALL)
    return [(int(addr, 16), int(end, 16), enc)
            for addr, end, enc in matches]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', help='path to mach-o object file')
    args = parser.parse_args()

    symbols = get_defined_symbols(args.input)
    compact_unwind = get_compact_unwind(args.input)
    eh_frame = get_eh_frame(args.input)

    # Combine symbols, compact_unwind, eh_frame, sort all by address, and
    # at same address put symbol first, then compact unwind, then eh_frame.
    def decorate(l, dec): return [(t[0], dec, *t[1:]) for t in l]
    merged = sorted(decorate(symbols, '0sym') +
                    decorate(compact_unwind, 'cu') +
                    decorate(eh_frame, 'eh'))

    last_cu = None
    for entry in merged:
        if entry[1] == '0sym':
            print('%s at 0x%x' % (entry[2], entry[0]))
        elif entry[1] == 'cu':
            last_cu = entry
            print('  active CU: enc 0x%08x at 0x%x...0x%x' %
                  (entry[3], entry[0], entry[2]))
        elif entry[1] == 'eh':
            eh_inactive = \
                last_cu and entry[0] >= last_cu[0] and entry[2] <= last_cu[2]
            kind = 'EH made inactive by CU' if eh_inactive else 'active EH'
            print('  %s at 0x%x...0x%x' %
                  (kind, entry[0], entry[2]))
            if not eh_inactive:
                print('  opcodes:')
                print(textwrap.indent(entry[3], '  '))


if __name__ == '__main__':
    sys.exit(main())
