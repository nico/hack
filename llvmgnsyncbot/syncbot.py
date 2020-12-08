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

import argparse
import logging
import json
import os
import subprocess
import signal
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


def run(last_exit_code):
    # XXX: Check if there are local changes and warn?

    # Pull.
    logging.info('pulling...')
    try:
        old_rev = git_output(['rev-parse', 'origin/main'])
        check_git(['fetch', 'origin', 'main'])
        new_rev = git_output(['rev-parse', 'origin/main'])
    except subprocess.CalledProcessError:
        # Connectivity issues. Wait a bit and hope it goes away.
        time.sleep(5 * 60)
        raise
    if old_rev == new_rev:
        # syncbot.sh greps for this string. If you change this string, also
        # change syncbot.sh.
        logging.info('no new commits. sleeping for 30s, then exiting.')
        time.sleep(30)
        return

    check_git(['checkout', '-f', 'main'])
    check_git(['reset', '--hard', 'origin/main'])

    # Sync GN files.
    logging.info('syncing...')

    if git(['show-ref', '--verify', '--quiet', 'refs/heads/merge']) == 0:
        check_git(['branch', '-D', 'merge'])
    check_git(['checkout', '-b', 'merge'])
    check_git(['branch', '--set-upstream-to', 'origin/main'])
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

    all_tests = {
            '//clang/test:check-clang': 'check-clang',
            '//clang-tools-extra/clangd/test:check-clangd': 'check-clangd',
            '//clang-tools-extra/test:check-clang-tools': 'check-clang-tools',
            '//lld/test:check-lld': 'check-lld',
            '//llvm/test:check-llvm': 'check-llvm',
    }
    if sys.platform not in ['darwin', 'win32' ]:
        all_tests['//compiler-rt/test/hwasan:check-hwasan'] = 'check-hwasan'

    logging.info('analyze')
    if last_exit_code != 0:
        # Could instead try to get all files since last green build, but
        # need to make sure that the last green build isn't too long ago
        # with that approach.
        step_output('skipping analyze because previous build was not green')
        tests = set(all_tests.values())
    else:
        changed_files = git_output(
                ['diff', '--name-only', '%s' % old_rev]).splitlines()
        analyze_in = {
            'files': ['//' + f for f in changed_files],
            'test_targets': sorted(all_tests.keys()),
            # We don't use this, but GN insists:
            'additional_compile_targets': [],
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
        tests = set(all_tests[k] for k in analyze_out['test_targets'])

        # `gn analyze` only knows about things in GN's build graph. The GN build
        # doesn't currently list .h files (due to the CMake build not doing it),
        # so included files (.h, .def, .inc) must conservatively cause all tests
        # to run.
        #
        # Likewise, included .td files are currently only listed in depfiles,
        # so GN doesn't know about them. We could give tblgen a mode where
        # it lists included files and call that at GN time, but it'd be slow so
        # it should be opt-in (like Chromium's compute_inputs_for_analyze).
        # For now, conservatively run all tests when a .td file is touched.
        #
        # Finally, the GN build graph does currently not know that
        # llvm/utils/lit is needed to run tests, or that files in llvm/test are
        # needed for check-llvm (etc). This can be added the the .gn files by
        # using the "data" directive, but there's nothing yet that enforces the
        # data directives are complete. Running tests on swarming would enforce
        # this: https://gist.github.com/nico/ee7a6e3e77ed7224cbe47ec5e0d74997
        # For now, put this knowledge here (also unenforced):
        def forces_full_test_run(f):
            # .h/.td changes in busy directories not part of the GN build
            # shouldn't cause all tests to run.
            return (f.startswith('llvm/utils/lit/') or
                    all(not f.startswith(p) for p in [ 'mlir/', 'lldb/' ]) and
                    any(f.endswith(e) for e in [ '.h', '.def', '.inc', '.td' ]))
        if any(forces_full_test_run(f) for f in changed_files):
            step_output('running all tests due to change to blacklisted file')
            tests = set(all_tests.values())

        # Similar to llvm/utils/lit: The GN build doesn't yet know that the
        # files in the various test/ directories are inputs of tests.
        # These too should use "data", but for now hardcode this here.
        def changed_start(e):
            return any(f.startswith(e) for f in changed_files)
        if changed_start('clang/test/'): tests.add('check-clang')
        if changed_start('clang-tools-extra/test/'):
            tests.add('check-clang-tools')
        if changed_start('clang-tools-extra/clangd/test/'):
            tests.add('check-clangd')
        if changed_start('lld/test/'): tests.add('check-lld')
        if changed_start('llvm/test/'): tests.add('check-llvm')
        if (sys.platform not in ['darwin', 'win32' ] and
            changed_start('compiler-rt/test/')):
            tests.add('check-hwasan')

    logging.info('testing')
    for test in sorted(all_tests.values()):
        if test in tests:
            logging.info('test %s', test)
            subprocess.check_call(['ninja', '-C', 'out/gn', test])
        else:
            logging.info('skip %s', test)

    # All worked fine, so land changes (if any).
    if (sys.platform.startswith('linux') and
            git(['diff', '--quiet', 'main']) != 0):
        # FIXME: Maybe fetch and rebase before pushing, so that the commit
        # works if something landed while tests ran -- see
        # http://lists.llvm.org/pipermail/llvm-dev/2019-October/136266.html
        logging.info('committing changes')
        check_git(['push', 'origin', 'HEAD:main'])
        # Make origin/main forget the commit(s) with gn file changes, so that
        # they pulled on the next build. Alternatively, annotate.py could look
        # at the output of the 'committing changes' step and make the sync
        # commit of the current build, but that works less well with a
        # potential future "fetch and rebase before pushing", and it'd also
        # be different from what the mac and linux bots are doing.
        # The @{1} means "previous reflog entry", see
        # https://git-scm.com/book/en/v2/Git-Tools-Revision-Selection#_git_reflog
        check_git(['update-ref', 'refs/remotes/origin/main',
                   'refs/remotes/origin/main@{1}'])

    logging.info('done')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--last-exit', type=int, help='exit code of last build')
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO,
        format='%(levelname)s:%(asctime)s:%(name)s:%(message)s',
        datefmt='%Y-%m-%dT%H:%M:%SZ')  # ISO 8601, trailing 'Z' for UTC.
    logging.Formatter.converter = time.gmtime  # UTC.

    # XXX: loop
    run(args.last_exit)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        # https://github.com/nico/hack/blob/HEAD/sigint.sh
        signal.signal(signal.SIGINT, signal.SIG_DFL)
        os.kill(os.getpid(), signal.SIGINT)
