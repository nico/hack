from __future__ import print_function

tests = [
    ('demumble hello', 'hello\n'),
    ('demumble _Z4funcPci _Z1fv', 'func(char*, int)\nf()\n'),
    ('demumble < _Z4funcPci _Z1fv', 'func(char*, int)\nf()\n'),
    ('demumble ?Fx_i@@YAHP6AHH@Z@Z', 'int __cdecl Fx_i(int (__cdecl*)(int))\n'),
    ('demumble __Znwi', 'operator new(int)\n'),  # Strip extra _ (for OS X)
    ('demumble < __Znwi', 'operator new(int)\n'),  # Also from stdin
    ('demumble -m hi _Z1fv ho _Z1gv', 'hi\nf()\nho\ng()\n'),
    ('demumble -m < hi_ho _Z1fv ho _Z1gv ?hm', 'f()\ng()\n?hm\n'),
    ('demumble -m < _Z1fv!_Z1gv', 'f()\ng()\n'),
    ('demumble -m < _Z1fv!foo_bar', 'f()\n'),
    ('demumble Pi', 'int*\n'),
    ('demumble < Pi', 'Pi\n'),
    ('demumble < ___Z10blocksNRVOv_block_invoke',
     'invocation function for block in blocksNRVO()\n'),
    ('demumble < .____Z10blocksNRVOv_block_invoke',
     '.invocation function for block in blocksNRVO()\n'),
    ('demumble -m < .____Z10blocksNRVOv_block_invoke',
     'invocation function for block in blocksNRVO()\n'),
]

import os, subprocess, sys
status = 0
for t in tests:
    cmd = t[0].split()
    # Assume that demumble is next to this script.
    cmd[0] = os.path.join(os.path.dirname(__file__) or '.', cmd[0])
    if '<' in cmd:
        p = subprocess.Popen(cmd[:cmd.index('<')], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = p.communicate(input='\n'.join(cmd[cmd.index('<') + 1:]) + '\n')[0]
    else:
        out = subprocess.check_output(cmd)
    if out != t[1]:
        print("`%s`: Expected '%s', got '%s'" % (t[0], t[1], out))
        status = 1
print("passed" if status == 0 else "failed")
sys.exit(status)
