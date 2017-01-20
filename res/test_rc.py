#!/usr/bin/env python
from __future__ import print_function
import filecmp, subprocess, sys
if sys.platform == 'win32':
  cmd = 'cl rc.cc /EHsc /wd4838 /nologo'
else:
  cmd = 'clang++ -std=c++14 -o rc rc.cc -Wall -Wno-c++11-narrowing'.split()
subprocess.check_call(cmd)

tests = [
'language',
'cursor',
'bitmap',
'icon',
'menu',
'menu_opts',
'dialog_nocontrols', 'dialog_controls',
'dialogex_nocontrols', 'dialogex_controls',
'stringtable',
'accelerators',
'rcdata', 'rcdata_inline',
'versioninfo_fixedonly', 'versioninfo', 'versioninfo_slurp',
'dlginclude',
'html',
'custom',
'stringnames',
'eval',
'literals_int',
]

for test in tests:
  print(test)
  subprocess.check_call('rc.exe' if sys.platform == 'win32' else './rc',
                        stdin=open('test/%s.rc' % test))
  assert filecmp.cmp('out.res', 'test/%s.res' % test)

print('passed')
