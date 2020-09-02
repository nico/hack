#!/bin/bash
rm -rf *.profraw *.prof a.out.*

set -x

CFLAGS="-std=c++11 -O2 -isysroot $(xcrun -show-sdk-path)"

# vanilla
~/src/llvm-project/out/gn/bin/clang bar.cc baz.cc $CFLAGS -o a.out.vanilla

# clang instrumentation
~/src/llvm-project/out/gn/bin/clang -fprofile-instr-generate=prof-instr.profraw bar.cc baz.cc $CFLAGS -o a.out.prof-instr-gen
./a.out.prof-instr-gen 100000
~/src/llvm-project/out/gn/bin/llvm-profdata merge -output=prof-instr.prof prof-instr.profraw
~/src/llvm-project/out/gn/bin/clang -fprofile-instr-use=prof-instr.prof bar.cc baz.cc $CFLAGS -o a.out.prof-instr-use

# IR instrumentation
~/src/llvm-project/out/gn/bin/clang -fprofile-generate=prof.profraw bar.cc baz.cc $CFLAGS -o a.out.prof-gen
./a.out.prof-gen 100000
~/src/llvm-project/out/gn/bin/llvm-profdata merge -output=prof.prof prof.profraw/default*.profraw
~/src/llvm-project/out/gn/bin/clang -fprofile-use=prof.prof bar.cc baz.cc $CFLAGS -o a.out.prof-use

# CSIR instrumentation (first 3 steps identical to previous)
~/src/llvm-project/out/gn/bin/clang -fprofile-generate=prof-cs-1.profraw bar.cc baz.cc $CFLAGS -o a.out.prof-gen-cs-1
./a.out.prof-gen-cs-1 100000
~/src/llvm-project/out/gn/bin/llvm-profdata merge -output=prof-cs-1.prof prof-cs-1.profraw/default*.profraw

~/src/llvm-project/out/gn/bin/clang -fprofile-use=prof-cs-1.prof -fcs-profile-generate=prof-cs-2.profraw bar.cc baz.cc $CFLAGS -o a.out.prof-gen-cs-2
./a.out.prof-gen-cs-2 100000
~/src/llvm-project/out/gn/bin/llvm-profdata merge -output=prof-cs-2.prof prof-cs-2.profraw/default*.profraw prof-cs-1.prof
~/src/llvm-project/out/gn/bin/clang -fprofile-use=prof-cs-2.prof bar.cc baz.cc $CFLAGS -o a.out.prof-cs-use
