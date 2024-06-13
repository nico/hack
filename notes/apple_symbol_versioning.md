Symbol versioning on Apple platforms
====================================

Scenarios:

* Extract part of a big library into a standalone framework
  (example PDFKit.framework, extracted from XXX)

* Move a standalone framework into another, existing framework
  (example: Swift runtime moving into XXX)

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

XXX
