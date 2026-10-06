// Drives QuickLookThumbnailing the same way Finder does, then classifies the
// result by colour. The spike thumbnail extension paints (0.90, 0.10, 0.10),
// so a red result means the extension was invoked and an anything-else result
// means the system handled the file itself.
import AppKit
import CoreGraphics
import Foundation
import ImageIO
import QuickLookThumbnailing

// The extensions render real images now, so there is no fixed colour to look
// for. What this can still tell us is whether Quick Look returned a real
// thumbnail at all, and at what size and coverage. Whether *our* extension
// produced it is answered by the unified log, not by pixels.

/// Returns the alpha-weighted mean colour and the alpha coverage. Quick Look
/// letterboxes a reply into the requested canvas, so a raw mean over the whole
/// image is diluted by transparent padding and must not be compared directly.
func sample(_ image: CGImage) -> (r: Double, g: Double, b: Double, coverage: Double)? {
    let w = 16, h = 16
    var buf = [UInt8](repeating: 0, count: w * h * 4)
    guard let ctx = CGContext(data: &buf, width: w, height: h,
                              bitsPerComponent: 8, bytesPerRow: w * 4,
                              space: CGColorSpace(name: CGColorSpace.sRGB)!,
                              bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else { return nil }
    ctx.clear(CGRect(x: 0, y: 0, width: w, height: h))
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
    var r = 0.0, g = 0.0, b = 0.0, a = 0.0
    for i in stride(from: 0, to: buf.count, by: 4) {
        // Buffer is premultiplied, so summing then dividing by summed alpha
        // recovers the un-premultiplied mean over covered pixels.
        r += Double(buf[i]); g += Double(buf[i + 1])
        b += Double(buf[i + 2]); a += Double(buf[i + 3])
    }
    guard a > 0 else { return (0, 0, 0, 0) }
    return (r / a, g / a, b / a, a / (Double(w * h) * 255.0))
}

func writePNG(_ image: CGImage, to url: URL) {
    guard let dest = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil)
    else { return }
    CGImageDestinationAddImage(dest, image, nil)
    CGImageDestinationFinalize(dest)
}

func classify(_ c: (r: Double, g: Double, b: Double, coverage: Double)) -> String {
    if c.coverage <= 0.01 { return "EMPTY — nothing rendered" }
    if c.coverage > 0.995 { return "rendered, fills the context" }
    return String(format: "rendered, letterboxed (%.0f%% coverage)", c.coverage * 100)
}

func typeName(_ t: QLThumbnailRepresentation.RepresentationType) -> String {
    switch t {
    case .icon: return "icon"
    case .lowQualityThumbnail: return "lowQualityThumbnail"
    case .thumbnail: return "thumbnail"
    @unknown default: return "unknown(\(t.rawValue))"
    }
}

let paths = Array(CommandLine.arguments.dropFirst())
guard !paths.isEmpty else {
    FileHandle.standardError.write(Data("usage: qlprobe <file>...\n".utf8)); exit(2)
}

let sizes: [(CGFloat, CGFloat)] = [(64, 64), (512, 512)]
let group = DispatchGroup()
var lines: [String] = []
let lock = NSLock()

for path in paths {
    let url = URL(fileURLWithPath: path)
    for (w, h) in sizes {
        group.enter()
        let req = QLThumbnailGenerator.Request(
            fileAt: url, size: CGSize(width: w, height: h), scale: 2.0,
            representationTypes: .thumbnail)
        QLThumbnailGenerator.shared.generateBestRepresentation(for: req) { rep, err in
            defer { group.leave() }
            var line = String(format: "  %-32s %4.0fpt  ", (url.lastPathComponent as NSString).utf8String!, w)
            if let err {
                line += "ERROR: \((err as NSError).domain) \((err as NSError).code) — \(err.localizedDescription)"
            } else if let rep, let c = sample(rep.cgImage) {
                line += String(format: "%-12s %4dx%-4d rgb(%.2f, %.2f, %.2f) cov=%.0f%%  %@",
                               (typeName(rep.type) as NSString).utf8String!,
                               rep.cgImage.width, rep.cgImage.height,
                               c.r, c.g, c.b, c.coverage * 100, classify(c))
                if let dir = ProcessInfo.processInfo.environment["QLPROBE_DUMP"] {
                    let out = URL(fileURLWithPath: dir)
                        .appendingPathComponent("\(url.lastPathComponent)-\(Int(w)).png")
                    writePNG(rep.cgImage, to: out)
                }
            } else {
                line += "no representation"
            }
            lock.lock(); lines.append(line); lock.unlock()
        }
    }
}

if group.wait(timeout: .now() + 60) == .timedOut {
    print("  TIMED OUT waiting for QuickLook")
}
lines.sorted().forEach { print($0) }
