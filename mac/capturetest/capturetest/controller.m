//
//  controller.m
//  capturetest
//
//  Created by test on 12/3/11.
//  Copyright (c) 2011 __MyCompanyName__. All rights reserved.
//

#import "controller.h"

// http://developer.apple.com/library/mac/#samplecode/StillMotion/Listings/MyDocument_m.html#//apple_ref/doc/uid/DTS10004355-MyDocument_m-DontLinkElementID_4
// http://developer.apple.com/library/mac/#samplecode/LiveVideoMixer3/Listings/CaptureChannel_m.html#//apple_ref/doc/uid/DTS10004044-CaptureChannel_m-DontLinkElementID_4

@implementation controller

- (void)awakeFromNib {
  session_ = [[QTCaptureSession alloc] init];

  QTCaptureDevice *device = [QTCaptureDevice defaultInputDeviceWithMediaType:QTMediaTypeVideo];

  NSError* error;
  BOOL success = [device open:&error];
  if (!success) {
    [[NSAlert alertWithError:error] runModal];
    return;
  }

  deviceInput_ = [[QTCaptureDeviceInput alloc] initWithDevice:device];
  success = [session_ addInput:deviceInput_ error:&error];
  if (!success) {
    [[NSAlert alertWithError:error] runModal];
    return;
  }

  output_ = [[QTCaptureVideoPreviewOutput alloc] init];
  [output_ setDelegate:self];
  success = [session_ addOutput:output_ error:&error];
  if (!success) {
    [[NSAlert alertWithError:error] runModal];
    return;
  }
  
  // Preview the video from the session in the document window
  //[capture_ setDelegate:self];  // This can be used to modify the CIImage right before it's displayed
  [capture_ setCaptureSession:session_];
  
  // Start the session
  [session_ startRunning];
}

// This delegate method is called whenever the QTCaptureVideoPreviewOutput receives a frame
- (void)captureOutput:(QTCaptureOutput*)captureOutput
  didOutputVideoFrame:(CVImageBufferRef)videoFrame
     withSampleBuffer:(QTSampleBuffer*)sampleBuffer
       fromConnection:(QTCaptureConnection*)connection {
  // Store the latest frame
  // This must be done in a @synchronized block because this delegate method is not called on the main thread
//  CVImageBufferRef imageBufferToRelease;
//  
//  CVBufferRetain(videoFrame);
//  
//  @synchronized (self) {
//    imageBufferToRelease = mCurrentImageBuffer;
//    mCurrentImageBuffer = videoFrame;
//  }
//  
//  CVBufferRelease(imageBufferToRelease);
}

// A QTCaptureView delegate function to change an image before it's displayed.
- (CIImage*)view:(QTCaptureView*)view willDisplayImage:(CIImage*)image {
  CIFilter* filter = [CIFilter filterWithName:@"CIGaussianBlur"];
  [filter setDefaults];
  [filter setValue:image forKey:@"inputImage"];
  return [filter valueForKey:@"outputImage"];
  //return nil;  // No change.
}

@end
