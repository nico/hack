# Quickly copying directories containing large files on macOS

Let's say we have a directory containing a large file:

    mkdir foo
    dd if=/dev/urandom of=foo/large bs=1m count=1k

Let's throw in a hardlink and a symlink:

    ln -s large foo/symlink
    ln foo/large foo/hardlink

We want to copy this directory quickly, and preserve symlinks.
We want to preserve timestamps on the copied files and directory.

We want to only use tools that are part of a macOS default install.

On Linux, we could use `cp -alR`, but that's not supported on macOS.

APFS added high-res time stamps, which can be queried with `stat -f %Fm`:

    % stat -f '%N %Fm' foo/{,*}
    foo/ 1609297230.485880489
    foo/hardlink 1609294944.907726636
    foo/large 1609294944.907726636
    foo/symlink 1609294960.193255375
    
(On Linux, use `stat -c %.Y` instead.)

To check if hardlinks are preserved, you can check:

* `ls -i` output to print each file's inode (all hardlinks to a file should
  print the same inode)

* `ls -l` output, which contains the number of links to a file after the
  permission bits and before the owner's name. In this example, it's the `2`:

  ```
  % ls -l foo/hardlink
  -rw-r--r--  2 thakis ...
  ```

## cp

Let's try the obvious thing:

    % time (rm -rf bar && cp -aR foo bar)
    0.720 total

That's slow. From `man cp`:

    -R ...
       Note that cp copies hard-linked files as separate files.  If you need to
       preserve hard links, consider using tar(1), cpio(1), or pax(1) instead.

The output directory is 2 GiB, in addition to the 1 GiB input directory.
That explains the slowdown.

It also chops off the nanoseconds of the timestamps (note trailing zeros)
(update: Fixed in macOS 11.3):

    % stat -f '%N %Fm' bar/{,*}
    bar/ 1609297230.485880000
    bar/hardlink 1609294944.907726000
    bar/large 1609294944.907726000
    bar/symlink 1609294960.193255000

This means that this method can't be used with mtime-tracking build systems
that read high-resolution timestamps.

Slow and (before macOS 11.3) buggy! But the man page at least tells us which
alternatives to look at.

## ditto

This is not among the alternatives listed in `man cp`. But it also copies
directories, so let's try it for completeness.

    % time (rm -rf bar && ditto foo bar)
    0.493 total

Faster, but still not fast. It copies `large` instead of hardlinking it, but it
makes `bar/hardlink` a hardlink to `bar/large`, so at least it copies the file
just once, not twice like `cp`.

`ditto` also makes an effort to copy mtimes from the source directory to the
output directory, but it chops off the subsecond part of the timestamp which
makes this unusable fro build system use, and it gives the symlink in the
output dir the current time as mtime instead of the mtime of the symlink in the
source directory.  (TODO: file bugs for these.)

## pax

Has a `-l` switch that tells it to hardlink the outputs to the inputs:

    % time (rm -rf bar && mkdir bar && cd foo && pax -rwl . ../bar)
    0.008 total

The hardlinks mean that this is fast! It also means that the output directory
needs almost no additional storage space in addition to the 1 GiB needed for
the input directory.

And the timestamps at first sight look good (no wonder, these are hardlinks):

    % stat -f '%N %Fm' bar/*
    bar/hardlink 1609294944.907726636
    bar/large 1609294944.907726636
    bar/symlink 1609302462.687747817

But, alas, the timestamp on the directory itself is truncated
(update: fixed in macOS 12):

    % stat -f '%N %Fm' bar
    bar 1609297230.000000000

So this is unusable for build system use too.
(And when not using `-l`, the timestamps for all output files are truncated.
(Update: Also fixed in macOS 12.))

If you can require macOS 12+, this works!

If we relax the requirement to preserve timestamps and just require that the
output files have timestamps that are not older than the input timestamps, then
we can pass `-p m` to not preserve mtimes. With that, pax updates the mtime
on the directory (and, without `-l`, on all files) to the current time --
but only with second granularity:

    % time (rm -rf bar && mkdir bar && cd foo && pax -rwl -p m . ../bar)
    0.009 total
    % stat -f '%N %Fm' bar/{,*}
    bar/ 1609693043.000000000
    bar/hardlink 1609294944.907726636
    bar/large 1609294944.907726636
    bar/symlink 1609693043.832774972

This should be workable in most situations. If we do need higher-resolution
timestamps, we could run `touch ../bar` at the end. But:

* `touch` only sets milliseconds and microseconds, not nanoseconds (like `cp`,
  but unlike most other methods for creating files). This isn't a problem in
  practice, but it's strange.

* If the directory contains subdirectories that also need timestamps that are
  more granular than seconds, something like `find ../bar -type d | xargs touch`
  is needed, and at that point `cpio` doesn't look much worse.

But most of the time, seconds-granularity is sufficient. So if preserving
mtimes isn't needed, this is an ok approach.

## cpio

Also has a `-l` switch, but takes lists on stdio so it's a bit weird to use:

    % time (rm -rf bar && cd foo && find . | cpio -pdlm ../bar)
    0 blocks
    0.012 total

Also fast, but a bit slower due to both `find` and `cpio` having to look at
each file -- and for directories with many files in it, the overhead from that
would be more noticeable.

It sets all timestamps correctly, even on the output directory itself!

    % stat -f '%N %Fm' bar/{,*}
    bar/ 1609297230.485880489
    bar/hardlink 1609294944.907726636
    bar/large 1609294944.907726636
    bar/symlink 1609294960.193255375

This needing `find` and a pipe makes it slower than necessary and a bit ugly,
as does this printing `0 blocks`. But this is usable.

## tar and rsync

tar and rsync both have modes that let it preserve hardlinks, meaning that
bar/hardlink will hardlink to bar/large, but as far as I can tell they don't
have modes to hardlink to the source directory. They're slower than `cp`:

    % time (rm -rf bar && rsync -a -H --delete --numeric-ids foo/ bar/)
    3.399 total
    % time (rm -rf bar && (cd foo; tar cf - .) | (mkdir bar; cd bar; tar xf -))
    1.170 total

They also both truncate mtimes to whole seconds.

## cp reloaded

In macOS 10.12, `cp` grew a new flag `-c`:

     -c    copy files using clonefile(2)

`man clonefile` says:

     clonefile -- create copy on write clones of files

Let's give it a try:

    % time (rm -rf bar && cp -caR foo bar)
    0.007 total

Also very fast. It also mysteriously fixes the timestamps on the large
files, but it still truncates nanoseconds for the symlink and the directory
itself:

    % stat -f '%N %Fm' bar/{,*}
    bar/ 1609297230.485880000
    bar/hardlink 1609294944.907726636
    bar/large 1609294944.907726636
    bar/symlink 1609294960.193255000

If we relax the requirement to preserve timestamps and just require that the
output files have timestamps that are not older than the input timestamps, then
we can use `cp -cR` and this method works.

Another drawback is that if this method is used as part of a build and something
writes the input directory on the next build, the OS has to copy-on-write
all pages when `foo/large` is written on the next build. So while this is fast,
some of the speed is a time debt that needs to be repaid in the future.

## Conclusions

If you can require macOS 12+, use `pax`. Filing bugs sometimes helps!

Else, there's no single best way to copy a directory on macOS as far as I can
tell.

If mtimes need to be preserved in the output directory, `cpio -pdlm` seems like
the least bad current option.  If `pax` set the timestamp on the output
directory correctly, it'd be the clear winner.

If mtimes don't need to be preserved, `pax -rwl -p m` seems best if mtime
doesn't need sub-second granularity. Else, `cpio -pdlm` seems best again
(or, if we don't _want_ mimes to be preserved, just `cpio -pdl`). If `pax` set
timestamps with sub-second granularity, it'd be the clear winner again.

Bugs filed:

* [FB8957219 `cp -p` doesn't correctly copy mtime](https://openradar.appspot.com/radar?id=4946596567449600) (fixed in macOS 11.3)
* [FB8957230 `pax -rwl` doesn't correctly set mtime on directories](https://openradar.appspot.com/radar?id=5032029741645824) (fixed in macOS 12)
* [FB8957277 Please add a `-l` flag to `cp`](https://openradar.appspot.com/radar?id=5017815211835392)
* [FB8960643 `pax -rw -p m` sets mtimes with only seconds granularity](https://openradar.appspot.com/radar?id=5046181155569664) (fixed in macOS 12)
* [FB8960652 `touch` doesn't fill in nanoseconds for already-existing files](https://openradar.appspot.com/radar?id=4954148596350976)
