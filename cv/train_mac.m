#import <Cocoa/Cocoa.h>

void save(NSBitmapImageRep* bitmapRep, NSString* name) {
  //NSSize s = [img size];
  //[img lockFocus];
  //NSBitmapImageRep* bitmapRep = [[NSBitmapImageRep alloc]
      //initWithFocusedViewRect:NSMakeRect(0, 0, s.width, s.height)];
  //[img unlockFocus];

  [[bitmapRep representationUsingType:NSPNGFileType properties:nil]
      writeToFile:name atomically:YES];

  //[bitmapRep release];
}

void i() {
  NSBitmapImageRep* r = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:NULL
                    pixelsWide:16
                    pixelsHigh:16
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace
                  bitmapFormat:0
                   bytesPerRow:0
                  bitsPerPixel:0];

  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext* context = [NSGraphicsContext
    graphicsContextWithBitmapImageRep:r];
  [context setShouldAntialias:YES];
  [context setImageInterpolation:NSImageInterpolationHigh];
  [NSGraphicsContext setCurrentContext:context];

  // ...

  [NSGraphicsContext restoreGraphicsState];

  save(r, @"test.png");
  [r release];
}

int main() {
  NSApplicationLoad();
  i();
}
