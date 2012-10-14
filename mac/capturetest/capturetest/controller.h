//
//  controller.h
//  capturetest
//
//  Created by test on 12/3/11.
//  Copyright (c) 2011 __MyCompanyName__. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <QTKit/QTKit.h>

@interface controller : NSObject {
  IBOutlet QTCaptureView* capture_;
  QTCaptureSession* session_;
  QTCaptureDeviceInput* deviceInput_;

  // Or QTCaptureDecompressedVideoOutput, see docs.
  QTCaptureVideoPreviewOutput* output_;
}

@end
