#!/usr/bin/env python3

"""Reads the buildlog_cache dir created by annotate.py to produce some stats."""

# Use e.g. like
#     rsync -az llvm@45.33.8.238:buildlog .
#     llvmgnsyncbot/annotate.py buildlog html
#     llvmgnsyncbot/stats/stats.py buildlog_cache

import argparse
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
    return pd.json_normalize([load(f) for f in files])


def main():
    parser = argparse.ArgumentParser(
                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-P', '--platforms',
                        help='comma-delimited list of platforms',
                        type=lambda s:s.split(','),
                        default='linux,mac,macm1,win')
    parser.add_argument('buildlog_cache')
    args = parser.parse_args()

    # Register datetime converter for matplotlib plotting.
    pd.plotting.register_matplotlib_converters()

    max_y = 0
    min_x = pd.to_datetime(pd.Timestamp.max, utc=True)
    max_x = pd.to_datetime(pd.Timestamp.min, utc=True)

    for platform in args.platforms:
        df = json_dir_to_dataframe(os.path.join(args.buildlog_cache, platform))

        # Only keep successful builds.
        df = df[df.exit_code == 0]

        df.start_utc = pd.to_datetime(df.start_utc, utc=True)
        #df.start_utc = df.start_utc.dt.tz_convert('US/Eastern')
        df = df.set_index('start_utc')

        # Filter out outlier slow builds that blow out the scale.
        if platform == 'win':
            # Builds before 137 had lots of security software enabled which
            # caused cycle times of 45+ mins.
            df = df[~(df.build_nr < 137) &
                    ~((df.build_nr >= 45010) & (df.build_nr < 45050)) &
                    ~((df.build_nr >= 45081) & (df.build_nr < 45099)) &
                    ~((df.build_nr >= 45200) & (df.build_nr < 45220))]
            # Filter out shark fin, for clearer trend lines.
            fin_start = pd.Timestamp('2019-11-08').tz_localize('UTC')
            fin_end = pd.Timestamp('2019-12-05').tz_localize('UTC')
            df = df[(df.elapsed_s <= 8 * 60 * 60) |
                    (df.index < fin_start) | (df.index > fin_end)]
            # Filter out 80x slowdown due to security software problem
            # around 2021/9/5, 2021/9/8, 2021/9/14.
            noel_start = pd.Timestamp('2021-09-04').tz_localize('UTC')
            noel_end = pd.Timestamp('2021-09-16').tz_localize('UTC')
            df = df[(df.elapsed_s <= 60 * 60) |
                    (df.index < noel_start) | (df.index > noel_end)]
        elif platform == 'mac':
            # Filter out a stray build where compile took 1.5h for some reason.
            df = df[df.build_nr != 34788]
        elif platform == 'macm1':
            # First few M1 builds missed "prevent sleep with no monitor" toggle.
            df = df[df.build_nr > 10]

        print(platform)
        print(df[df.elapsed_s > 2 * 60 * 60])

        min_x = min(min_x, df.index.min())
        max_x = max(max_x, df.index.max())

        df['elapsed_m'] = df.elapsed_s / 60.
        max_y = max(max_y, df.elapsed_m.max())

        plt.scatter(df.index, df.elapsed_m, alpha=0.1, label=platform)

        plt.plot(df.index, df.elapsed_m.rolling(
                250, min_periods=10, win_type='hann', center=True).mean(),
            label='%s (avg)' % platform)

    plt.ylabel('elapsed minutes')
    plt.gcf().autofmt_xdate()
    plt.legend()
    plt.tight_layout()  # Less whitespace around the plot.

    plt.xlim(min_x, max_x)
    plt.ylim(0, max_y)

    # Highlight weekends in date range.
    s = pd.date_range(min_x.normalize(), max_x.normalize()).to_series()
    for d in s[s.dt.dayofweek >= 5]:
        plt.gca().axvspan(d, d + pd.Timedelta(days=1),
                          alpha=0.03, facecolor='black', edgecolor='none')

    plt.show()

if __name__ == '__main__':
    sys.exit(main())
