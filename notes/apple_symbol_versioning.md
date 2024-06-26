Symbol versioning on Apple platforms
====================================

Scenarios:

* Extract part of a big library into a standalone framework
  (example PDFKit.framework, extracted from Quartz.framework, see below
  for details.)

* Move a standalone framework into another, existing framework
  (example: Swift runtime moving from libswiftCoreFoundation.dylib to
  Foundation.dylib, <https://github.com/llvm/llvm-project/issues/56074>,
  see below)

* Rename existing framework


The magic `$ld$` symbols
------------------------

Old form:

General form: `$ld$ <action> $ <condition> $ <symbol-name>`

Valid `<action>`s:

* `hide`
* `add`
* `weak`
* `install_name`
* `compatibility_version`

Newer form:

`$ld$previous$ <installname> $ <compatVersion> $ <platformStr> $ <startVersion> $ <endVersion> $ <symbol> $`

`<platformStr>`, `<startVersion>`, and `<endVersion>` are required and are
used to see if this symbol applies to the current output binary.

`<platformStr>` is an integer that has this meaning (from ld64):

```
    macOS               = 1,    // PLATFORM_MACOS
    iOS                 = 2,    // PLATFORM_IOS
    tvOS                = 3,    // PLATFORM_TVOS
    watchOS             = 4,    // PLATFORM_WATCHOS
    bridgeOS            = 5,    // PLATFORM_BRIDGEOS
    iOSMac              = 6,    // PLATFORM_MACCATALYST
    iOS_simulator       = 7,    // PLATFORM_IOSSIMULATOR
    tvOS_simulator      = 8,    // PLATFORM_TVOSSIMULATOR
    watchOS_simulator   = 9,    // PLATFORM_WATCHOSSIMULATOR
    driverKit           = 10,   // PLATFORM_DRIVERKIT
```

`<compatVersion>` can be empty.

`<symbol>` can be empty.

Examples
--------

### PDFKit move

`MacOSX.sdk/System/Library/Frameworks/PDFKit.framework/PDFKit.tbd` contains
several links looking like so:

    '$ld$install_name$os10.13$/System/Library/Frameworks/Quartz.framework/Versions/A/Quartz',
    '$ld$install_name$os10.14$/System/Library/Frameworks/Quartz.framework/Versions/A/Quartz',

This means that when targeting macOS 10.4 (it contains one line each for 10.4,
10.5, ..., to 10.14; I'm showing just two of these lines above) to 10.14
will cause a dependency on Quarts.framework instead of PDFKit.framework when
linking to symbols in PDFKit.framework.

This allowed transparently moving partz of Quartz.framework into a new
framework.

If a binary linked for an older macOS version (which expects PDFKit symbols
in Quartz.framework) runs on a newer macOS version (where the symbols are
in PDFKit.framework), the older binary can run because Quartz.framework
reexports PDDFKit.framework. (Or used to, before the dyld shared libary cache.)

### libswiftFoundation.dylib move into Foundation

`MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/Foundation.framework/Foundation.tbd`
contains tens of thousands of lines looking like so:

    '$ld$previous$/usr/lib/swift/libswiftFoundation.dylib$1.0.0$1$10.15$13.0$_$s10Foundation10CocoaErrorV010formattingC0AC4CodeVvgZ$',

This line sets:

  * `<installname>`: /usr/lib/swift/libswiftFoundation.dylib$
  * `<compatVersion>`: 1.0.0
  * `<platformStr>`: 1 (i.e. macOS)
  * `<startVersion>`: 10.15
  * `<endVersion>`: 13.0
  * `<symbol>`: `_$s10Foundation10CocoaErrorV010formattingC0AC4CodeVvgZ`
                (pass it to `swift demangle` to see the demangled spelling)

It means that when targeting macOS 10.15 to 13.0, the linker will output a
dependency on `/usr/lib/swift/libswiftFoundation.dylib` instead of on
`/System/Library/Frameworks/Foundation.framework` when that symbol is used,
even though it is in Foundation.tbd.

This mechanism makes it possible to move all symbols in libswiftFoundation.dylib
into the Foundation framework in newer macOS versions.

When targeting those newer macOS versions, executables will depend on just
Foundation.framework. When targeting older macOS versions, they will also
depend on the old home of these symbols, and run on older macOS versions.
(Newer macOS versions will either symlink
/usr/lib/swift/libswiftFoundation.dylib to
Foundation.framework/Versions/A/Foundation to be able to run binaries linked
for older macOS versions, or they will contain this information in the
dyld closure.)

### More examples

Look through the output of

    find $(xcrun -show-sdk-path) -name '*.tbd' | xargs rg '\$ld'
