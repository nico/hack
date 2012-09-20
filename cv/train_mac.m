#import <Cocoa/Cocoa.h>

const int kSize = 16;

void save(NSBitmapImageRep* bitmapRep, NSString* name) {
  [[bitmapRep representationUsingType:NSPNGFileType properties:nil]
      writeToFile:name atomically:YES];
}

NSBitmapImageRep* chr(int num) {
  NSBitmapImageRep* rep = [[[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:NULL
                    pixelsWide:kSize
                    pixelsHigh:kSize
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
  NSFont* font = [NSFont fontWithName:@"Helvetica" size:kSize];
  NSDictionary* attributes = @{
    NSFontAttributeName: font,
    NSParagraphStyleAttributeName: style,
  };
  NSRect txrect = imgrect;
  txrect.origin.y = (NSHeight(imgrect) - [font capHeight]) / 2;
  [[NSString stringWithFormat:@"%d", num]
      drawInRect:txrect withAttributes:attributes];

  [NSGraphicsContext restoreGraphicsState];
  return rep;
}

int main() {
  @autoreleasepool {
    NSApplicationLoad();
    save(chr(4), @"test.png");
  }
}
