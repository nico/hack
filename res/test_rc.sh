#!/bin/bash

set -eu
clang++ -std=c++11 -o rc rc.cc -Wall -Wno-c++11-narrowing

# Useful for debugging: `diff <(xxd out.res) <(xxd test.res)`
./rc < test/icon.rc && cmp test/icon.res out.res

echo passed
