#!/usr/bin/env python3

"""prints average and max time of the 'building' step"""

# Use e.g. like
#     rsync -az llvm@45.33.8.238:buildlog .
#     llvmgnsyncbot/annotate.py buildlog html
#     llvmgnsyncbot/stats/compile_time_avg.py buildlog_cache

import json
import os
import sys

import pandas as pd


def json_dir_to_dataframe(json_dir):
    files = sorted(os.listdir(json_dir), key=lambda f:int(f.split('.')[0]))
    def load(name):
        with open(os.path.join(json_dir, name)) as f:
            d = json.load(f)
            compile_step = next(
                (s for s in d['steps'] if s['name'] == 'building'), None)
            if compile_step:
                d['elapsed_compile_s'] = compile_step['elapsed_s']
            del d['steps'], d['log_file']
            d.pop('git_revision', None)
            d.pop('prev_git_revision', None)
            return d
    return pd.json_normalize([load(f) for f in files])


for platform in ('linux', 'mac', 'macm1', 'win',):
    df = json_dir_to_dataframe(os.path.join(sys.argv[1], platform))
    df = df[df.exit_code == 0]  # Only keep successful builds.
    print(platform, 'average compile time', df.elapsed_compile_s.mean(),
          ', max', df.elapsed_compile_s.max())

    if platform == 'macm1':
        # macm1 didn't use goma until build 7476.
        df = df[df.build_nr >= 7476]
        print('macm1 average compile time post goma',
              df.elapsed_compile_s.mean(),
              ', max', df.elapsed_compile_s.max())
