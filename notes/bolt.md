LLVM Bolt notes
===============

Build bolt binaries:

```
% time caffeinate ninja -C out/gn -j250 \
    llvm-bolt llvm-bolt-heatmap llvm-bat-dump merge-fdata
```


When building your program, keep static relocations in the executable.
Also, bolt's PLT optimization requires symbols are resolved at program start.
Use:

```
-Wl,--emit-relocs -Wl,-znow
```

Instrument your program by running:

```
bin/llvm-bolt your_program -o your_program.inst \
    -instrument --instrumentation-file-append-pid |
    --instrumentation-file=your_profile_dir/prof.fdata
```

Train by running `your_program` on some workload.

Merge the generated profiles:

```
clang/utils/perf-training/perf-helper.py merge-fdata bin/merge-fdata \
    'merged.fdata your_profile_dir
```

Then optimize using the generated profiles:

```
bin/llvm-bolt your_program -o your_program.opt -data merged.fdata \
    -reorder-blocks=ext-tsp -reorder-functions=hfsort+ \
    -split-functions -split-all-cold -split-eh -dyno-stats' \
    -icf=1 -use-gnu-stack -use-old-text
```
