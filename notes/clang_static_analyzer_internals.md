Clang Static Analyzer Internals
===============================

I sometimes hack on clang's static analyzer. But enough time passes between
each of my interactions with it that I forget how it works in between each time.
This document exists to hopefully make it easier for me to relearn it the next
time I touch it.

CFG construction is done by the class CFGBuilder in clang/lib/Analysis/CFG.cpp.

I mostly care about CFG construction.

How to write checkers (analyses using the CFG) is documented e.g. in:
* https://llvm.org/devmtg/2012-11/Zaks-Rose-Checker24Hours.pdf
* https://clang-analyzer.llvm.org/checker_dev_manual.html

This is a good overview of the whole analyzer:
https://llvm.org/devmtg/2019-10/slides/Dergachev-DevelopingTheClangStaticAnalyzer.pdf

## CFG Construction

The CFG is built by `CFGBuilder` in clang/lib/Analysis/CFG.cpp.

The CFG is built in reverse order: The last statement in a function
is processed first.

`CFGElement`, `CFGBlock`

Every CFG block is built in 

`CFGBuilder::buildCFG` is the entry point to CFG construction. It calls
`CFGBuilder::addStmt()` which calls `CFGBuilder::Visit` which dispatches
to a `CFGBuilder::Visit...` method based on stmt class.

XXX:

`BuildOpts.AddEHEdges` (never true in practice)

`CFGBuilder::Block` is the current block.

`Succ` is the block after the current block. Since the CFG is created in
reverse order, `Succ` is created before `Block`.

`badCFG`, `Terminator`, `cfg->getExit()`.

```
  if (Block) {
    if (badCFG)
      return nullptr;
    TrySuccessor = Block;
  } else
    TrySuccessor = Succ;
```

```
Block = nullptr;
```

`TryTerminatedBlock`

`ContinueJumpTarget`, `BreakJumpTarget`, `SEHLeaveJumpTarget`

`CFGBuilder::createBlock(bool add_successor = true)` creates a new block and
returns it. If `add_successor` is true and `Succ` is set, `Succ` is added as
successor to the new block.

`autoCreateBlock()` sets `Block` to `createBlock()` if it's `null`.

`ScopePos` chain

A block in the cfg is a `CFGBlock` object from include/clang/Analysis/CFG.h.

Link with notes:
https://reviews.llvm.org/D114660#3157160

Links:
* https://reviews.llvm.org/D112287
  `CFG construction for @try and @catch`
  * https://reviews.llvm.org/D114660
    `Fix -Wreturn-type false positive in @try statements` followup
* https://reviews.llvm.org/D36914
  `CFG construction for __try / __except / __leave`
* https://reviews.llvm.org/rG04c6851cd6053
  Removed exceptional edges from CallExprs to tries
* https://reviews.llvm.org/rG33979f75a0fd8
  Added -Wreturn-type
* https://reviews.llvm.org/rG0c2ec779cf and then
  https://reviews.llvm.org/rG918fe8498d moved the -Wreturn-type code to its
  current location
 

## Exception handling

Codegen doesn't use CFG but generates LLVM IR. It creates calls to dtors
on all paths outside of a scope instead.

XXX SEH `__leave`

Exception edges are currently not modelled. There's `AddEHEdges` but it's
always true in practice. If it's true (which it never is), _every_ call in a
try block adds an edge to the catch list. (For SEH, that's not enough:
every statement would have to add an edge! There's no code for that at all,
not even behind `AddEHEdges`.)

Try bodies just become regular blocks.

A special "try" block is added for every try, but it's an additional CFG
root (to model the "exception is thrown" behavior). With `AddEHEdges`, that's
where calls would add edges to (XXX check if that's true).

### Finally

`finally` special since every input edge has its own output edge

similar to c++ dtors. these are modeled by XXX

Idea: Every jump out of a try (return, continue, throw, goto, break)
jumps to the try's finally instead, and that then gets the jump's destination
as (additional) successor.

This nests, though: If the finally is another try that also has a finally,
the inner finally has the outer finally as successor and _that_ gets the
jump's destination instead.

## Analysis-based warnings

Some regular clang warnings can use the CFG to produce warnings, even during
a normal compile, without using the static analyzer.

(As far as I know, these warnings don't use the analyzer's symbolic execution
features, just the CFG itself.)

These are in clang/lib/Sema/AnalysisBasedWarnings.cpp.

`-Wreturn-type`, `-Wunreachable-code`

## Dumping with `debug.DumpCFG`

Demo:

```
% cat test.cc
int main(int argc, char* argv[]) {
  if (argc > 1)
    return 4;

  return 5;
}
```
```
% clang -cc1 -analyze -analyzer-checker=debug.DumpCFG test.cc 
int main(int argc, char *argv[])
 [B4 (ENTRY)]
   Succs (1): B3

 [B1]
   1: 5
   2: return [B1.1];
   Preds (1): B3
   Succs (1): B0

 [B2]
   1: 4
   2: return [B2.1];
   Preds (1): B3
   Succs (1): B0

 [B3]
   1: argc
   2: [B3.1] (ImplicitCastExpr, LValueToRValue, int)
   3: 1
   4: [B3.2] > [B3.3]
   T: if [B3.4]
   Preds (1): B4
   Succs (2): B2 B1

 [B0 (EXIT)]
   Preds (2): B1 B2
```

Implemented in clang/lib/StaticAnalyzer/Checkers/DebugCheckers.cpp by CFGDumper.


### What about `debug.ViewCFG`?

There's also `-analyzer-checker=debug.ViewCFG`, but:

* While it works in open-source clang builds, it doesn't work in Xcode's clang
* It tries to run `open ${tmpfile}.dot` on the result on macOS, and there's
  no good `.dot` file anymore.

It prints the name of the temp dot file it writes, so it's possible to
paste that into a web-based dot viewer, but that's a bit of a pain.

## Pretty-printing implementation

`StmtPrinterHelper`
`CFGBlockTerminatorPrint`
