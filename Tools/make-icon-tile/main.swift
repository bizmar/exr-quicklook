// Composes the app icon as a full tile on Apple's macOS icon grid: an 824 pt
// rounded square centred on a 1024 canvas, filled edge to edge, with the
// artwork inside it.
//
// Why a tile: since macOS 26, an app icon whose pixels do not fill that shape
// is treated as non-conforming -- the system puts it on a grey tile of its own
// and shrinks it by about a fifth. Our free-floating glass art was shown at
// ~45% of the tile width that way. A conforming tile is shown at full size.
//
// Two modes:
//   make-icon-tile <transparent-art.png> <out.png> [art-fraction]
//       art with an alpha background, placed on a light gradient tile
//   make-icon-tile --photo <logo.jpg> <out.png> [scale]
//       a square logo photographed on white, scaled to cover the whole tile.
//       Keeps the soft floor shadow, which background removal turns into a
//       ragged edge at icon sizes, and leaves no seam to hide.
import AppKit

var args = CommandLine.arguments
let photoMode = args.count > 1 && args[1] == "--photo"
if photoMode { args.remove(at: 1) }
guard args.count >= 3, let art = NSImage(contentsOfFile: args[1]),
      let artCG = art.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
    print("usage: make-icon-tile [--photo] <image> <out-1024.png> [art-fraction|scale]"); exit(2)
}
// Art mode: how much of the tile's width the artwork's longest side may take.
// Photo mode: the photo's size relative to the tile (>= 1 so it covers it).
let artFraction = args.count > 3 ? CGFloat(Double(args[3]) ?? 0.78) : 0.78
let photoScale = max(1.0, args.count > 3 ? CGFloat(Double(args[3]) ?? 1.0) : 1.0)

let canvas = 1024, tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let shape = CGPath(roundedRect: tile, cornerWidth: 185.4, cornerHeight: 185.4, transform: nil)

func save(_ image: CGImage, _ path: String) {
    let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL,
                                               "public.png" as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, image, nil)
    CGImageDestinationFinalize(dest)
}

if photoMode {
    let ctx = CGContext(data: nil, width: canvas, height: canvas, bitsPerComponent: 8, bytesPerRow: 0,
                        space: CGColorSpace(name: CGColorSpace.displayP3)!,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.interpolationQuality = .high
    ctx.addPath(shape)
    ctx.clip()
    let side = tile.width * photoScale
    ctx.draw(artCG, in: CGRect(x: tile.midX - side / 2, y: tile.midY - side / 2, width: side, height: side))
    save(ctx.makeImage()!, args[2])
    print(String(format: "tile 824/1024, photo at %.0f%% of tile", photoScale * 100))
    exit(0)
}

// The artwork's opaque bounds, so source padding does not shrink it again.
let aw = artCG.width, ah = artCG.height
var px = [UInt8](repeating: 0, count: aw * ah * 4)
let actx = CGContext(data: &px, width: aw, height: ah, bitsPerComponent: 8, bytesPerRow: aw * 4,
                     space: CGColorSpace(name: CGColorSpace.sRGB)!,
                     bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
actx.draw(artCG, in: CGRect(x: 0, y: 0, width: aw, height: ah))
var minX = aw, minY = ah, maxX = -1, maxY = -1
for y in 0..<ah { for x in 0..<aw where px[(y * aw + x) * 4 + 3] > 8 {
    minX = min(minX, x); maxX = max(maxX, x); minY = min(minY, y); maxY = max(maxY, y) } }
guard maxX >= minX, let cropped = artCG.cropping(to: CGRect(x: minX, y: ah - 1 - maxY,
                                                            width: maxX - minX + 1,
                                                            height: maxY - minY + 1)) else {
    print("artwork has no opaque pixels"); exit(1)
}

let ctx = CGContext(data: nil, width: canvas, height: canvas, bitsPerComponent: 8, bytesPerRow: 0,
                    space: CGColorSpace(name: CGColorSpace.displayP3)!,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.interpolationQuality = .high

// Tile: the grid's corner radius (185.4 at this size), a light neutral
// gradient -- the white the logo was drawn on, with a little depth.
ctx.addPath(shape)
ctx.clip()
let top = CGColor(srgbRed: 1.0, green: 1.0, blue: 1.0, alpha: 1)
let bottom = CGColor(srgbRed: 0.88, green: 0.88, blue: 0.90, alpha: 1)
let gradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
                          colors: [top, bottom] as CFArray, locations: [0, 1])!
ctx.drawLinearGradient(gradient, start: CGPoint(x: 0, y: tile.maxY), end: CGPoint(x: 0, y: tile.minY),
                       options: [])

// Artwork, centred, longest side at artFraction of the tile.
let cw = CGFloat(cropped.width), ch = CGFloat(cropped.height)
let scale = tile.width * artFraction / max(cw, ch)
let dw = cw * scale, dh = ch * scale
ctx.draw(cropped, in: CGRect(x: tile.midX - dw / 2, y: tile.midY - dh / 2, width: dw, height: dh))

save(ctx.makeImage()!, args[2])
print(String(format: "tile 824/1024, artwork %.0f x %.0f px (%.0f%% of tile)", dw, dh, artFraction * 100))
