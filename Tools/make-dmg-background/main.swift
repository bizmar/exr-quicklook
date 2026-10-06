// Draws the DMG window background: a drag arrow from the app (left) to
// Applications (right), and a quieter row underneath for the readme files.
// Coordinates are Finder window points, origin top-left, matching the icon
// positions in Tools/dmg-settings.py.
//
// usage: make-dmg-background <out-dir>   -> background.png, background@2x.png
import AppKit

let W: CGFloat = 640, H: CGFloat = 420
let appX: CGFloat = 170, appsX: CGFloat = 470, iconY: CGFloat = 130   // icon centres
let rowY: CGFloat = 320                                                  // readme row

func render(scale: CGFloat, to path: String) {
    let pw = Int(W * scale), ph = Int(H * scale)
    let ctx = CGContext(data: nil, width: pw, height: ph, bitsPerComponent: 8, bytesPerRow: 0,
                        space: CGColorSpace(name: CGColorSpace.sRGB)!,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.scaleBy(x: scale, y: scale)
    // Flip to top-left origin so the numbers read like Finder's.
    ctx.translateBy(x: 0, y: H); ctx.scaleBy(x: 1, y: -1)

    // Background: the white the icon is drawn on, with a little depth.
    let bg = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
                        colors: [CGColor(srgbRed: 1, green: 1, blue: 1, alpha: 1),
                                 CGColor(srgbRed: 0.93, green: 0.93, blue: 0.94, alpha: 1)] as CFArray,
                        locations: [0, 1])!
    ctx.drawLinearGradient(bg, start: CGPoint(x: 0, y: 0), end: CGPoint(x: 0, y: H), options: [])

    // The arrow: one arc in the logo's amber-to-red. The shaft stops exactly
    // at the head's base, and the head is aimed along the curve's tangent *at
    // that point*, so shaft and head meet with one direction -- no kink.
    // (Aiming along a chord, or letting the shaft run on into the head while
    // it still curves, both leave a visible bend.)
    let x0 = appX + 82, x1 = appsX - 84, y = iconY - 2
    let headLength: CGFloat = 26, headHalfWidth: CGFloat = 15
    let p0 = CGPoint(x: x0, y: y), control = CGPoint(x: (x0 + x1) / 2, y: y - 34)
    let p2 = CGPoint(x: x1, y: y)
    func q(_ t: CGFloat) -> CGPoint {
        let u = 1 - t
        return CGPoint(x: u * u * p0.x + 2 * u * t * control.x + t * t * p2.x,
                       y: u * u * p0.y + 2 * u * t * control.y + t * t * p2.y)
    }
    // End the shaft where the head, laid along the tangent, would reach p2.
    var tb: CGFloat = 1
    while tb > 0 && hypot(q(tb).x - p2.x, q(tb).y - p2.y) < headLength { tb -= 0.0005 }
    let base = q(tb)
    // Tangent of the quadratic at tb: 2(1-t)(C-P0) + 2t(P2-C).
    var dx = 2 * (1 - tb) * (control.x - p0.x) + 2 * tb * (p2.x - control.x)
    var dy = 2 * (1 - tb) * (control.y - p0.y) + 2 * tb * (p2.y - control.y)
    let len = hypot(dx, dy); dx /= len; dy /= len
    let tip = CGPoint(x: base.x + dx * headLength, y: base.y + dy * headLength)
    // The shaft is exactly the arc up to tb (de Casteljau split).
    let shaft = CGMutablePath()
    shaft.move(to: p0)
    shaft.addQuadCurve(to: base, control: CGPoint(x: p0.x + (control.x - p0.x) * tb,
                                                  y: p0.y + (control.y - p0.y) * tb))
    ctx.saveGState()
    // Round caps: the start looks finished, and the end cap sits inside the
    // head (it reaches 3.5 pt past the base, the head is 30 pt wide there).
    ctx.setLineWidth(7); ctx.setLineCap(.round)
    ctx.addPath(shaft); ctx.replacePathWithStrokedPath(); ctx.clip()
    let amber = CGColor(srgbRed: 0.93, green: 0.55, blue: 0.24, alpha: 1)
    let red = CGColor(srgbRed: 0.72, green: 0.20, blue: 0.16, alpha: 1)
    let grad = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
                          colors: [amber, red] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(grad, start: CGPoint(x: x0, y: 0), end: CGPoint(x: base.x, y: 0),
                           options: [.drawsBeforeStartLocation, .drawsAfterEndLocation])
    ctx.restoreGState()
    let nx = -dy, ny = dx   // normal to the tangent
    let head = CGMutablePath()
    head.move(to: tip)
    head.addLine(to: CGPoint(x: base.x + nx * headHalfWidth, y: base.y + ny * headHalfWidth))
    head.addLine(to: CGPoint(x: base.x - nx * headHalfWidth, y: base.y - ny * headHalfWidth))
    head.closeSubpath()
    ctx.setFillColor(red); ctx.addPath(head); ctx.fillPath()

    // Captions. Text needs an unflipped context.
    func text(_ s: String, size: CGFloat, weight: NSFont.Weight, gray: CGFloat, centreX: CGFloat, topY: CGFloat) {
        let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: size, weight: weight),
                                                    .foregroundColor: NSColor(white: gray, alpha: 1)]
        let str = NSAttributedString(string: s, attributes: attrs)
        let sz = str.size()
        NSGraphicsContext.saveGraphicsState()
        ctx.saveGState()
        ctx.translateBy(x: 0, y: H); ctx.scaleBy(x: 1, y: -1)
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
        str.draw(at: NSPoint(x: centreX - sz.width / 2, y: H - topY - sz.height))
        ctx.restoreGState()
        NSGraphicsContext.restoreGraphicsState()
    }
    text("Drag to Applications", size: 13, weight: .medium, gray: 0.45, centreX: (appX + appsX) / 2, topY: iconY + 18)

    // Secondary row: a hairline and a small heading, so the readme files read
    // as "afterwards", not as a second thing to drag.
    ctx.setStrokeColor(CGColor(gray: 0, alpha: 0.10)); ctx.setLineWidth(1)
    ctx.move(to: CGPoint(x: 60, y: rowY - 72)); ctx.addLine(to: CGPoint(x: W - 60, y: rowY - 72)); ctx.strokePath()
    text("Then open the app once and switch on its extensions — see INSTALL.txt", size: 11.5, weight: .regular,
         gray: 0.50, centreX: W / 2, topY: rowY - 62)

    let img = ctx.makeImage()!
    let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL, "public.png" as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, img, [kCGImagePropertyDPIWidth: 72 * scale, kCGImagePropertyDPIHeight: 72 * scale] as CFDictionary)
    CGImageDestinationFinalize(dest)
}

let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
render(scale: 1, to: "\(out)/background.png")
render(scale: 2, to: "\(out)/background@2x.png")
print("wrote \(out)/background.png and background@2x.png (\(Int(W))x\(Int(H)) pt)")
