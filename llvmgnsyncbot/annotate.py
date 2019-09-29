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
    utc_iso_8601_re = r'\d\d\d\d-\d\d-\d\dT\d\d:\d\d:\d\dZ'
    annot_re = re.compile(r'^INFO:(' + utc_iso_8601_re + '):root:(.*)$', re.M)

    # name, timestamp_str, timestamp_datetime, start, end
    annot_lines = []
    for m in annot_re.finditer(log):
        utc_str, name = m.groups()
        # FIXME: Require timestamps.
        if not utc_str: utc_str = '1970-01-01T00:00:00Z'
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

    # Successful builds have a final 'done' annotation, unsuccessful builds
    # don't. Strip it, for consistency onward.
    if annot_lines[-1][0] == 'done':
        assert meta.get('exit_code', 0) == 0
        assert log[-1] == '\n'
        assert annot_lines[-1][4] + 1 == len(log)
        del annot_lines[-1]
        assert abs(sum(elapsed_s) - meta['elapsed_s']) <= 1
    else:
        assert meta.get('exit_code', 0) != 0
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

    parsed = dict(meta)
    m = re.search(r'^Updating [0-9a-f]+\.\.([0-9a-f]+)$',
                  log[steps[0]['output'][0]:steps[0]['output'][1]], re.M)
    assert m
    parsed['git_revision'] = m.group(1)
    parsed['steps'] = steps
    return parsed


def parse_buildlog(logfile, metafile):
    with open(logfile) as f:
        log = f.read()
    with open(metafile) as f:
        meta = json.load(f)
    return parse_output(log, meta)


class BuildList(object):
    def __init__(self, platform, buildlog_dir):
        self.platform = platform

        # Filter out swap files, .DS_store files, etc.
        platform_logdir = os.path.join(buildlog_dir, platform)
        files = [b for b in os.listdir(platform_logdir)
                      if not b.startswith('.')]
        # 1.txt, 1.meta.json => 1
        def get_num(f): return int(f.split('.', 1)[0])

        num_builds = max(get_num(f) for f in files)
        self.builds = [[None, None] for i in range(num_builds)]
        for f in files:
            self.builds[get_num(f) - 1][f.endswith('.meta.json')] = \
                os.path.join(platform_logdir, f)
        self.infos = [None for _ in range(self.num_builds())]

    def num_builds(self):
        return len(self.builds)

    def get_build_info(self, i):
        if self.infos[i] is None:
            log, meta = self.builds[i]
            info = parse_buildlog(log, meta)
            info['build_nr'] = i + 1
            info['log_file'] = log
            info['meta_file'] = meta
            self.infos[i] = info
        return self.infos[i]


def get_newest_build(build_list):
    newest = None
    last_good = None

    # The build after last_good is not always the one with the regression for
    # the current breakage. Imagine someone breaking compile and while compile
    # is broken someone breaks a test, and then compile is fixed.
    currently_failing_step = None
    first_fail_with_current_cause = None
    for i in reversed(range(build_list.num_builds())):
       info = build_list.get_build_info(i)
       if newest is None:
           newest = info
           if newest['exit_code'] == 0:
               break
           currently_failing_step = newest['steps'][-1]['name']
           continue
       if (first_fail_with_current_cause is None and
             (info['exit_code'] == 0 or
              info['steps'][-1]['name'] != currently_failing_step)):
           first_fail_with_current_cause = build_list.get_build_info(i + 1)
       if info['exit_code'] == 0:
           last_good = info
           break

    # FIXME: if fail, link to logfile?
    def build_str(info):
      elapsed = datetime.timedelta(seconds=info['elapsed_s'])
      start = info['steps'][0]['start']
      log = '%s/%s.txt' % (build_list.platform, info['build_nr'])
      return 'build <a href="%s">%d</a> (<time datetime="%s">%s</time>, elapsed %s)' % (
          log, info['build_nr'], start, start, str(elapsed))
    status = '<a href="%s">%s</a> %s, %s' % (
        build_list.platform + '/summary.html',
        build_list.platform,
        'passing' if newest['exit_code'] == 0 else 'failing',
        build_str(newest))
    if newest['exit_code'] != 0:
        status += '\n    failing step: ' + newest['steps'][-1]['name']
    if last_good is not None:
        status += '\n    last good %s' % build_str(last_good)
        revs = '%s...%s' % (last_good['git_revision'],
                                first_fail_with_current_cause['git_revision'])
        url = 'https://github.com/llvm/llvm-project/compare/' + revs
        status += '\n    regression range: <a href="%s">%s</a>' % (url, revs)
    return status


def platform_summary(build_list):
    text = []
    num_pass = 0
    sum_pass_elapsed_s = 0
    for i in reversed(range(build_list.num_builds())):
       info = build_list.get_build_info(i)
       start = info['steps'][0]['start']
       did_pass = info['exit_code'] == 0
       t = '%5d %s %s' % (info['build_nr'],
                          'pass' if did_pass else 'fail',
                          start,
                         )
       # XXX duration (as graph), only for successful builds
       if did_pass:
           num_pass += 1
           sum_pass_elapsed_s += info['elapsed_s']
       text.append(t)

    # XXX
    # average duration
    # failing step histogram
    # 1 build every N time units / day
    # num revisions per build
    summary = '%d/%d (%.1f%%) builds passed\n\n' % (
        num_pass,
        build_list.num_builds(),
        100.0 * num_pass / build_list.num_builds())

    return summary + '\n'.join(text)


def main():
    if len(sys.argv) != 2:
        return 1

    buildlog_dir = sys.argv[1]
    platforms = sorted([d for d in os.listdir(buildlog_dir)
                        if os.path.isdir(os.path.join(buildlog_dir, d))])

    html_dir = os.path.join(buildlog_dir, '..', 'html')
    for platform in platforms:
        platform_dir = os.path.join(html_dir, platform)
        if not os.path.isdir(platform_dir):
            os.makedirs(platform_dir)

    build_lists = [BuildList(platform, buildlog_dir) for platform in platforms]

    # Generate global summary page.
    text = '\n'.join(get_newest_build(bl) for bl in build_lists)
    template = '''\
<!doctype html>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LLVM GN buildbot</title>
<pre>%s</pre>
<script>
Array.from(document.getElementsByTagName('time')).forEach(elt => {
  elt.innerText = new Date(elt.getAttribute('datetime')).toLocaleString();
});
</script>
'''
    with open(os.path.join(html_dir, 'summary.html'), 'w') as f:
        print(template % text, file=f)

    # Generate per-platform summary page.
    for build_list in build_lists:
        platform_dir = os.path.join(html_dir, build_list.platform)
        with open(os.path.join(platform_dir, 'summary.html'), 'w') as f:
            f.write(template % platform_summary(build_list))


if __name__ == '__main__':
    sys.exit(main())
