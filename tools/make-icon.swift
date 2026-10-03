// Renders macos/Clairvo/Resources/AppIcon.icns.
// Usage: swift tools/make-icon.swift <output.icns>
import AppKit

let output = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "AppIcon.icns"
let iconset = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("Clairvo.iconset")
try? FileManager.default.removeItem(at: iconset)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)

func render(_ size: Int) -> Data {
    let s = CGFloat(size)
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let inset = s * 0.1
    let rect = NSRect(x: inset, y: inset, width: s - 2 * inset, height: s - 2 * inset)
    let shape = NSBezierPath(roundedRect: rect, xRadius: s * 0.18, yRadius: s * 0.18)
    NSGradient(colors: [NSColor(calibratedRed: 0.10, green: 0.12, blue: 0.20, alpha: 1),
                        NSColor(calibratedRed: 0.18, green: 0.24, blue: 0.42, alpha: 1)])!
        .draw(in: shape, angle: 90)

    // Three stacked "clips" fanning into a tree.
    let accent = NSColor(calibratedRed: 0.36, green: 0.78, blue: 0.95, alpha: 1)
    let cardW = s * 0.42, cardH = s * 0.13
    for i in 0..<3 {
        let y = s * 0.56 - CGFloat(i) * s * 0.17
        let x = s * 0.40 + CGFloat(i) * s * 0.02
        let card = NSBezierPath(roundedRect: NSRect(x: x, y: y, width: cardW, height: cardH), xRadius: s * 0.025, yRadius: s * 0.025)
        accent.withAlphaComponent(1.0 - CGFloat(i) * 0.22).setFill()
        card.fill()
        // sprocket holes
        NSColor(calibratedWhite: 0.1, alpha: 0.55).setFill()
        for h in 0..<4 {
            NSBezierPath(rect: NSRect(x: x + s * 0.03 + CGFloat(h) * cardW / 4, y: y + cardH * 0.62,
                                      width: cardW * 0.1, height: cardH * 0.2)).fill()
        }
        // connector line to the trunk
        let line = NSBezierPath()
        line.move(to: NSPoint(x: s * 0.27, y: y + cardH / 2))
        line.line(to: NSPoint(x: x, y: y + cardH / 2))
        line.lineWidth = max(1, s * 0.018)
        NSColor.white.withAlphaComponent(0.85).setStroke()
        line.stroke()
    }
    let trunk = NSBezierPath()
    trunk.move(to: NSPoint(x: s * 0.27, y: s * 0.72))
    trunk.line(to: NSPoint(x: s * 0.27, y: s * 0.22 + s * 0.065))
    trunk.lineWidth = max(1, s * 0.018)
    trunk.stroke()
    NSColor.white.setFill()
    NSBezierPath(ovalIn: NSRect(x: s * 0.27 - s * 0.045, y: s * 0.72 - s * 0.02, width: s * 0.09, height: s * 0.09)).fill()
    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

for base in [16, 32, 128, 256, 512] {
    try render(base).write(to: iconset.appendingPathComponent("icon_\(base)x\(base).png"))
    try render(base * 2).write(to: iconset.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}

let task = Process()
task.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
task.arguments = ["-c", "icns", iconset.path, "-o", output]
try task.run()
task.waitUntilExit()
print(task.terminationStatus == 0 ? "Wrote \(output)" : "iconutil failed")
