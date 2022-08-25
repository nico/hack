Notes on Catalyst
=================

Catalyst is a technology to run iOS apps on macOS.

It consists of several parts:

1. A dedicated mach-o platform for binaries that run on macOS but can
   call iOS code. This is interesting from a toolchain perspective, and
   this is what this document is primarily about.

2. A port of UIKit to mac.

Catalyst apps are built against the macOS SDK.

Normally, Xcode handles the mechanics of building Catalyst apps for you.

This document describes some things that happen under the hood. If you just
want to build a Catalyst app, use Xcode and stop reading. But if you work
on compilers and linkers, or are just interested in what happens under the hood,
this document might be interesting to you.

Building a catalyst commandline tool
------------------------------------

It's possible to write a commandline program that uses catalyst. That's not
something you'd want to do in real life (just write a normal mac commandline
tool), but it's useful for testing.

```
% cat hello.c
#include <stdio.h>

int main() { printf("hello world\n"); }
```

You build a Catalyst binary by using an iOS triple and adding `-macabi` as
the environment part of the triple:

```
% clang hello.cc -target arm64-apple-ios13.1-macabi
```

ld64 automatically ad-hoc codesigns binaries built for macOS, but it doesn't do
this when building for Catalyst (probably because your triple says that you're
building for a variant of iOS, and ad-hoc codesigning doesn't make sense for
iOS, and nobody added the code to do it for Catalyst, or Catalyst is supposed
to be as similar to iOS as possible, or whatever). So you have to do this
manually:

```
% codesign --sign - a.out
```

You can then run it:

```
% ./a.out           
hello
```

(If you forget the codesigning step, you'll instead see:

```
% ./a.out
zsh: killed     ./a.out
% echo $?
137
```

and Console.app will contain:

```
default	11:42:52.753055-0400	kernel	AMFI: hook..execve() killing pid 78760: Attempt to execute completely unsigned code (must be at least ad-hoc signed).
default	11:42:52.753289-0400	kernel	ASP: Security policy would not allow process: 78760, /Users/thakis/src/llvm-project/a.out
```
)

And it's indeed a binary built for Catalyst:

```
% otool -lv a.out | grep platform
 platform MACCATALYST
```

From a `<TargetConditionals.h>` point of view, Catalyst binaries are built as
`TARGET_OS_IOS`. This makes sense, as Catalyst is for taking existing iOS
apps mostly as they are, and running them on macOS.

Building a zippered dylib
-------------------------

It's possible to build dylibs that can be linked into both normal mac apps
and into Catalyst apps. These are called "zippered dylibs" and contain
load commands for both `MAC` and `MACCATALYST` platforms.


Here's an example:

```
% cat foo.cc
const char* foo() { return "foo"; }
```

You build a zippered dylib by passing both a `-target` flag with a mac triple,
and a `-target-variant` flag with a Catalyst triple:

```
% clang -shared foo.cc -o libfoo.dylib \
        -target arm64-apple-macos \
        -target-variant arm64-apple-ios-macabi \
        -Wl,-install_name,@rpath/libfoo.dylib
```

If you use open-source clang instead of Xcode's clang, `-target-variant` is
spelled `-darwin-target-variant` instead.

```
% otool -lv libfoo.dylib | grep platform
 platform MACOS
 platform MACCATALYST
```

From a `<TargetConditionals.h>` point of view, zippered dylibs are built as
`TARGET_OS_OSX`, _not_ as `TARGET_OS_IOS`.

Apple uses zippered dylibs for most of its macOS system libraries, so that it
doesn't have to ship them twice.

A zippered dylib can be linked into a normal mac binary:

```
% cat hello2.c
#include <stdio.h>
const char* foo();
int main() { printf("hello world %s\n", foo()); }

% clang -rpath @executable_path hello2.c libfoo.dylib

% ./a.out                                            
hello world foo

% otool -lv a.out | grep platform       
 platform MACOS
```

And into a Catalyst binary:

```
% clang -target arm64-apple-ios13.1-macabi \
        -rpath @executable_path hello2.c libfoo.dylib

% codesign --sign - a.out

% ./a.out                                            
hello world foo

% otool -lv a.out | grep platform
 platform MACCATALYST
```

SDK, and unzippered twins
-------------------------

Catalyst apps are built against the normal macOS SDK. That SDK is usually
at this path:

```
% xcrun -show-sdk-path
/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
```

Xcode's clang automatically uses that path as default sysroot.

(If you want to use an SDK at some other path for some reason, use `-isysroot`.
Open-source clang doesn't automatically add the path above as default sysroot,
so there you have to pass `-isysroot $(xcrun -show-sdk-path)` to clang.)


Catalyst apps link against three kinds of libraries:
1. Zippered, normal mac libraries.
2. Catalyst-only libraries, which are available for Catalyst apps,
   but not for non-catalyst apps (e.g. UIKit.framework)
3. Libraries that are available to both mac and Catalyst apps,
   but that are slightly different for mac and Catalyst
   (e.g. PDFKit.framework).

Let's look at each of them.

For the first category, libSystem is an example. The example programs further
up called `printf()`, which lives in libSystem. Let's look at it:

```
% head -4 $(xcrun -show-sdk-path)/usr/lib/libSystem.tbd
--- !tapi-tbd
tbd-version:     4
targets:         [ x86_64-macos, x86_64-maccatalyst, arm64-macos, arm64-maccatalyst,
                   arm64e-macos, arm64e-maccatalyst ]
```

This is the text description of libSystem.dylib's interface, and that library
supports linking against it for both macOS and Catalyst. The actual .dylib
can't be seen as it's part of the
[dyld shared cache](https://github.com/keith/dyld-shared-cache-extractor)).
But if you extract it, indeed:

```
 % otool -lv /tmp/libraries/usr/lib/libSystem.B.dylib | grep platform
 platform MACOS
 platform MACCATALYST
```

(A small minority of libraries is _not_ marked as usable in Catalyst apps,
but ld64 still tolerates them if they're only linked implicitly by other
shared libraries. In the macOS 12.0 SDK, CarbonCore.tbd in
CoreServices.framework is an example of such a library.)

For the second category, these are libraries that are below
`/System/iOSSupport` in the SDK. For example,
`$(xcrun -show-sdk-path)/System/iOSSupport/System/Library/Frameworks/UIKit.framework`.

For the third category, these are libraries that exist both in the regular
mac SDK _and_ below `/System/iOSSupport`, but that are different in both
locations, for example:

* `$(xcrun -show-sdk-path)/System/Library/Frameworks/PDFKit.framework`
* `$(xcrun -show-sdk-path)/System/iOSSupport/System/Library/Frameworks/PDFKit.framework`

It's interesting to compare `PDFKit.tbd` in both of these directories.
Libraries in the third category are called "unzippered twins".
