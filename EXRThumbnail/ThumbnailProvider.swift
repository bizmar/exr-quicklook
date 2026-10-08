import CoreGraphics
import QuickLookThumbnailing
import os

/// Renders the primary layer through EXRCore. On any failure it reports an
/// error so Quick Look falls back to the generic icon — never a partial or
/// approximate render (plan §6.6).
@objc(ThumbnailProvider)
final class ThumbnailProvider: QLThumbnailProvider {

    private static let log = Logger(subsystem: "io.github.bizmar.exr-quicklook", category: "thumbnail")

    override func provideThumbnail(
        for request: QLFileThumbnailRequest,
        _ handler: @escaping (QLThumbnailReply?, Error?) -> Void
    ) {
        let pixelEdge = Int32((max(request.maximumSize.width, request.maximumSize.height)
                               * request.scale).rounded())

        // Thumbnails always render committed defaults. Session overrides set in
        // the preview overlay deliberately do not apply here (§8).
        var settings = EXRRenderer.Settings.default
        settings.maxEdge = max(pixelEdge, 1)

        guard let image = EXRRenderer.image(at: request.fileURL, settings: settings) else {
            Self.log.error("render failed for \(request.fileURL.lastPathComponent, privacy: .public)")
            handler(nil, CocoaError(.fileReadCorruptFile))
            return
        }

        // The positive signal Tools/ql-integration.sh looks for: this file was
        // rendered by this extension, not by macOS's own decoder.
        Self.log.info("rendered \(request.fileURL.lastPathComponent, privacy: .public) \(image.width)x\(image.height)")

        let contextSize = request.maximumSize
        let reply = QLThumbnailReply(contextSize: contextSize) { (ctx: CGContext) -> Bool in
            // The drawing context is sized in pixels and is NOT pre-scaled by
            // request.scale, so use its own bounds rather than maximumSize.
            let bounds = ctx.boundingBoxOfClipPath
            let scale = min(bounds.width / CGFloat(image.width),
                            bounds.height / CGFloat(image.height))
            let w = CGFloat(image.width) * scale
            let h = CGFloat(image.height) * scale
            let rect = CGRect(x: bounds.midX - w / 2, y: bounds.midY - h / 2, width: w, height: h)
            ctx.interpolationQuality = .high
            ctx.draw(image, in: rect)
            return true
        }
        handler(reply, nil)
    }
}
