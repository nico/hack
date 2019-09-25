#!/usr/bin/env python

"""Reads N.txt / N.meta.json produced by syncbot.sh writes processed output."""

# Use e.g. like
# rsync -az llvm@45.33.8.238:buildlog . && llvmgnsyncbot/annotate.py buildlog

from __future__ import print_function

import datetime
import json
import os
import re
import sys


def parse_utc(s):
    return datetime.datetime.strptime(s, '%Y-%m-%dT%H:%M:%SZ')


def parse_output(log, meta):
    parsed = dict(meta)

    # Older builds don't have timestamps yet.
    # FIXME: Require timestamps.
    utc_iso_8601_re = r'(\d\d\d\d-\d\d-\d\dT\d\d:\d\d:\d\dZ)'
    annot_re = re.compile(r'^INFO:(?:' + utc_iso_8601_re + ':)root:(.*)$', re.M)

    # name, timestamp_str, timestamp_datetime, start, end
    annot_lines = []
    for m in annot_re.finditer(log):
        utc_str, name = m.groups()
        utc = parse_utc(utc_str)
        annot_lines.append((name, utc_str, utc, m.start(), m.end()))

    elapsed_s = []
    for i in range(len(annot_lines) - 1):
        elapsed_s.append(
            (annot_lines[i + 1][2] - annot_lines[i][2]).total_seconds())

    step_outputs = []
    for i in range(len(annot_lines) - 1):
        assert log[annot_lines[i][4]] == '\n'
        step_outputs.append((annot_lines[i][4] + 1, annot_lines[i + 1][3]))

    # The pending build has a final
    # 'no new commits. sleeping for 30s, then exiting.' annotation.
    if annot_lines[-1][0].startswith('no new commits.'):
        parsed['no_commits'] = True
        return parsed

    # Successful builds have a final 'done' annotation, unsuccessful builds
    # don't. Strip it, for consistency onward.
    if annot_lines[-1][0] == 'done':
        assert meta.get('exit_code', 0) == 0
        assert log[-1] == '\n'
        assert annot_lines[-1][4] + 1 == len(log)
        del annot_lines[-1]
        if 'elapsed_s' in meta:
            assert abs(sum(elapsed_s) - meta['elapsed_s']) <= 1
    else:
        assert meta.get('exit_code', 0) != 0
        if 'elapsed_s' in meta:
            elapsed_s.append(meta['elapsed_s'] - sum(elapsed_s))
        step_outputs.append((annot_lines[-1][4] + 1, len(log)))

    steps = []
    assert len(annot_lines) == len(elapsed_s) == len(step_outputs)
    for i in range(len(annot_lines)):
        step = {
            'name': annot_lines[i][0],
            'start': annot_lines[i][1],
            'elapsed_s': elapsed_s[i],
            'output': step_outputs[i],
        }
        steps.append(step)

    m = re.search(r'^Updating [0-9a-f]+\.\.([0-9a-f]+)$',
                  log[steps[0]['output'][0]:steps[0]['output'][1]], re.M)
    assert m
    parsed['git_revision'] = m.group(1)

    parsed['steps'] = steps
    return parsed


def parse_buildlog(logfile, metafile):
    with open(logfile) as f:
        log = f.read()

    meta = {}
    # Very old builds don't have meta.json files yet.
    # FIXME: Require meta.json files soonish.
    if metafile is not None:
       with open(metafile) as f:
            meta = json.load(f)

    return parse_output(log, meta)


def get_newest_build(platform, platform_logdir):
    # Filter out swap files, .DS_store files, etc.
    files = [b for b in os.listdir(platform_logdir) if not b.startswith('.')]

    # 1.txt, 1.meta.json => 1
    def get_num(f): return int(f.split('.', 1)[0])

    num_builds = max(get_num(f) for f in files)
    builds = [[None, None] for i in range(num_builds)]
    for f in files:
        builds[get_num(f) - 1][f.endswith('.meta.json')] = \
            os.path.join(platform_logdir, f)

    newest = None
    last_good = None
    infos = [None for _ in range(len(builds))]

    # The build after last_good is not always the one with the regression for
    # the current breakage. Imagine someone breaking compile and while compile
    # is broken someone breaks a test, and then compile is fixed.
    currently_failing_step = None
    first_fail_with_current_cause = None
    for i in reversed(range(len(builds))):
       log, meta = builds[i]
       # FIXME: if meta.json stored the no_commits bit directly, this wouldn't
       # have to do full log parsing. (OTOH, need to do that later anyways.)
       info = parse_buildlog(log, meta)
       if info.get('no_commits', False):
           continue

       info['build_nr'] = i + 1
       info['log_file'] = log
       info['meta_file'] = meta
       infos[i] = info

       if newest is None:
           newest = info
           if newest['exit_code'] == 0:
               break
           currently_failing_step = newest['steps'][-1]['name']
           continue
       if (first_fail_with_current_cause is None and
             info['steps'][-1]['name'] != currently_failing_step):
           first_fail_with_current_cause = infos[i + 1]
       if info['exit_code'] == 0:
           last_good = info
           break

    # FIXME: if fail, link to logfile?
    def build_str(info):
      elapsed = datetime.timedelta(seconds=info['elapsed_s'])
      start = info['steps'][0]['start']
      return 'build %d (%s, %s ago, elapsed %s)' % (
          info['build_nr'], start,
          datetime.datetime.utcnow() - parse_utc(start), str(elapsed))
    status = '%s %s, %s' % (
        platform, 'passing' if newest['exit_code'] == 0 else 'failing',
        build_str(newest))
    if newest['exit_code'] != 0:
        status += '\n    failing step: ' + newest['steps'][-1]['name']
    if last_good is not None:
        status += '\n    last good %s' % build_str(last_good)
        url = 'https://github.com/llvm/llvm-project/compare/'
        status += '\n    regression range: %s%s...%s' % (url,
            last_good['git_revision'],
            first_fail_with_current_cause['git_revision'])
    return status


def main():
    if len(sys.argv) != 2:
        return 1

    buildlog_dir = sys.argv[1]
    platforms = [d for d in os.listdir(buildlog_dir)
                 if os.path.isdir(os.path.join(buildlog_dir, d))]

    for platform in sorted(platforms):
        newest = get_newest_build(
            platform, os.path.join(buildlog_dir, platform))
        print(newest)


if __name__ == '__main__':
    sys.exit(main())
