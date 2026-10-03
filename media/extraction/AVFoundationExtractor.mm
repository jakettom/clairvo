#include "media/extraction/MetadataExtractor.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <string>

namespace vo::media {

namespace {

std::string toStd(NSString* s) { return s ? std::string([s UTF8String] ?: "") : std::string(); }

std::string fourCC(FourCharCode code) {
    char c[5] = {static_cast<char>((code >> 24) & 0xFF), static_cast<char>((code >> 16) & 0xFF),
                 static_cast<char>((code >> 8) & 0xFF), static_cast<char>(code & 0xFF), 0};
    return std::string(c);
}

NSString* isoDate(NSDate* date) {
    static NSISO8601DateFormatter* fmt = [] {
        NSISO8601DateFormatter* f = [[NSISO8601DateFormatter alloc] init];
        f.formatOptions = NSISO8601DateFormatWithInternetDateTime;
        return f;
    }();
    @synchronized(fmt) {
        return [fmt stringFromDate:date];
    }
}

// Extraction runs on worker threads, so async AVFoundation loads are awaited.
void waitOn(dispatch_semaphore_t sem) { dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)); }

std::string valueString(AVMetadataItem* item) {
    id value = item.value;
    if ([value isKindOfClass:[NSString class]]) return toStd((NSString*)value);
    if ([value isKindOfClass:[NSNumber class]]) return toStd([(NSNumber*)value stringValue]);
    if ([value isKindOfClass:[NSDate class]]) return toStd(isoDate((NSDate*)value));
    if (item.stringValue) return toStd(item.stringValue);
    return "";
}

void appendItems(NSArray<AVMetadataItem*>* items, const std::string& source, std::vector<RawMetadataEntry>& out) {
    for (AVMetadataItem* item in items) {
        NSString* identifier = item.identifier;
        std::string key = identifier ? toStd(identifier) : "";
        if (key.empty() && [item.key isKindOfClass:[NSString class]]) key = toStd((NSString*)item.key);
        if (key.empty()) continue;
        std::string value = valueString(item);
        if (value.empty()) continue;
        out.push_back({key, value, source});
    }
}

class AVFoundationExtractor final : public MetadataExtractor {
public:
    std::vector<RawMetadataEntry> extract(const std::string& absolutePath) override {
        std::vector<RawMetadataEntry> out;
        @autoreleasepool {
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:absolutePath.c_str()]];
            AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url
                                                    options:@{AVURLAssetPreferPreciseDurationAndTimingKey : @NO}];

            CMTime duration = kCMTimeInvalid;
            NSArray<AVMetadataItem*>* metadata = nil;
            __block NSArray<AVAssetTrack*>* videoTracks = nil;
            AVMetadataItem* creationDate = nil;

            dispatch_semaphore_t assetSem = dispatch_semaphore_create(0);
            [asset loadValuesAsynchronouslyForKeys:@[ @"duration", @"metadata", @"creationDate" ]
                                 completionHandler:^{
                                   dispatch_semaphore_signal(assetSem);
                                 }];
            waitOn(assetSem);
            NSError* err = nil;
            if ([asset statusOfValueForKey:@"duration" error:&err] == AVKeyValueStatusLoaded) duration = asset.duration;
            if ([asset statusOfValueForKey:@"metadata" error:&err] == AVKeyValueStatusLoaded) metadata = asset.metadata;
            if ([asset statusOfValueForKey:@"creationDate" error:&err] == AVKeyValueStatusLoaded)
                creationDate = asset.creationDate;

            dispatch_semaphore_t tracksSem = dispatch_semaphore_create(0);
            [asset loadTracksWithMediaType:AVMediaTypeVideo
                         completionHandler:^(NSArray<AVAssetTrack*>* tracks, NSError* error) {
                           videoTracks = tracks;
                           dispatch_semaphore_signal(tracksSem);
                         }];
            waitOn(tracksSem);

            if (CMTIME_IS_NUMERIC(duration)) {
                double secs = CMTimeGetSeconds(duration);
                if (std::isfinite(secs)) out.push_back({"asset.duration", std::to_string(secs), "avfoundation"});
            }
            if (creationDate) {
                NSDate* d = creationDate.dateValue;
                std::string v = d ? toStd(isoDate(d)) : toStd(creationDate.stringValue);
                if (!v.empty()) out.push_back({"asset.creationDate", v, "avfoundation"});
            }
            if (metadata) appendItems(metadata, "container", out);

            AVAssetTrack* track = videoTracks.firstObject;
            if (track) {
                CGSize size = CGSizeZero;
                CGAffineTransform transform = CGAffineTransformIdentity;
                float fps = 0;
                NSArray* formats = nil;
                NSArray<AVMetadataItem*>* trackMetadata = nil;
                dispatch_semaphore_t trackSem = dispatch_semaphore_create(0);
                [track loadValuesAsynchronouslyForKeys:@[
                    @"naturalSize", @"preferredTransform", @"nominalFrameRate", @"formatDescriptions", @"metadata"
                ]
                                     completionHandler:^{
                                       dispatch_semaphore_signal(trackSem);
                                     }];
                waitOn(trackSem);
                if ([track statusOfValueForKey:@"naturalSize" error:&err] == AVKeyValueStatusLoaded) size = track.naturalSize;
                if ([track statusOfValueForKey:@"preferredTransform" error:&err] == AVKeyValueStatusLoaded)
                    transform = track.preferredTransform;
                if ([track statusOfValueForKey:@"nominalFrameRate" error:&err] == AVKeyValueStatusLoaded)
                    fps = track.nominalFrameRate;
                if ([track statusOfValueForKey:@"formatDescriptions" error:&err] == AVKeyValueStatusLoaded)
                    formats = track.formatDescriptions;
                if ([track statusOfValueForKey:@"metadata" error:&err] == AVKeyValueStatusLoaded)
                    trackMetadata = track.metadata;

                if (size.width > 0 && size.height > 0) {
                    out.push_back({"track.video.width", std::to_string(std::lround(size.width)), "avfoundation"});
                    out.push_back({"track.video.height", std::to_string(std::lround(size.height)), "avfoundation"});
                    double degrees = std::atan2(transform.b, transform.a) * 180.0 / M_PI;
                    out.push_back({"track.video.rotation", std::to_string(std::lround(degrees)), "avfoundation"});
                }
                if (fps > 0) out.push_back({"track.video.frameRate", std::to_string(fps), "avfoundation"});
                if (formats.count > 0) {
                    CMFormatDescriptionRef desc = (__bridge CMFormatDescriptionRef)formats.firstObject;
                    out.push_back({"track.video.codec", fourCC(CMFormatDescriptionGetMediaSubType(desc)), "avfoundation"});
                }
                if (trackMetadata) appendItems(trackMetadata, "track", out);
            }
        }
        return out;
    }
};

} // namespace

std::shared_ptr<MetadataExtractor> makeAVFoundationExtractor() { return std::make_shared<AVFoundationExtractor>(); }

} // namespace vo::media
