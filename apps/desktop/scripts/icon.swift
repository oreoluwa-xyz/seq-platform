import AppKit

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let folder = root.appendingPathComponent("build/SEQ.iconset")
try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let pixels = size * scale
        let image = NSImage(size: NSSize(width: pixels, height: pixels))
        image.lockFocus()
        let transform = NSAffineTransform()
        transform.scale(by: CGFloat(pixels) / 1024)
        transform.concat()
        NSColor(calibratedRed: 0.957, green: 0.953, blue: 0.929, alpha: 1).setFill()
        NSBezierPath(roundedRect: NSRect(x: 62, y: 62, width: 900, height: 900), xRadius: 200, yRadius: 200).fill()
        NSColor(calibratedRed: 0.96, green: 0.32, blue: 0.16, alpha: 1).setFill()
        for (x, height) in [(270, 490), (440, 350), (610, 215)] {
            let bar = NSBezierPath()
            bar.move(to: NSPoint(x: x, y: 270))
            bar.line(to: NSPoint(x: x + 98, y: 270))
            bar.line(to: NSPoint(x: x + 98 + height / 5, y: 270 + height))
            bar.line(to: NSPoint(x: x + height / 5, y: 270 + height))
            bar.close(); bar.fill()
        }
        image.unlockFocus()
        let rep = NSBitmapImageRep(data: image.tiffRepresentation!)!
        let name = "icon_\(size)x\(size)\(scale == 2 ? "@2x" : "").png"
        try rep.representation(using: .png, properties: [:])!.write(to: folder.appendingPathComponent(name))
    }
}
let process = Process()
process.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
process.arguments = ["-c", "icns", folder.path, "-o", root.appendingPathComponent("build/SEQ.icns").path]
try process.run(); process.waitUntilExit()
if process.terminationStatus != 0 { exit(process.terminationStatus) }
