// SPDX-License-Identifier: MIT
// Renders the animated neon grid floor for the games247 "Neon" theme as a looping GIF.
// Usage: swift make_floor.swift <output.gif>
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let W = 960, H = 360
let frames = 30
let delay = 0.04 // seconds per frame: one grid row passes every 1.2 seconds
let out = URL(fileURLWithPath: CommandLine.arguments[1])

// Camera looking at the horizon (y = 0): a ground point at depth z appears at y ~ 1 / z.
let rows = 40
let cx = Double(W) / 2.0

func color(_ r: Double, _ g: Double, _ b: Double, _ a: Double) -> CGColor {
    CGColor(srgbRed: r, green: g, blue: b, alpha: a)
}

guard let dest = CGImageDestinationCreateWithURL(out as CFURL, UTType.gif.identifier as CFString,
                                                 frames, nil) else { fatalError("no GIF writer") }
CGImageDestinationSetProperties(dest, [kCGImagePropertyGIFDictionary:
    [kCGImagePropertyGIFLoopCount: 0]] as CFDictionary)

for frame in 0..<frames {
    let ctx = CGContext(data: nil, width: W, height: H, bitsPerComponent: 8, bytesPerRow: 0,
                        space: CGColorSpace(name: CGColorSpace.sRGB)!,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    // Flip so y grows downwards from the horizon.
    ctx.translateBy(x: 0, y: CGFloat(H))
    ctx.scaleBy(x: 1, y: -1)

    // Ground: glowing magenta at the horizon fading to near black.
    let ground = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB)!,
                            colors: [color(0.55, 0.08, 0.42, 1), color(0.16, 0.03, 0.22, 1),
                                     color(0.04, 0.0, 0.09, 1)] as CFArray,
                            locations: [0, 0.18, 1])!
    ctx.drawLinearGradient(ground, start: .zero, end: CGPoint(x: 0, y: H), options: [])

    func stroke(_ path: CGPath, _ width: Double, _ alpha: Double) {
        // A wide faint pass for the glow, then the bright core.
        ctx.setLineCap(.round)
        ctx.addPath(path); ctx.setStrokeColor(color(1, 0.24, 0.55, alpha * 0.22))
        ctx.setLineWidth(width * 4); ctx.strokePath()
        ctx.addPath(path); ctx.setStrokeColor(color(1, 0.36, 0.66, alpha))
        ctx.setLineWidth(width); ctx.strokePath()
    }

    // Lines running into the distance, all meeting at the vanishing point.
    for i in -12...12 {
        let xBottom = cx + Double(i) * Double(W) * 0.14
        let p = CGMutablePath()
        p.move(to: CGPoint(x: cx + (xBottom - cx) * 0.02, y: 0))
        p.addLine(to: CGPoint(x: xBottom, y: Double(H)))
        stroke(p, 2.2, 0.85)
    }

    // Cross lines, evenly spaced on the ground and moving towards the viewer. Row k sits at
    // depth 1 + k - t, so after a full loop (t = 1) every row has taken the place of the one
    // in front of it and the animation repeats seamlessly.
    let t = Double(frame) / Double(frames)
    for k in 0..<rows {
        let z = 1.0 + Double(k) - t
        let y = Double(H) * 1.15 / z
        if y > Double(H) + 6 || y < 9 { continue }
        let nearness = min(y / Double(H), 1)
        let p = CGMutablePath()
        p.move(to: CGPoint(x: 0, y: y)); p.addLine(to: CGPoint(x: Double(W), y: y))
        stroke(p, 0.8 + 2.6 * nearness, 0.3 + 0.7 * nearness)
    }

    // Bright horizon line.
    let hp = CGMutablePath()
    hp.move(to: CGPoint(x: 0, y: 1)); hp.addLine(to: CGPoint(x: Double(W), y: 1))
    stroke(hp, 3, 1)

    CGImageDestinationAddImage(dest, ctx.makeImage()!, [kCGImagePropertyGIFDictionary:
        [kCGImagePropertyGIFDelayTime: delay, kCGImagePropertyGIFUnclampedDelayTime: delay]]
        as CFDictionary)
}
guard CGImageDestinationFinalize(dest) else { fatalError("couldn't write GIF") }
