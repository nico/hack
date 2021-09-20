#!/usr/bin/env python3

"""Shows per-step stats for a single platform."""

# Use e.g. like
#     rsync -az llvm@45.33.8.238:buildlog .
#     llvmgnsyncbot/annotate.py buildlog html
#     llvmgnsyncbot/stats/steps.py buildlog_cache/linux

import argparse
import json
import os
import sys

import matplotlib.pyplot as plt
import pandas as pd

import numpy as np


def json_dir_to_dataframe(json_dir):
    files = sorted(os.listdir(json_dir), key=lambda f:int(f.split('.')[0]))
    nums = []
    for name in files:
        with open(os.path.join(json_dir, name)) as f:
            d = json.load(f)
            if d['exit_code'] != 0:
                continue
            r = { 'start_utc': d['start_utc'] }
            for s in d['steps']:
                name = s['name']
                # Ignore builds that didn't run all steps.
                if name.startswith('skip '):
                    break
                # Skip steps that are either very fast or that are fastish and
                # out of our influence anyways.
                if (name == 'pulling...' or
                    name == 'syncing...' or
                    name == 'restart goma' or
                    name == 'analyze' or
                    name == 'testing'):
                    continue
                r[name] = s['elapsed_s'] / 60.
            else:
                nums.append(r)
    return pd.json_normalize(nums)


def main():
    parser = argparse.ArgumentParser(
                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('buildlog_cache')
    parser.add_argument('-P', '--platform',
                        help='platform to show',
                        required=True,
                        choices=['linux', 'mac', 'macm1', 'win'])
    args = parser.parse_args()

    # Register datetime converter for matplotlib plotting.
    pd.plotting.register_matplotlib_converters()

    df = json_dir_to_dataframe(os.path.join(args.buildlog_cache, args.platform))

    # Put compile on top since it has highest variance.
    df = df[[col for col in df.columns if col != 'building'] + ['building']]

    print(df)

    df.start_utc = pd.to_datetime(df.start_utc, utc=True)
    #df.start_utc = df.start_utc.dt.tz_convert('US/Eastern')
    df = df.set_index('start_utc')

    df.plot.area()

    plt.ylabel('elapsed minutes')
    plt.gcf().autofmt_xdate()
    plt.legend()
    plt.tight_layout()  # Less whitespace around the plot.

    # Highlight weekends in date range.
    min_x = df.index.min()
    max_x = df.index.max()
    s = pd.date_range(min_x.normalize(), max_x.normalize()).to_series()
    for d in s[s.dt.dayofweek >= 5]:
        plt.gca().axvspan(d, d + pd.Timedelta(days=1),
                          alpha=0.03, facecolor='black', edgecolor='none')

    plt.show()

if __name__ == '__main__':
    sys.exit(main())
