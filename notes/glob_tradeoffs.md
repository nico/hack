Tradeoffs for using globs in build files
========================================

(The examples are in CMake, but all points are independent of concrete build
system.)

Some projects like listing all their source files:

```
add_executable(foo
               foo_01.cc
               foo_02.cc
)
```

Others use globs instead:

```
file(GLOB FOO_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_executable(foo "${FOO_SOURCES}")
```

Here's the list of tradeoffs for and against using globs that I know about:

Pros of using globs
-------------------

* You don't have to explicitly list all source files in a directory in a
  build file.

* You can't forget to add cpp files to build files when adding new cpp files.
  This is especially useful for tests since it's easy to miss adding a new test
  cpp file: The test just silently doesn't run.

Cons of using globs
-------------------

* It's more implicit, in that you can't grep build files to see where a
  file is referenced.

* Explicit lists give you flexibility for per-platform files, and installs

* If the glob is too wide, it might match temporary files (like swp files
  that editors might create while editing code)

* git keeps untracked files around in the tree, so if you add a new file but
  forget to `git add` it, then switch branches, then the glob-based approach will
  silently keep the file in the build. The explicit source list approach
  at least shows that you have a pending change in git diff. (This sounds
  obscure, but it's something we've run into surprisingly often in practice.)

* Likewise, if you add a new file, add it to a build file, then accidentally
  undo the build file change, you likely won't notice for a while that your file
  isn't actually getting rebuilt.

* For large projects, evaluating globs makes the generator slower.

* The generator (ie cmake) generally can't know that it needs to re-run when a
  new file is added, you have to rerun it manually somehow (explicit cmake
  call, touch a cmakelists file, ...) [1]. This makes the behavior of
  cmakelists with globs a bit surprising at times, unless you know about this.
  * But: `CONFIGURE_DEPENDS`
    * But but: See editor saving strats below

* Many editors, when saving files, first write to a temp file next to the final
  location, and then rename it to the actual file name (to save atomically, I
  think). This means every file save creates a file, which updates directory
  mtime in addition to saved file's mtime, which means the generator (cmake)
  needs to re-run on every save instead of only when new files are added.

* For CMake in particular, [the documentation recommends against using globs](
  https://cmake.org/cmake/help/latest/command/file.html?highlight=CONFIGURE_DEPENDS#filesystem)

1: The `CONFIGURE_DEPENDS` suggestion above can't work in general. In practice,
most OSs update the mtime of a directory node when a file is created in a
directory, but iirc not all of them do, and I don't know if cmake adds
directory paths to the `configure_depends` list for globs (...and all subdirs for
recursive globs) -- and even if it does, this doesn't work on all systems iirc
(maybe not on windows). (Edit, added later:) If cmake does add a dependency on
the directory mtime: Many editors implement saving by creating a temporary file
next to the actual file and then atomically renaming it to the final file name.
That means every time you save a file in such an editor, the directory mtime is
updated and cmake needs to re-run.

