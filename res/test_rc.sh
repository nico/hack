#!/bin/bash

set -eu
clang++ -std=c++11 -o rc rc.cc -Wall -Wno-c++11-narrowing

# Useful for debugging: `diff <(xxd out.res) <(xxd test.res)`
# Intentionally not using && since `set -e` doesn't work with it.
./rc < test/icon.rc
cmp test/icon.res out.res

./rc < test/cursor.rc
cmp test/cursor.res out.res

echo passed
