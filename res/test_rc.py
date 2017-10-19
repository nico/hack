#!/usr/bin/env python
from __future__ import print_function
import filecmp, subprocess, sys
if sys.platform == 'win32':
  cmd = 'cl rc.cc /EHsc /wd4838 /nologo shlwapi.lib'
else:
  cmd = 'clang++ -std=c++14 -o rc rc.cc -Wall -Wno-c++11-narrowing'.split()
subprocess.check_call(cmd)

# General tests.
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
'stringtable_by_language',
'accelerators',
'rcdata', 'rcdata_inline',
'versioninfo_fixedonly', 'versioninfo', 'versioninfo_slurp',
'dlginclude',
'html', 'html_inline',
'custom', 'custom_inline',
'stringnames',
'eval',
'literals_int',
'literals_string',
'unicode_simple_utf16le_bom', 'unicode_simple_utf16le_nobom',
'unicode_utf16le_bom_custom',
'unicode_utf16le_bom_dialog',
'unicode_utf16le_bom_menu',
'unicode_utf16le_bom_stringtable',
'unicode_utf16le_bom_versioninfo',
]

RC = 'rc.exe' if sys.platform == 'win32' else './rc'
for test in tests:
  print(test)
  subprocess.check_call(RC, stdin=open('test/%s.rc' % test))
  assert filecmp.cmp('out.res', 'test/%s.res' % test)

# Directory search order tests.
import os, tempfile
RCDIR = os.path.abspath(os.path.dirname(__file__))
TESTDIR = os.path.join(RCDIR, 'test')
os.chdir(tempfile.gettempdir())
with open('cwdfile.txt', 'wb') as f:
  f.write('never read')
flags = [
  '/Idir1',
  '/Idir2',
  '/Idir1 /Idir2',
  '/Idir2 /Idir1',
  '/Idir2/dir1',
]
for flag in flags:
  print('dirsearch ' + flag)
  relflag = ' '.join(['/I' + os.path.join(os.path.relpath(TESTDIR, '.'), f[2:])
                   for f in flag.split()])
  cmd = '%s %s %s %s' % (
      os.path.join(RCDIR, RC), relflag, '/cd' + TESTDIR,
      '/fo%s/out.res' % RCDIR)
  if sys.platform != 'win32':
    cmd = cmd.split()
  subprocess.check_call(cmd, stdin=open(os.path.join(TESTDIR, 'dirsearch.rc')))
  suffix = flag.replace('/', '').replace(' ', '.')
  # To rebase:
  # rc /Idir1 /foc:\src\hack\res\test\dirsearchIdir1.res c:\src\hack\res\test\dirsearch.rc
  assert filecmp.cmp(RCDIR + '/out.res', TESTDIR + '/dirsearch%s.res' % suffix)
# Test references to absolute paths in .rc files.
print('abspath')
with open('abs.rc', 'wb') as f:
  cwdfile_path = os.path.join(os.path.abspath(os.getcwd()), 'cwdfile.txt')
  f.write('1 RCDATA "%s"' % cwdfile_path.replace('\\', '\\\\'))
cmd = '%s /cd. %s' % (os.path.join(RCDIR, RC), '/fo%s/out.res' % RCDIR)
if sys.platform != 'win32':
  cmd = cmd.split()
# Just succeeding is enough for this test.
subprocess.check_call(cmd, stdin=open('abs.rc'))
#os.remove('cwdfile.txt')
#os.remove('abs.rc')

print('passed')
