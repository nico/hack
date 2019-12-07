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
import json
import os
import subprocess
import sys
import time


def git(args):
    return subprocess.call(['git'] + args)

def check_git(args):
    return subprocess.check_call(['git'] + args)

def git_output(args):
    return subprocess.check_output(['git'] + args).rstrip()


def step_output(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def run():
    # XXX: Check if there are local changes and warn?

    # Pull.
    logging.info('pulling...')
    try:
        old_rev = git_output(['rev-parse', 'origin/master'])
        check_git(['fetch', 'origin', 'master'])
        new_rev = git_output(['rev-parse', 'origin/master'])
    except subprocess.CalledProcessError:
        # Connectivity issues. Wait a bit and hope it goes away.
        time.sleep(5 * 60)
        raise
    if old_rev == new_rev:
        logging.info('no new commits. sleeping for 30s, then exiting.')
        time.sleep(30)
        return

    check_git(['checkout', '-f', 'master'])
    check_git(['reset', '--hard', 'origin/master'])

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

    # Build.
    logging.info('restart goma')
    if sys.platform == 'win32':
        goma_ctl = r'c:\src\goma\goma-win64\goma_ctl.py'
    else:
        goma_ctl = os.path.expanduser('~/goma/goma_ctl.py')
    subprocess.check_call([sys.executable, goma_ctl, 'restart'])

    logging.info('building')
    j = '-j1000'
    if sys.platform == 'darwin':
        # `ninja: fatal: pipe: Too many open files` with default ulimit else.
        j = '-j200'
    subprocess.check_call(['ninja', '-C', 'out/gn', j])

    # Test.

    tests = {
            '//clang/test:check-clang': 'check-clang',
            '//clang-tools-extra/clangd/test:check-clangd': 'check-clangd',
            '//clang-tools-extra/test:check-clang-tools': 'check-clang-tools',
            '//lld/test:check-lld': 'check-lld',
            '//llvm/test:check-llvm': 'check-llvm',
    }
    if sys.platform not in ['darwin', 'win32' ]:
        tests['//compiler-rt/test/hwasan:check-hwasan'] = 'check-hwasan'

    logging.info('analyze')
    changed_files = git_output(
            ['diff', '--name-only', '%s..%s' % (old_rev, new_rev)]).splitlines()
    analyze_in = {
        'files': ['//' + f for f in changed_files],
        'test_targets': sorted(tests.keys()),
        'additional_compile_targets': [],  # We don't use this, but GN insists.
    }
    with open('analyze_in.json', 'wb') as f:
        json.dump(analyze_in, f)
    subprocess.check_call([
            sys.executable,
            'llvm/utils/gn/gn.py',
            'analyze',
            'out/gn',
            'analyze_in.json',
            'analyze_out.json'])
    with open('analyze_out.json', 'rb') as f:
        analyze_out = json.load(f)
    step_output('gn analyze output:\n' + json.dumps(analyze_out, indent=2))
    step_output('gn analyze input:\n' + json.dumps(analyze_in, indent=2))
    # FIXME: Add blacklist for .h/.def/.inc, */test/, llvm/utils/lit
    # FIXME: Actually use analyze output

    logging.info('testing')
    for test in sorted(tests.values()):
        logging.info('test %s', test)
        subprocess.check_call(['ninja', '-C', 'out/gn', test])

    # All worked fine, so land changes (if any).
    if (sys.platform.startswith('linux') and
            git(['diff', '--quiet', 'master']) != 0):
        # FIXME: Maybe fetch and rebase before pushing, so that the commit
        # works if something landed while tests ran -- see
        # http://lists.llvm.org/pipermail/llvm-dev/2019-October/136266.html
        # If I do that, I need to change the triggering logic, else the next
        # build won't build the commits fetched here.
        logging.info('committing changes')
        check_git(['push', 'origin', 'HEAD:master'])

    logging.info('done')


def main():
    logging.basicConfig(level=logging.INFO,
        format='%(levelname)s:%(asctime)s:%(name)s:%(message)s',
        datefmt='%Y-%m-%dT%H:%M:%SZ')  # ISO 8601, trailing 'Z' for UTC.
    logging.Formatter.converter = time.gmtime  # UTC.


    # XXX: loop
    run()


if __name__ == '__main__':
    sys.exit(main())
