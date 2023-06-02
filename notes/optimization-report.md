`-fsave-optimization-record`
============================

Summary:

0. Install opt-viewer.py deps with `pip3 install pygments` and
   `pip3 install PyYAML`.
1. Get compile command (eg from `ninja -v`)
2. Add `-fsave-optimization-record` to it, it'll put a `.opt.yaml` next to the
   `.o` output
3. `python3 ~/src/llvm-project/llvm/tools/opt-viewer/opt-viewer.py foo.opt.yaml`

    Due to a [bug](https://github.com/llvm/llvm-project/issues/62403), you
    currently have to pass `-j1`, else you'll get
    `AttributeError: type object 'Missed' has no attribute 'demangler_lock'`.

    If passing an absolute path to the source file to clang in step 2 (*cough*
    cmake), pass `-s ../..` (or similar) to opt-viewer.py.
4. `open html/index.html`

<https://johnnysswlab.com/loop-optimizations-interpreting-the-compiler-optimization-report/>
has lots of details.
