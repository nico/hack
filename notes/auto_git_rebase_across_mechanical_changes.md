Auto-`git rebase` across mechanical changes
===========================================

Let's say you want to clang-format a large codebase, and you're worried that
the reformatting change causes everyone work when they rebase their in-flight
patches. This note describes how you can make rebasing across the reformatting
completely transparent. (You can also use this approach for other large-scale
mechanical changes)

For this to be completely automatic, you need three things:

1. Create a rebasing script

   You need a script that is called during the rebase process to merge changes.
   The script will be called for each file that's being rebased and takes four
   arguments:

   1. `base`: Path to the file's contents at the ancestor's version.
   2. `current`: Path to the file's contents at the current version.
   3. `other`: Path to the file's contents at the other branch's
      version (for nonlinear histories, there might be multiple other branches,
      this document here glosses over that).
   4. `path`: The path of the file in the repository.

   For example, the four arguments to the script might be:

       .merge_file_ATljVo .merge_file_jtKanZ .merge_file_sCmdle path/to/a/file.c

   The job of the script is to apply the mechanical change to the three files
   at 1, 2, 3 and then run

       git merge-file -Lcurrent -Lbase -Lother $base $current $other

   This applies the mechanical change to in-flight patches while they are being
   rebased, avoiding conflicts from the mechanical change but not hiding true
   conflicts.

   Here is a concrete example for the mechanical change being "run
   clang-format".

   Since `base`, `current`, and `other` are paths to temporary files in the
   repository's root, `clang-format` will only pick up the contents of
   `.clang-format` in the repository's root.  To fix this, `clang-format`
   helpfully has a `--assume-filename=` option that you can receive
   the forth argument `$path` but it only honors this flag if the input
   comes from stdin. So you can't use `-i` but have to pipe to clang-format and
   make its output to memory, and then overwrite the file in a second step:

       $ cat my/clang_format_script.sh
       #!/bin/bash
       base="$1"
       current="$2"
       other="$3"
       path="$4"
       clang-format --style=file --assume-filename=$path < $base > $base.tmp
       mv $base.tmp $base
       clang-format --style=file --assume-filename=$path < $current > $current.tmp
       mv $current.tmp $current
       clang-format --style=file --assume-filename=$path < $other > $other.tmp
       mv $other.tmp $other
       git merge-file -Lcurrent -Lbase -Lother $base $current $other

   You also need to make sure clang-format is the same version for everyone,
   else could get conflicts on lines where clang-format's output is different
   across versions (arguably a good thing).

2. Define a custom merge driver as [described in git's documentation](
   https://git-scm.com/docs/gitattributes#_defining_a_custom_merge_driver): Add

       [merge "reformat"]
         name = Free-form text
         driver = my/clang_format_script.sh %O %A %B %P
         recursive = binary

   (You don't need `%L` from the docs.)

   This gives the merge driver a name, in this case `reformat`.  You can
   arbitrarily choose the name of the merge driver, but it has to be the same
   for everyone. Note that the merge driver also has a `name` property, but
   that's just free-form text that doesn't matter.  The name of the
   merge-driver here is `reformat` (important), and the value of its `name`
   property is `Free-form text` (irrelevant). The `driver` property is very
   important, as it tells git which command to run and which arguments to pass
   to it.

   The current working directory when this driver runs will be the repository
   root, so you can use relative paths to scripts in your repository.

   Ideally you can install this from some repository hook. If that's not an
   option, you have to ask everyone to run something like:

       git config merge.reformat.name "Free-form text"
       git config merge.reformat.driver "my/clang_format_script.sh %O %A %B %P"
       git config merge.reformat.recursive binary

   If you don't have a hook to automatically add the config from, this does
   require manual work by everyone, but it can be done well in advance of the
   mechanical change.

3. In the commit that does the mechanical change (or in a separate commit that
   lands at the same time), also add this to a `.gitattributes` file in the
   directory that the mechanical change applies to:

       *.cpp merge=clang-format
       *.h   merge=clang-format

   (Use whatever pattern appropriate for your project and your change instead,
   of course.)

Testing
-------

For testing, do 1 and 2, then make two branches off main called `branch1` and
`branch2`, make one commit on each tthat changes some file in conflict, add a
`.gitattributes` file that contains

    your-test-file.cpp merge=reformat

(no need to add that one to source control) and run

    git rebase branch1 branch2

If you add bunch of print statements to your script from step 1, you should see
them show up.

Real-world examples
-------------------

Chromium used this approach when clang-formatting all of Blink after forking it
from WebKit. Here are references to the changes made there:

* Part 1: <https://codereview.chromium.org/2356933002> Chromium supports Windows
  as development platform, so there's a bash script for unix systems and a bat
  file for Windows developers that both forward to a python script that does
  the actual work.
* Part 2: <https://codereview.chromium.org/2359933005> Chromium uses a system
  called `gclient` for managing dependencies that developers run after every
  sync. This was a convenient place for installing the git mergedriver config.
* Part 3: <https://codereview.chromium.org/2382993005>
* Initial description of the idea: <https://bugs.chromium.org/p/chromium/issues/detail?id=574611#c23>
