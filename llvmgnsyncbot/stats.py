#!/usr/bin/env python

"""Reads the buildlog_cache dir created by annotate.py to produce some stats."""

# Use e.g. like
#     rsync -az llvm@45.33.8.238:buildlog .
#     llvmgnsyncbot/annotate.py buildlog html
#     llvmgnsyncbot/stats.py buildlog_cache/linux

from __future__ import print_function

import json
import os
import sys

import matplotlib.pyplot as plt
import pandas as pd


def json_dir_to_dataframe(json_dir):
    files = sorted(os.listdir(json_dir), key=lambda f:int(f.split('.')[0]))
    def load(name):
        with open(os.path.join(json_dir, name)) as f:
            d = json.load(f)
            del d['steps'], d['log_file']
            d.pop('git_revision', None)
            d.pop('prev_git_revision', None)
            return d
    return pd.io.json.json_normalize([load(f) for f in files])


# Register datetime converter for matplotlib plotting.
pd.plotting.register_matplotlib_converters()

max_y = 0
min_x = pd.to_datetime(pd.Timestamp.max, utc=True)
max_x = pd.to_datetime(pd.Timestamp.min, utc=True)

for platform in ('linux', 'mac', 'win'):
    df = json_dir_to_dataframe(os.path.join(sys.argv[1], platform))

    # Only keep successful builds.
    df = df[df.exit_code == 0]

    # Builds before 137 had lots of security software enabled which caused
    # cycle times of 45+ mins. That blows out the scale, so omit them.
    if platform == 'win':
        df = df[df.build_nr >= 137]

    df.start_utc = pd.to_datetime(df.start_utc, utc=True)
    #df.start_utc = df.start_utc.dt.tz_convert('US/Eastern')
    df = df.set_index('start_utc')

    max_x = max(max_x, df.index.max())
    min_x = min(min_x, df.index.min())

    df['elapsed_m'] = df.elapsed_s / 60.
    max_y = max(max_y, df.elapsed_m.max())

    plt.scatter(df.index, df.elapsed_m, alpha=0.1, label=platform)
    plt.xlabel('date')
    plt.ylabel('elapsed minutes')

    #plt.plot(df.index, df.elapsed_m.rolling('2d').mean(),
             #label='%s (avg)' % platform)

plt.legend()
plt.xlim(min_x, max_x)
plt.ylim(0, max_y)
plt.show()
