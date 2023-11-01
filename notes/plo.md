Post-link optimization
======================

This started out as a few notes on bolt, but then grew to cover other
post-link optimization ("PLO") technologies.

In the meantime, Amir Ayupov wrote a pretty good overview on this too,
[here](https://aaupov.github.io/blog/2023/07/09/pgo). It's a great overview.
This document here still has links to some more details.

LLVM Bolt notes
---------------

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
3. [CSSPGO](https://reviews.llvm.org/D90125) (2020) (review links to RFC)
4. Propeller (2019)
   [1](https://github.com/google/llvm-propeller/blob/424c3b885e60d8ff9446b16df39d84fbf6596aec/Propeller_RFC.pdf)
   [2](https://lists.llvm.org/pipermail/llvm-dev/2019-September/135393.html)
   [3](https://github.com/google/llvm-propeller/blob/main/ArtifactEvaluation/Scripts/optimize_clang.sh)
   [4](https://storage.googleapis.com/pub-tools-public-publication-data/pdf/578a590c3d797cd5d3fcd98f39657819997d9932.pdf)

CSPGO is context-sensitive PGO, which does a second profile-generaation and
PGO-link step after the first, to get profiles of code after inlining done
during the first PGO pass.

CSSPGO is context-sensitive sampling PGO, that is CSPGO using sampling to
collect data for the second pass instead of instrumentation, like in vanilla
CSPGO. Currently, the only sampling input seems to be perf.data information.

Interesting thread (more about Propeller):
<https://lists.llvm.org/pipermail/llvm-dev/2019-October/135616.html>

eg
<https://lists.llvm.org/pipermail/llvm-dev/2019-October/136097.html> =>
<https://docs.google.com/document/d/1jqjUZc8sEoNl6_lrVD6ZkITyFBFqhUOZ6ZaDm_XVbb8/edit>

<https://lists.llvm.org/pipermail/llvm-dev/2019-October/136102.html>

<https://lists.llvm.org/pipermail/llvm-dev/2020-January/138426.html>

<https://reviews.llvm.org/D68062> -- but looks like currently the thinking
is to use `create_llvm_prof` from the autofdo repo to convert it to an llvm
sample profile.

[Propeller RFC](https://github.com/google/llvm-propeller/blob/424c3b885e60d8ff9446b16df39d84fbf6596aec/Propeller_RFC.pdf)
"Can this be done with PGO itself in the compiler?" section compares to PGO and
CS-PGO.

It claims that instrumentation-based profiles are noticeably worse than
sampling-based ones, at least for post-link optimization ("PLO").

XXX: measure impact of sampling vs instrumentation, using llvm-propeller's
measurement script.

Apparently someone figured out how to do `perf` on macOS:
<https://gist.github.com/ibireme/173517c208c7dc333ba962c1f0d67d12>

(Instruments.app can read performance counters, but it can't be driven from
a script as far as I know.)

LLVM upstream profile data formats
----------------------------------

* Frontend (`-fprofile-instr-generate`, for coverage)
* IR (`-fprofile-generate`, for PGO)
* CS-IR (`fprofile-use=pass1.profdata -fcs-profile-generate`, for CSPGO)
* Sampling (eg for profiles collected by `perf`)

(See also `pgotest/build.sh` and `notes/llvm_pgo.md` in this repo.)
