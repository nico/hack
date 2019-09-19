#!/usr/bin/env python

"""A script that tries to auto-merge CMake changes.

This script runs the following in a loop:
1. Pull the latest LLVM revision. If there are no new changes,
   sleep for one minute and go to 1.
1a. XXX Auto-update bot script once checked in?
2. Run llvm/utils/gn/build/sync_source_lists_from_cmake.py --write
3. Run `ninja check-clang check-clangd check-clang-tools check-hwasan check-lld check-llvm`
4. If 3 succeeds, commit changes written by 2 (if any)

It is meant to be run on a bot in a loop.
The bot should have `llvm_targets_to_build = "all"` in its args.gn.
Also run `git config core.trustctime false` -- else ctime change caused by the
hardlinking LLVM's gn build does for copy steps causes git to change the mtime
for random file copy inputs, making incremental builds do much more work than
necessary.

Use through syncbot.sh wrapper script:
./syncbot.sh
"""

from __future__ import print_function

import logging
import os
import subprocess
import sys
import time


def git(args):
    return subprocess.call(['git'] + args)

def check_git(args):
    return subprocess.check_call(['git'] + args)

def git_output(args):
    return subprocess.check_output(['git'] + args)


def run():
    # XXX: Check if there are local changes and warn?

    # Pull.
    logging.info('pulling...')
    check_git(['fetch', 'origin', 'master'])

    # Check if it's the same ref
    # as HEAD, in which case nothing happened since the last time
    # the script ran.
    local_ref = git_output(['rev-parse', 'master'])
    remote_ref = git_output(['rev-parse', 'origin/master'])

    # Need to compare master and origin/master instead of merge and
    # origin/master, else we'd busy-loop if sync_source_lists_from_cmake
    # creates commits on the merge branch that don't build cleanly
    # (e.g. when trunk is broken for unrelated reasons).
    if local_ref == remote_ref:
        logging.info('no new commits. sleeping for 30s, then exiting.')
        time.sleep(30)
        return

    check_git(['checkout', '-f', 'master'])
    check_git(['merge', '--ff-only', 'origin/master'])

    # Sync GN files.
    logging.info('syncing...')

    if git(['show-ref', '--verify', '--quiet', 'refs/heads/merge']) == 0:
        check_git(['branch', '-D', 'merge'])
    check_git(['checkout', '-b', 'merge'])
    check_git(['branch', '--set-upstream-to', 'origin/master'])
    subprocess.check_call([
            sys.executable,
            'llvm/utils/gn/build/sync_source_lists_from_cmake.py',
            '--write'])

    # Build/test.
    logging.info('building')
    subprocess.check_call([os.path.expanduser('~/goma/goma_ctl.py'), 'restart'])
    j = '-j1000'
    if sys.platform == 'darwin':
        # `ninja: fatal: pipe: Too many open files` with default ulimit else.
        j = '-j200'
    subprocess.check_call(['ninja', '-C', 'out/gn', j])

    logging.info('testing')
    tests = [
            'check-clang',
            'check-clangd',
            'check-clang-tools',
            'check-lld',
            'check-llvm',
    ]
    if sys.platform != 'darwin':
        tests += [ 'check-hwasan' ]
    for test in tests:
      logging.info('test %s', test)
      subprocess.check_call(['ninja', '-C', 'out/gn', test])

    # All worked fine, so land changes (if any).
    logging.info('committing changes')
    subprocess.check_call(['llvm/utils/git-svn/git-llvm', 'push', '-f'])


def main():
    logging.basicConfig(level=logging.INFO)

    # XXX: loop
    run()

if __name__ == '__main__':
    sys.exit(main())
