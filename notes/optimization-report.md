`-fsave-optimization-record`
============================

Summary:

1. Get compile command (eg from `ninja -v`)
2. Add `-fsave-optimization-record` to it, it'll put a `.opt.yaml` next to the
   `.o` output
3. `python3 ~/src/llvm-project/llvm/tools/opt-viewer/opt-viewer.py foo.opt.yaml`
4. `open html/index.html`

<https://johnnysswlab.com/loop-optimizations-interpreting-the-compiler-optimization-report/>
has lots of details.
