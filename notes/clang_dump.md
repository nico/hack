Making clang dump useful information
====================================

clang has a whole bunch of internal flags for dumping state.
Some of them are useful for end users.
This document lists some of them.

Demo input program:

```
$ cat demo.cc

```

```
% clang -Xclang -ast-dump demo.cc -std=c++17 -c
% clang -Xclang -ast-view demo.cc -std=c++17 -c
# XXX ast-dump-filter
# XXX ast-dump-all
# XXX ast-dump-decl-types
# XXX ast-dump-lookups
# XXX templight-dump
```

```
% clang -Xclang -fdump-record-layouts demo.cc -std=c++17 -fsyntax-only
% clang -Xclang -fdump-record-layouts demo.cc -std=c++17 -c

% clang -Xclang -fdump-record-layouts-canonical demo.cc -std=c++17 -c
# XXX -simple
# XXX -complete
```

```
__builtin_dump_struct
```

```
% clang -Xclang -fdump-vtable-layouts demo.cc -std=c++17 -c
```

```
-dependency-dot
```

```
debug.DumpCallGraph, debug.ViewCallGraph
debug.DumpCFG, debug.ViewCFG
debug.DumpDominators
debug.DumpLiveVars
debug.DumpLiveExprs
debug.ViewExplodedGraph
```

```
-Xclang -analyzer-dump-egraph=foo.dot
-analyzer-viz-egraph-graphviz
```

```
-Xclang -analyzer-checker-help
```

```
dump-coverage-mapping
```

```
code-completion-at
```

```
dump-tokens
dump-raw-tokens
```

```
dump-deserialized-decls
```

```
--ccc-print-phases
--ccc-print-bindings
```

Viewing GraphViz output
-----------------------
