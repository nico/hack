# Quickly copying directories containing large files on macOS

Let's say we have a directory containing a large file:

    mkdir foo
    touch foo/large && shred --iterations=1 --size=1G foo/large

For good measure, let's also throw in a hard link and a symlink:

    ln -s large foo/symlink
    ln foo/large foo/hardlink

We want to copy this directory quickly, and preserve symlinks.
We want to exactly preserve timestamps on the copied files and directory.

We want to only use tools that are part of a macOS default install.

On Linux, we could use `cp -alR`, but that's not supported on macOS.

APFS added high-res time stamps, which can be queried with `stat -f %Fm`:

    % stat -f '%N %Fm' foo
    foo 1609297230.485880489
    % stat -f '%N %Fm' foo/*
    foo/hardlink 1609294944.907726636
    foo/large 1609294944.907726636
    foo/symlink 1609294960.193255375

## cp

Let's try the obvious thing:

    % time (rm -rf bar && cp -aR foo bar)
    0.720 total

That's kind of slow. From `man cp`:

    -R ...
       Note that cp copies hard-linked files as separate files.  If you need to
       preserve hard links, consider using tar(1), cpio(1), or pax(1) instead.

The output directory is 2 GiB, in addition to the 1 GiB input directory.
That explains the slowdown.

Timestamps got almost preserved (they're 

It also chops off the nanoseconds off the timestamps (note trailing zeros):

    % stat -f '%N %Fm' bar
    bar 1609297230.485880000
    % stat -f '%N %Fm' bar/*
    bar/hardlink 1609294944.907726000
    bar/large 1609294944.907726000
    bar/symlink 1609294960.193255000

This means that this method can't be used with mtime-tracking build systems
that read high-resolution timestamps.

Slow and buggy! But the man page at least tells us which alternatives to look
at.

## pax

Has a `-l` switch that tells it to hardlink the outputs to the inputs:

    % time (rm -rf bar && mkdir bar && cd foo && pax -rwl . ../bar)
    0.008 total

The hardlinks mean that this is fast! It also means that the output directory
needs basically no additional storage space in addition to the 1 GiB needed for
the input directory.

And the timestamps at first sight look good (no wonder, these are hardlinks):

    % stat -f '%N %Fm' bar/*
    bar/hardlink 1609294944.907726636
    bar/large 1609294944.907726636
    bar/symlink 1609302462.687747817

But, alas, the timestamp on the directory itself is truncated:

    % stat -f '%N %Fm' bar
    bar 1609297230.000000000

So this is unusable for build system use too.
(And when not using `-l`, the timestamps for all output files are truncated.)

## cpio

Also has a `-l` switch, but takes lists on stdio so it's a bit weird to use:

    % time (rm -rf bar && cd foo && find . | cpio -pdl ../bar)
    0 blocks
    0.012 total

Also fast, but a bit slower due to both `find` and `cpio` having to look at
each file -- and for directories with many files in it, the overhead from that
would be more noticeable.

It sets all timestamps correctly, even on the output directory itself!

    % stat -f '%N %Fm' bar
    bar 1609297230.485880489
    % stat -f '%N %Fm' bar/*
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

    % stat -f '%N %Fm' bar
    bar 1609297230.485880000
    % stat -f '%N %Fm' bar/*
    bar/hardlink 1609294944.907726636
    bar/large 1609294944.907726636
    bar/symlink 1609294960.193255000

Maybe we can relax the requirement to preserve timestamps and just require
that the output files have timestamps that just are not older than the input
timestamps. Then we can use `cp -cR` and this method works.

Another drawback is that if this method is used as part of a build and something
writes the input directory on the next build, the OS has to copy-on-write
all pages when `foo/large` is written on the next build. So while this is fast,
some of the speed is a time debt that needs to be repaid in the future.

## Conclusions

There's no single best way to copy a directory on macOS as far as I can tell.

`cpio` seems like the least bad current option.

If `pax` set the timestamp on the output directory correctly, it'd be the clear
winner.
