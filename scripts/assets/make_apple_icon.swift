// Preserve the existing Blitz artwork; only frame it for classic macOS icons.
// Run with: xcrun swift -module-cache-path out/macos/swift-cache \
//   scripts/assets/make_apple_icon.swift
import AppKit

let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let source = root.appendingPathComponent("assets/icon/blitz-apple-source.png")
guard let artwork = NSImage(contentsOf: source) else { fatalError("Missing blitz-apple-source.png") }

func writeIcon(_ filename: String, macOS: Bool) throws {
    guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 1024,
        pixelsHigh: 1024, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
        isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0),
        let context = NSGraphicsContext(bitmapImageRep: bitmap) else {
        fatalError("Cannot create icon bitmap")
    }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    context.imageInterpolation = .high
    let frame = macOS ? NSRect(x: 100, y: 100, width: 824, height: 824)
                      : NSRect(x: 0, y: 0, width: 1024, height: 1024)
    if macOS {
        NSBezierPath(roundedRect: frame, xRadius: 180, yRadius: 180).addClip()
    }
    artwork.draw(in: frame, from: .zero, operation: .copy, fraction: 1)
    if macOS {
        let border = NSBezierPath(roundedRect: frame.insetBy(dx: 1, dy: 1),
                                  xRadius: 179, yRadius: 179)
        NSColor(white: 0, alpha: 0.4).setStroke()
        border.lineWidth = 2
        border.stroke()
    }
    context.flushGraphics()
    NSGraphicsContext.restoreGraphicsState()
    var output = bitmap
    if !macOS {
        // iOS app icons must be opaque and have no alpha channel in the PNG.
        guard let rgb = CGContext(data: nil, width: 1024, height: 1024,
            bitsPerComponent: 8, bytesPerRow: 4096, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue), let input = bitmap.cgImage else {
            fatalError("Cannot flatten square icon")
        }
        rgb.draw(input, in: CGRect(x: 0, y: 0, width: 1024, height: 1024))
        guard let flattened = rgb.makeImage() else { fatalError("Cannot export square icon") }
        output = NSBitmapImageRep(cgImage: flattened)
    }
    guard let png = output.representation(using: .png, properties: [:]) else {
        fatalError("Cannot encode icon PNG")
    }
    try png.write(to: root.appendingPathComponent("assets/icon/" + filename))
}

try writeIcon("blitz-macos.png", macOS: true)
// iOS applies its own mask. Keep an opaque, square copy for iOS packaging.
try writeIcon("blitz-apple.png", macOS: false)
