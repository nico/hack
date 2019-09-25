#!/usr/bin/env python

"""Reads N.txt / N.meta.json produced by syncbot.sh writes processed output."""

from __future__ import print_function

import datetime
import json
import re
import sys


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
        utc = datetime.datetime.strptime(utc_str, '%Y-%m-%dT%H:%M:%SZ')
        annot_lines.append((name, utc_str, utc, m.start(), m.end()))

    durations_s = []
    for i in range(len(annot_lines) - 1):
        durations_s.append(
            (annot_lines[i + 1][2] - annot_lines[i][2]).total_seconds())

    step_outputs = []
    for i in range(len(annot_lines) - 1):
        assert log[annot_lines[i][4]] == '\n'
        step_outputs.append(log[annot_lines[i][4] + 1:annot_lines[i + 1][3]])

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
        if 'durations_s' in meta:
            assert sum(durations_s) == meta['durations_s']
    else:
        assert meta.get('exit_code', 0) != 0
        if 'durations_s' in meta:
            durations_s.append(meta['durations_s'] - sum(durations_s))

    steps = []
    assert len(annot_lines) == len(durations_s) == len(step_outputs)
    for i in range(len(annot_lines)):
        step = {
            'name': annot_lines[i][0],
            'start': annot_lines[i][1],
            'duration_s': durations_s[i],
            'output': step_outputs[i],
        }
        steps.append(step)

    parsed['steps'] = steps
    return parsed


def main():
    if len(sys.argv) != 2:
        return 1
    logname = '%s.txt' % sys.argv[1]
    metaname = '%s.meta.json' % sys.argv[1]

    with open(logname) as f:
        log = f.read()

    meta = {}
    try:
        with open(metaname) as f:
            meta = json.load(f)
    except IOError:
        # Very old builds don't have meta.json files yet.
        # FIXME: Require meta.json files soonish.
        pass

    meta = parse_output(log, meta)
    print(meta)
    print(json.dumps(meta, indent=2))


if __name__ == '__main__':
    sys.exit(main())
