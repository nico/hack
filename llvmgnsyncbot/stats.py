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


df = json_dir_to_dataframe(sys.argv[1])

# Only keep successful builds.
df = df[df.exit_code == 0]

plt.scatter(df.build_nr, df.elapsed_s)
plt.show()
