#!/bin/bash

set -eu
clang++ -std=c++11 -o rc rc.cc -Wall -Wno-c++11-narrowing

# Useful for debugging: `diff <(xxd out.res) <(xxd test.res)`
# Intentionally not using && since `set -e` doesn't work with it.
./rc < test/cursor.rc
cmp test/cursor.res out.res

./rc < test/bitmap.rc
cmp test/bitmap.res out.res

./rc < test/icon.rc
cmp test/icon.res out.res

./rc < test/menu.rc
cmp test/menu.res out.res
./rc < test/menu_opts.rc
cmp test/menu_opts.res out.res

./rc < test/stringtable.rc
cmp test/stringtable.res out.res

./rc < test/accelerators.rc
cmp test/accelerators.res out.res

./rc < test/rcdata.rc
cmp test/rcdata.res out.res

./rc < test/versioninfo_fixedonly.rc
cmp test/versioninfo_fixedonly.res out.res
# FIXME ./rc < test/versioninfo.rc
# FIXME cmp test/versioninfo.res out.res

./rc < test/dlginclude.rc
cmp test/dlginclude.res out.res

./rc < test/html.rc
cmp test/html.res out.res

echo passed
