// Generates a folder of small, real H.264 QuickTime clips carrying camera
// metadata (make/model, GPS, creation date) for trying out Clairvo.
// Usage: swift tools/make-sample-footage.swift <output-folder>
import AVFoundation
import CoreVideo
import Foundation

struct Spec {
    let path: String
    let make: String
    let model: String
    let width: Int
    let height: Int
    let fps: Int32
    let location: String?
    let date: String
}

let root = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "SampleFootage")
var specs: [Spec] = []
for i in 1...6 {
    specs.append(Spec(path: String(format: "CardA/C%03d.MOV", i), make: "Sony", model: "ILCE-7M4",
                      width: i % 3 == 0 ? 1920 : 3840, height: i % 3 == 0 ? 1080 : 2160, fps: 30,
                      location: "+42.3736-071.1097+012.000/", date: String(format: "2026-09-12T10:%02d:00-0400", i * 3)))
}
for i in 1...5 {
    specs.append(Spec(path: String(format: "iPhone/IMG_%04d.MOV", 4100 + i), make: "Apple", model: "iPhone 15 Pro",
                      width: 3840, height: 2160, fps: 30,
                      location: i % 2 == 0 ? "+42.3770-071.1167+010.000/" : "+42.3601-071.0589+005.000/",
                      date: String(format: "2026-09-13T14:%02d:00-0400", i * 5)))
}
for i in 1...3 {
    specs.append(Spec(path: String(format: "DJI_%04d.MP4", 20 + i), make: "DJI", model: "Mini 4 Pro",
                      width: 1920, height: 1080, fps: 30, location: nil,
                      date: String(format: "2026-09-14T09:%02d:00Z", i * 7)))
}
specs.append(Spec(path: "misc/unknown_camera.mov", make: "", model: "", width: 1280, height: 720, fps: 24,
                  location: nil, date: ""))

func write(_ spec: Spec) throws {
    let url = root.appendingPathComponent(spec.path)
    try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try? FileManager.default.removeItem(at: url)
    let writer = try AVAssetWriter(outputURL: url, fileType: .mov)
    var items: [AVMetadataItem] = []
    func add(_ id: AVMetadataIdentifier, _ value: String?) {
        guard let value, !value.isEmpty else { return }
        let item = AVMutableMetadataItem()
        item.identifier = id
        item.value = value as NSString
        items.append(item)
    }
    add(.quickTimeMetadataMake, spec.make)
    add(.quickTimeMetadataModel, spec.model)
    add(.quickTimeMetadataLocationISO6709, spec.location)
    add(.quickTimeMetadataCreationDate, spec.date)
    writer.metadata = items

    // Flat frames keep even 4K samples tiny.
    let w = spec.width, h = spec.height
    let input = AVAssetWriterInput(mediaType: .video, outputSettings: [
        AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: w, AVVideoHeightKey: h,
    ])
    input.expectsMediaDataInRealTime = false
    let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: [
        kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        kCVPixelBufferWidthKey as String: w, kCVPixelBufferHeightKey as String: h,
    ])
    writer.add(input)
    writer.startWriting()
    writer.startSession(atSourceTime: .zero)
    let frames = Int(spec.fps) * 2
    for f in 0..<frames {
        while !input.isReadyForMoreMediaData { Thread.sleep(forTimeInterval: 0.002) }
        var buffer: CVPixelBuffer?
        CVPixelBufferPoolCreatePixelBuffer(nil, adaptor.pixelBufferPool!, &buffer)
        guard let buffer else { continue }
        CVPixelBufferLockBaseAddress(buffer, [])
        memset(CVPixelBufferGetBaseAddress(buffer), Int32((f * 4) & 0xFF), CVPixelBufferGetDataSize(buffer))
        CVPixelBufferUnlockBaseAddress(buffer, [])
        adaptor.append(buffer, withPresentationTime: CMTime(value: CMTimeValue(f), timescale: spec.fps))
    }
    input.markAsFinished()
    let done = DispatchSemaphore(value: 0)
    writer.finishWriting { done.signal() }
    done.wait()
    if writer.status != .completed { throw writer.error ?? NSError(domain: "write", code: 1) }
}

for spec in specs {
    try write(spec)
    print("wrote \(spec.path)")
}
try "Notes about the shoot".write(to: root.appendingPathComponent("CardA/notes.txt"), atomically: true, encoding: .utf8)
print("Sample footage in \(root.path)")
