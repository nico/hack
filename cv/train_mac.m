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
  NSBitmapImageRep* rep = [[[NSBitmapImageRep alloc]
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
                  bitsPerPixel:0] autorelease];

  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext* context = [NSGraphicsContext
    graphicsContextWithBitmapImageRep:rep];
  [context setShouldAntialias:YES];
  [context setImageInterpolation:NSImageInterpolationHigh];
  [NSGraphicsContext setCurrentContext:context];

  [[NSColor whiteColor] set];
  NSRect imgrect = NSMakeRect(0, 0, [rep size].width, [rep size].height);
  NSRectFill(imgrect);

  NSMutableParagraphStyle* style =
      [[[NSMutableParagraphStyle alloc] init] autorelease];
  [style setAlignment:NSCenterTextAlignment];
  NSFont* font = [NSFont fontWithName:@"Helvetica" size:16.0];
  NSDictionary* attributes = @{
    NSFontAttributeName: font,
    NSParagraphStyleAttributeName: style,
  };
  NSRect txrect = imgrect;
  txrect.origin.y = (NSHeight(imgrect) - [font capHeight]) / 2;
  [@"3" drawInRect:txrect withAttributes:attributes];

  [NSGraphicsContext restoreGraphicsState];

  save(rep, @"test.png");
}

int main() {
  @autoreleasepool {
    NSApplicationLoad();
    i();
  }
}
