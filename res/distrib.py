#!/usr/bin/env python

# Builds rc for Mac, Linux, Windows.  Must run on a Mac.
# Needs a chromium checkout that was synced with target_os=['win'] to get
# the Windows toolchain. You must run
# `build/linux/sysroot_scripts/install-sysroot.py --arch amd64` once to
# get the linux toolchain.

# Doesn't run tests, so make sure to run `./test_rc.py` on all 3 platforms
# before running this script.

import json
import glob
import os
import subprocess
import sys

crsrc = '/Users/thakis/src/chrome/src'
if len(sys.argv) > 1:
  crsrc = os.path.abspath(sys.argv[1])
clangxx = crsrc + '/third_party/llvm-build/Release+Asserts/bin/clang++'
common = [
    clangxx, '-std=c++14', 'rc.cc', '-Wall', '-Wno-c++11-narrowing',
    '-O2', '-fno-rtti', '-fno-exceptions', '-DNDEBUG', ]

# Linux.
linux_sysroot = crsrc + '/build/linux/debian_sid_amd64-sysroot'
subprocess.check_call(
    common + ['-o', 'rc-linux64', '-fuse-ld=lld',
     '-target', 'x86_64-unknown-linux-gnu',
     '--sysroot', linux_sysroot,
    ])

# Mac.
mac_sysroot = subprocess.check_output(['xcrun', '-show-sdk-path']).strip()
subprocess.check_call(
    common + ['-o', 'rc-mac',
     '-mmacosx-version-min=10.9',
     '-target', 'x86_64-apple-darwin',
     '-isysroot', mac_sysroot,
    ])

# Win.
win_sysroot = glob.glob(
    crsrc + '/third_party/depot_tools/win_toolchain/vs_files/*')[0]
win_bindir = win_sysroot + '/Windows Kits/10/bin'
# This json file looks like http://codepad.org/kmfgf0UL
winenv = json.load(open(win_bindir + '/SetEnv.x64.json'))['env']
for k in ['INCLUDE', 'LIB']:
  winenv[k] = [os.path.join(*([win_sysroot] + e)) for e in winenv[k]]
# FIXME: why does this work? I thought I added -imsvc 'cause this didn't work.
win_include = ['-isystem' + i for i in winenv['INCLUDE']]
win_lib = ['-Wl,/libpath:' + i for i in winenv['LIB']]
subprocess.check_call(
    common + ['-o', 'rc-win.exe', '-fuse-ld=lld',
     '-target', 'x86_64-windows-msvc',
     '-D_CRT_SECURE_NO_WARNINGS',
     '-lShlwapi.lib',
    ] + win_include + win_lib)
