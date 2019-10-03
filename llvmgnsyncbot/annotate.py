#!/usr/bin/env python

"""Reads N.txt / N.meta.json produced by syncbot.sh writes processed output."""

# Use e.g. like
#     rsync -az llvm@45.33.8.238:buildlog .
#     llvmgnsyncbot/annotate.py buildlog html

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
            'elapsed_s': elapsed_s[i],
            'output': step_outputs[i],
        }
        steps.append(step)

    parsed = dict(meta)

    pull_output = log[steps[0]['output'][0]:steps[0]['output'][1]]
    m = re.search(r'^Updating ([0-9a-f]+)\.\.([0-9a-f]+)$', pull_output, re.M)
    assert m
    parsed['prev_git_revision'] = m.group(1)
    parsed['git_revision'] = m.group(2)
    m = re.search(r"^Your branch is behind 'origin/master' by (\d+) commit",
                  pull_output, re.M)
    assert m
    parsed['num_commits'] = int(m.group(1))

    parsed['start_utc'] = annot_lines[0][1]
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
        start = info['start_utc']
        log = '%s/%d/summary.html' % (build_list.platform, info['build_nr'])
        return 'build <a href="%s">%d</a> (<time datetime="%s">%s</time>, elapsed %s)' % (
            log, info['build_nr'], start, start, str(elapsed))
    status = '<a href="%s">%s</a> %s, %s' % (
        build_list.platform + '/summary.html',
        build_list.platform,
        'passing' if newest['exit_code'] == 0 else 'failing',
        build_str(newest))
    if newest['exit_code'] != 0:
        status += '\n    failing step: <a href="%s/%d/%s">%s</a>' % (
            build_list.platform,
            newest['build_nr'],
            'step_%d.txt' % len(newest['steps']),
            newest['steps'][-1]['name'],
            )
    if last_good is not None:
        status += '\n    last good %s' % build_str(last_good)
        revs = '%s...%s' % (
            last_good['git_revision'],
            first_fail_with_current_cause['git_revision'],
            )
        url = 'https://github.com/llvm/llvm-project/compare/' + revs
        status += '\n    regression range: <a href="%s">%s</a>' % (url, revs)

    status += '\n    <span class="pending" hidden>%s</span>' % newest['git_revision']
    return status


def platform_summary(build_list):
    text = []
    num_pass = 0
    sum_pass_elapsed_s = 0
    sum_num_commits = 0
    for i in reversed(range(build_list.num_builds())):
       info = build_list.get_build_info(i)
       start = info['start_utc']
       did_pass = info['exit_code'] == 0
       t = '<a href="%s">%5d</a> %s %s' % (
                          '%d/summary.html' % info['build_nr'],
                          info['build_nr'],
                          'pass' if did_pass else 'fail',
                          start,
                         )
       # XXX duration (as graph), only for successful builds
       if did_pass:
           num_pass += 1
           sum_pass_elapsed_s += info['elapsed_s']
       sum_num_commits += info['num_commits']
       text.append(t)

    # Include spaces for "next " on build summary pages, for fast click-through.
    summary = '     <a href="../summary.html">up</a>\n\n'

    # XXX:
    # failing step histogram
    # 1 build every N time units / day
    summary += '%d/%d (%.1f%%) builds pass\n' % (
        num_pass,
        build_list.num_builds(),
        100.0 * num_pass / build_list.num_builds(),
        )
    avg_seconds = round(sum_pass_elapsed_s / float(num_pass))
    summary += 'passing builds take %s on average\n' % (
        str(datetime.timedelta(seconds=avg_seconds)),
        )
    summary += '%.2f commits per build on average\n' % (
        sum_num_commits / float(build_list.num_builds()),
        )

    return summary + '\n' + '\n'.join(text)


def build_details(info, has_next):
    text = []
    for j, step in enumerate(info['steps']):
       elapsed = datetime.timedelta(seconds=step['elapsed_s'])
       t = '        %s <a href="%s">%s</a>' % (
           str(elapsed),
           'step_%d.txt' % (j + 1),
           step['name'],
           )
       text.append(t)

    elapsed = datetime.timedelta(seconds=info['elapsed_s'])
    did_pass = info['exit_code'] == 0

    start = info['start_utc']
    header = '%s in %s, started <time datetime="%s">%s</time>\n' % (
        'pass' if did_pass else 'fail',
        str(elapsed),
        start,
        start,
        )

    footer = 'at rev <a href="%s">%s</a>\n' % (
        'https://github.com/llvm/llvm-project/tree/' + info['git_revision'],
        info['git_revision'],
        )

    revs = '%s...%s' % (info['prev_git_revision'], info['git_revision'])
    footer += 'contains <a href="%s">%d commit%s</a>\n' % (
        'https://github.com/llvm/llvm-project/compare/' + revs,
        info['num_commits'],
        '' if info['num_commits'] == 1 else 's',
        )
    footer += '\n<a href="log.txt">full log</a>\n'

    nextprev = ''
    if has_next:
        nextprev += '<a href="%s">next</a> ' % (
            '../%d/summary.html' % (info['build_nr'] + 1))
    else:
        nextprev += '     '
    nextprev += '<a href="../summary.html">up</a> '
    if info['build_nr'] > 1:
        nextprev += '<a href="%s">prev</a>' % (
            '../%d/summary.html' % (info['build_nr'] - 1))
    nextprev += '\n'

    # XXX include link to last green build, first build with same failure (?)
    return nextprev + '\n' + header + '\n' + '\n'.join(text) + '\n\n' + footer


def main():
    if len(sys.argv) != 3:
        return 1

    buildlog_dir = sys.argv[1]
    platforms = sorted([d for d in os.listdir(buildlog_dir)
                        if os.path.isdir(os.path.join(buildlog_dir, d))])

    html_dir = sys.argv[2]
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
    fetch = '''\
<script>
let url = "https://api.github.com/repos/llvm/llvm-project/git/refs/heads/master";
fetch(url).then(response => response.json()).then(json => {
  Array.from(document.getElementsByClassName('pending')).forEach(elt => {
    let trunkRev = json.object.sha;
    let curRev = elt.innerText.trim();
    if (!trunkRev.startsWith(curRev)) {
      elt.innerHTML = `<a href="https://github.com/llvm/llvm-project/compare/${curRev}...${trunkRev}">pending ${curRev.substring(0,8)}...${trunkRev.substring(0,8)}</a>`;
    } else {
      elt.innerText = 'caught up';
    }
    elt.hidden = false;
  });
});
</script>
'''
    with open(os.path.join(html_dir, 'summary.html'), 'w') as f:
        print(template % text + fetch, file=f)

    # Generate per-platform summary page, and pages for every build.
    for build_list in build_lists:
        platform_dir = os.path.join(html_dir, build_list.platform)
        with open(os.path.join(platform_dir, 'summary.html'), 'w') as f:
            f.write(template % platform_summary(build_list))

        for i in reversed(range(build_list.num_builds())):
            info = build_list.get_build_info(i)
            build_dir = os.path.join(platform_dir, '%d' % info['build_nr'])
            if os.path.isdir(build_dir):
                # Rewrite the previous last build to give it a "next" link.
                if i != build_list.num_builds() - 2:
                    continue
            else:
                os.mkdir(build_dir)
            build_html = os.path.join(build_dir, 'summary.html')
            with open(build_html, 'w') as f:
                has_next = i + 1 != build_list.num_builds()
                f.write(template % build_details(info, has_next))
            with open(info['log_file']) as f:
                log = f.read()
            with open(os.path.join(build_dir, 'log.txt'), 'w') as f:
                f.write(log)
            for j, step in enumerate(info['steps']):
                step_file = os.path.join(build_dir, 'step_%d.txt' % (j + 1))
                with open(step_file, 'w') as f:
                    f.write(log[step['output'][0]:step['output'][1]])


if __name__ == '__main__':
    sys.exit(main())
