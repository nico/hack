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

Other similar things
--------------------

(All work with ThinLTO.)

1. PGO
2. [CSPGO](https://reviews.llvm.org/D54175) (2018)
3. Propeller (2019)
   [1](https://github.com/google/llvm-propeller/blob/424c3b885e60d8ff9446b16df39d84fbf6596aec/Propeller_RFC.pdf)
   [2](https://lists.llvm.org/pipermail/llvm-dev/2019-September/135393.html)
   [3](https://github.com/google/llvm-propeller/blob/main/ArtifactEvaluation/Scripts/optimize_clang.sh)


Interesting thread (more about Propeller):
<https://lists.llvm.org/pipermail/llvm-dev/2019-October/135616.html>

eg
<https://lists.llvm.org/pipermail/llvm-dev/2019-October/136097.html> =>
<https://docs.google.com/document/d/1jqjUZc8sEoNl6_lrVD6ZkITyFBFqhUOZ6ZaDm_XVbb8/edit>

<https://lists.llvm.org/pipermail/llvm-dev/2019-October/136102.html>

<https://lists.llvm.org/pipermail/llvm-dev/2020-January/138426.html>

LLVM upstream profile data formats
----------------------------------

* Frontend (`-fprofile-instr-generate`, for coverage)
* IR (`-fprofile-generate`, for PGO)
* CS-IR (`fprofile-use=pass1.profdata -fcs-profile-generate`, for CSPGO)
* Sampling (eg for profiles collected by `perf`)

(See also `pgotest/build.sh` and `notes/llvm_pgo.md` in this repo.)
