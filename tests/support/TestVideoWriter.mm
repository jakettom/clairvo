#include "support/TestSupport.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

namespace vo::test {

bool writeTestMovie(const std::string& path, int width, int height, const std::string& make, const std::string& model,
                    const std::string& iso6709, const std::string& creationDate) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        NSError* error = nil;
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeQuickTimeMovie error:&error];
        if (!writer) return false;

        NSMutableArray<AVMetadataItem*>* items = [NSMutableArray array];
        auto addItem = [&](NSString* identifier, const std::string& value) {
            if (value.empty()) return;
            AVMutableMetadataItem* item = [AVMutableMetadataItem metadataItem];
            item.identifier = identifier;
            item.value = [NSString stringWithUTF8String:value.c_str()];
            [items addObject:item];
        };
        addItem(AVMetadataIdentifierQuickTimeMetadataMake, make);
        addItem(AVMetadataIdentifierQuickTimeMetadataModel, model);
        addItem(AVMetadataIdentifierQuickTimeMetadataLocationISO6709, iso6709);
        addItem(AVMetadataIdentifierQuickTimeMetadataCreationDate, creationDate);
        writer.metadata = items;

        NSDictionary* settings = @{
            AVVideoCodecKey : AVVideoCodecTypeH264,
            AVVideoWidthKey : @(width),
            AVVideoHeightKey : @(height),
        };
        AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                                       outputSettings:settings];
        input.expectsMediaDataInRealTime = NO;
        NSDictionary* attrs = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey : @(width),
            (id)kCVPixelBufferHeightKey : @(height),
        };
        AVAssetWriterInputPixelBufferAdaptor* adaptor =
            [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                                                             sourcePixelBufferAttributes:attrs];
        if (![writer canAddInput:input]) return false;
        [writer addInput:input];
        if (![writer startWriting]) return false;
        [writer startSessionAtSourceTime:kCMTimeZero];

        const int frames = 30;
        for (int i = 0; i < frames; ++i) {
            while (!input.readyForMoreMediaData) [NSThread sleepForTimeInterval:0.005];
            CVPixelBufferRef buffer = nullptr;
            CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor.pixelBufferPool, &buffer);
            if (!buffer) return false;
            CVPixelBufferLockBaseAddress(buffer, 0);
            memset(CVPixelBufferGetBaseAddress(buffer), (i * 8) & 0xFF, CVPixelBufferGetDataSize(buffer));
            CVPixelBufferUnlockBaseAddress(buffer, 0);
            [adaptor appendPixelBuffer:buffer withPresentationTime:CMTimeMake(i, 30)];
            CVPixelBufferRelease(buffer);
        }
        [input markAsFinished];
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
          dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
        return writer.status == AVAssetWriterStatusCompleted;
    }
}

} // namespace vo::test
