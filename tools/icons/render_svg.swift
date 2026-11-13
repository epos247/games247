// Usage: swift render_svg.swift <input.svg> <size> <output.png> [crop]
// Renders an SVG to a transparent square PNG at the given pixel size.
// With "crop", the 100px margin of the 1024 icon grid is trimmed so the tile fills the image.
import AppKit

let args = CommandLine.arguments
guard args.count >= 4, let size = Int(args[2]),
      let image = NSImage(contentsOf: URL(fileURLWithPath: args[1])) else {
    FileHandle.standardError.write("usage: render_svg.swift in.svg size out.png [crop]\n".data(using: .utf8)!)
    exit(1)
}
let crop = args.count > 4 && args[4] == "crop"

let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                           bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                           colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
rep.size = NSSize(width: size, height: size)
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
NSGraphicsContext.current?.imageInterpolation = .high
let s = CGFloat(size)
let src = crop ? NSRect(x: 100, y: 100, width: 824, height: 824)
               : NSRect(x: 0, y: 0, width: 1024, height: 1024)
image.size = NSSize(width: 1024, height: 1024)
image.draw(in: NSRect(x: 0, y: 0, width: s, height: s), from: src, operation: .copy, fraction: 1.0)
NSGraphicsContext.restoreGraphicsState()
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[3]))
