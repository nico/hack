Notes on LLVM Profile-Guided Optimization (PGO)
===============================================

Example program
---------------

Here's a program:

```cpp
#include <stdlib.h>

__attribute__((noinline))
static int c1(int i) { return i % 2 == 0 ? i / 2 : 3*i + 1; }

__attribute__((noinline))
static int c2(int i) {
  int n = 0;
  for (n = 0; i != 1; ++n)
    i = c1(i);
  return n;
}

int main(int argc, char* argv[]) {
  if (argc > 1)
    return c2(atoi(argv[1]));
}
```

Instrumenting and generating profiling data
-------------------------------------------

Let's compile it with instrumentation and then run it to generate profiling
data:

```sh
% rm -rf profiledata.dir
% clang c.c -O2 -fprofile-generate=profiledata.dir
% ./a.out 871
% llvm-profdata merge profiledata.dir -o c.profdata
```

(If you're on a mac, use `xcrun llvm-profdata` to run llvm-profdata.)

There's also `-fprofile-instr-generate`, which is great for instrumentation for
code coverage, but which does _not_ work well for PGO. `-fprofile-generate`
works by inserting counting instrumentation at the LLVM IR level, while
`-fprofile-instr-generate` inserts it as the Clang AST level.

Optimizing with profiling data
------------------------------

With this, we could create a profile-guided optimized (PGO) binary like so:

```sh
% clang c.c -O2 -fprofile-use=c.profdata
```

This is how you normally use PGO.

You can add `-S -o after.s` to save the PGO'd assembly to a file, and run
`clang c.c -O2 -S -o before.s` to get assembly to a different file, and then
diff before.s and after.s to see if the profile information had any effect.
On my system, even for such a tiny program, it moved one basic block around.

Other uses of profiling data
----------------------------

But! We can do other things with the profiling data as well.

### Show the top N hottest functions

```sh
% llvm-profdata show -topn=2 c.profdata      
Instrumentation level: IR  entry_first = 0
Total functions: 3
Maximum function count: 111
Maximum internal block count: 39
Top 2 functions with the largest internal block counts: 
  c.c:c2, max count = 111
  c.c:c1, max count = 72
```

This prints a short header, and then N functions, with their sample counts.

### Convert the profiling data to a text file

```sh
% llvm-profdata merge --text c.profdata -o c.proftext
```

(If you want to just look at the data, pass `-o -`, then it'll print to stdout.)

You can then open `c.proftext` in a text editor, modify it, and then convert
it back to binary (for PGO'ing with it, for example):

```sh
% llvm-profdata merge c.proftext -o c.profdata2
```

If you don't edit the text file, `c.profdata` and `c.profdata2` are identical.
If you want to be explicit, you can pass `--binary`, but it's the default.

How does PGO instrumentation work?
----------------------------------

Here's what the start of `c.proftext` (see above) looks like:

```
# IR level Instrumentation Flag
:ir
c.c:c1
# Func Hash:
146835647075900052
# Num Counters:
2
# Counter Values:
113
65
```

It's then followed by similar entries for `c2` and `main`. What do these lines
mean?

The first (non-comment) line appears just says that this is an LLVM IR-level
profile (`:ir`). This appears just once in the file, not per-function.

Per-function, we have:

1. The filename the function is in, and its symbol name (`c.c:c1`)

2. A hash of the function's CFG structure. When compiling the file with profile
   generation, LLVM computes a hash of the function's CFG (before inserting
   profiling instrumentation), and stores in the (instrumented) output .o
   file. The linker copies it to the (instrumented) output executable, and
   when the binary runs, the profiling runtime extracts that data and writes
   it to the generated profile. When the file is then compiled again with
   `-fprofile-use=`, LLVM checks that the (pre-profile-optimizations) IR
   is identical to what was originally instrumented.

   Exercise: Build the example with
   `-O2 -fprofile-generate=profiledata.dir -S -o -` and find `c1`'s
   hash in the _instrumented_ compiler output.

3. The number of counters per function. Handwavingly, for every branch, the
   instrumentation inserts a counter that counts how often the branch is taken.
   For `c1`, there are two counters: one for each branch of
   `i % 2 == 0` is true.

4. The values of those counters. In this case, the function is called `113+65`
   times total, and the value passed to `c1` is even more often than it is odd.

When optimizing a function, how can LLVM map these counters to branches in
the code?

[PGOInstrumentation.cpp][1] has a comment at the top that explains how it works:
Both when instrumenting and when using the profiling data, LLVM computes where
in each function the edges between basic blocks should have a counter. Since
the function's CFG shape is exactly identical (because the hashes match),
the counter locations are the same when the instrumentation was inserted and
now when the profiling data is used: LLVM can associate the counter values
with the IR locations by index. (Here's a [PDF of the "Optimal measurement of
points for program frequency counts" paper cited in that file][2]).

That also means you can use slightly older profiling data with newer
code—maybe you only regenerate profiling data once per day or so. For all
functions that have matching hashes, the profiling data can be used. As long as
most of the codebase doesn't change in a day, this will work ok.

You can run `clang c.c -fprofile-use=c.profdata -O2 -S -emit-llvm -o -`
to see LLVM IR annotated with profiling data. Look for `!prof`, `!PGOFuncName`,
and the corresponding metadata at the bottom. Here's an excerpt for `c1`:

```
define internal fastcc i32 @c1(i32 noundef %i) unnamed_addr #3 !prof !54 !PGOFuncName !55 {
entry:
  %0 = and i32 %i, 1
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %cond.true, label %cond.false, !prof !56

cond.true:                                        ; preds = %entry
  %div = sdiv i32 %i, 2
  br label %cond.end

cond.false:                                       ; preds = %entry
  %mul = mul nsw i32 %i, 3
  %add = add nsw i32 %mul, 1
  br label %cond.end

cond.end:                                         ; preds = %cond.false, %cond.true
  %cond = phi i32 [ %div, %cond.true ], [ %add, %cond.false ]
  ret i32 %cond
}
[...]
!54 = !{!"function_entry_count", i64 178}
!55 = !{!"c.c:c1"}
!56 = !{!"branch_weights", i32 113, i32 65}
```

The function signature references `!prof !54 !PGOFuncName !55`, and the branch
is annotated with `!prof !56`. The metadata at the bottom has the same branch
weights we saw in the `.proftext` file.

[1]: https://github.com/llvm/llvm-project/blob/main/llvm/lib/Transforms/Instrumentation/PGOInstrumentation.cpp

[2]: https://zh.booksc.eu/book/6775578/7bcec5
