// Turns the logo artwork into a transparent-background master for the app icon.
//
// Removes only the near-white region *connected to the image border* (a flood
// fill from the edges), so white highlights inside the glass survive. A plain
// "make near-white transparent" threshold would punch holes in the artwork.
import AppKit

let args = CommandLine.arguments
guard args.count == 3, let src = NSImage(contentsOfFile: args[1]),
      let cg = src.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
    print("usage: make-icon <in> <out.png>"); exit(2)
}
let w = cg.width, h = cg.height
var px = [UInt8](repeating: 0, count: w * h * 4)
let ctx = CGContext(data: &px, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                    space: CGColorSpace(name: CGColorSpace.sRGB)!,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))

// "Background" = bright and nearly neutral. The soft drop shadow and reflection
// under the stack are not neutral-white, so they are kept as part of the art.
func isBackground(_ i: Int) -> Bool {
    let r = Int(px[i]), g = Int(px[i + 1]), b = Int(px[i + 2])
    return min(r, g, b) > 232 && max(r, g, b) - min(r, g, b) < 14
}

var bg = [Bool](repeating: false, count: w * h)
var stack: [Int] = []
for x in 0..<w { stack.append(x); stack.append((h - 1) * w + x) }
for y in 0..<h { stack.append(y * w); stack.append(y * w + w - 1) }
while let p = stack.popLast() {
    if bg[p] || !isBackground(p * 4) { continue }
    bg[p] = true
    let x = p % w, y = p / w
    if x > 0 { stack.append(p - 1) }
    if x < w - 1 { stack.append(p + 1) }
    if y > 0 { stack.append(p - w) }
    if y < h - 1 { stack.append(p + w) }
}

// Second pass for the floor reflection under the stack. It is near-white but
// slightly tinted, so the strict test above keeps it. A relaxed test is only
// safe in the lower part of the image: higher up, the top slab's clear glass
// rim touches the background and a relaxed fill would leak into it. Down here
// the slabs are saturated red, which the relaxed test still rejects.
func isReflection(_ i: Int) -> Bool {
    let r = Int(px[i]), g = Int(px[i + 1]), b = Int(px[i + 2])
    return min(r, g, b) > 185 && max(r, g, b) - min(r, g, b) < 45
}
let lowerStart = Int(Double(h) * 0.55)   // bitmap rows run top to bottom
stack = []
for y in lowerStart..<h { for x in 0..<w where bg[y * w + x] { stack.append(y * w + x) } }
var seen = bg
while let p = stack.popLast() {
    let x = p % w, y = p / w
    for q in [x > 0 ? p - 1 : -1, x < w - 1 ? p + 1 : -1,
              y > lowerStart ? p - w : -1, y < h - 1 ? p + w : -1] where q >= 0 {
        if seen[q] || !isReflection(q * 4) { continue }
        seen[q] = true
        bg[q] = true
        stack.append(q)
    }
}

// Feather the edge over a couple of pixels so the cut-out is not jagged.
var alpha = bg.map { $0 ? 0.0 : 1.0 }
for _ in 0..<3 {
    var next = alpha
    for y in 1..<(h - 1) { for x in 1..<(w - 1) {
        let i = y * w + x
        guard !bg[i] else { continue }
        let n = (alpha[i - 1] + alpha[i + 1] + alpha[i - w] + alpha[i + w]) / 4
        next[i] = min(alpha[i], n * 0.5 + 0.5)
    } }
    alpha = next
}
for i in 0..<(w * h) {
    let a = alpha[i]
    px[i * 4 + 0] = UInt8(Double(px[i * 4 + 0]) * a)
    px[i * 4 + 1] = UInt8(Double(px[i * 4 + 1]) * a)
    px[i * 4 + 2] = UInt8(Double(px[i * 4 + 2]) * a)
    px[i * 4 + 3] = UInt8(255 * a)
}
let removed = bg.filter { $0 }.count
let out = ctx.makeImage()!
let rep = NSBitmapImageRep(cgImage: out)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[2]))
print(String(format: "background removed: %.1f%% of pixels", Double(removed) * 100 / Double(w * h)))
