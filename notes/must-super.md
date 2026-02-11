Requiring calls to superclass methods in overridden methods
===========================================================

Android: `@CallSuper` annotation

Objective-C: `NS_REQUIRES_SUPER`

Dart: `@mustCallSuper`

Ladybird one-off `MUST_UPCALL`:
<https://github.com/LadybirdBrowser/ladybird/pull/7773/>

Not available in non-Android Java, C++, C#, Swift.

For Swift, it's sometimes requested:
* <https://forums.swift.org/t/discussion-enforcing-calling-super/1470>
* <https://forums.swift.org/t/super-call-enforcement/38177> /
  <https://github.com/swiftlang/swift-evolution/pull/1167>
