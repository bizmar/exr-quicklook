import CoreGraphics
import EXRCore
import Foundation

/// The single rendering entry point for both extensions.
///
/// Both the thumbnail and the preview drive the same C transform, so a preview
/// with no override active produces pixels identical to the thumbnail — that is
/// how D7 is enforced, rather than by two code paths asked to agree.
enum EXRRenderer {

    struct Settings: Equatable {
        var maxEdge: Int32 = 0
        var exposureStops: Float = 0
        /// nil = automatic: the committed default view for imagery, raw for a
        /// data pass (depth, position, motion...). The overlay normalises a
        /// pick of whatever automatic would show back to nil.
        var view: String? = nil
        var channel: EXRChannelView = EXR_VIEW_RGB
        var alphaOverChecker = false
        var useDataWindow = false
        var inputColorspace: String? = nil // nil = committed default
        /// nil = the automatic choice. Changing this re-decodes, unlike the
        /// view-side settings, so it is handled like `useDataWindow`.
        var layer: String? = nil

        static let `default` = Settings()

        /// True when anything diverges from the committed defaults. Drives the
        /// overlay badge — an invisible override is a bug report waiting to
        /// happen (§8).
        var isOverridden: Bool {
            exposureStops != 0
                || channel != EXR_VIEW_RGB
                || alphaOverChecker
                || useDataWindow
                || layer != nil
                // nil is automatic, and the overlay normalises a pick of the
                // automatic view back to nil, so any value is a real override.
                || view != nil
                // nil is the only "not overridden" state: the overlay
                // normalises a pick of the file's own space back to nil.
                || inputColorspace != nil
        }
    }

    /// Calls `body` with a filled-in C options struct, keeping the backing
    /// strings alive for the duration of the call.
    private static func withOptionalCString<T>(_ string: String?,
                                               _ body: (UnsafePointer<CChar>?) -> T) -> T {
        guard let string else { return body(nil) }
        return string.withCString { body($0) }
    }

    private static func withOptions<T>(_ raw: Settings,
                                       _ body: (UnsafePointer<EXRRenderOptions>) -> T) -> T {
        // The committed preferences travel here, at the single point where
        // settings become C options, so no caller can bypass them. Both are
        // separate from the explicit overrides: the C side applies the assumed
        // colourspace only to a file that states no primaries, and the default
        // view only to imagery -- a data pass renders raw unless a view is
        // picked. Same rule for thumbnail and preview, so D7 holds by construction.
        let s = raw
        return withOptionalCString(s.view) { view in
            withOptionalCString(s.inputColorspace) { override in
                withOptionalCString(EXRPreferences.assumedInputColorspace) { assumed in
                    withOptionalCString(EXRPreferences.defaultView) { defaultView in
                        var o = EXRRenderOptions(max_edge: s.maxEdge,
                                                 exposure_stops: s.exposureStops,
                                                 view: view,
                                                 channel_view: Int32(s.channel.rawValue),
                                                 alpha_over_checker: s.alphaOverChecker ? 1 : 0,
                                                 use_data_window: s.useDataWindow ? 1 : 0,
                                                 input_colorspace: override,
                                                 assumed_colorspace: assumed,
                                                 default_view: defaultView)
                        return withUnsafePointer(to: &o) { body($0) }
                    }
                }
            }
        }
    }

    private static func makeImage(_ result: EXRRenderResult) -> CGImage? {
        guard let pixels = result.pixels, result.width > 0, result.height > 0 else { return nil }
        let width = Int(result.width), height = Int(result.height)
        let byteCount = width * height * 4 * MemoryLayout<UInt16>.size

        // CGDataProvider takes ownership; release through the C allocator that
        // produced the buffer, not Swift's.
        guard let provider = CGDataProvider(
            dataInfo: pixels, data: pixels, size: byteCount,
            releaseData: { info, _, _ in
                var r = EXRRenderResult(width: 0, height: 0,
                                        pixels: info?.assumingMemoryBound(to: UInt16.self))
                exr_render_free(&r)
            })
        else {
            var r = result
            exr_render_free(&r)
            return nil
        }

        // Display P3, per D5. The LUT already emitted display-encoded values,
        // so nothing further is applied here.
        let space = CGColorSpace(name: CGColorSpace.displayP3) ?? CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGBitmapInfo(rawValue:
            CGImageAlphaInfo.noneSkipLast.rawValue | CGBitmapInfo.byteOrder16Little.rawValue)
        return CGImage(width: width, height: height,
                       bitsPerComponent: 16, bitsPerPixel: 64,
                       bytesPerRow: width * 4 * MemoryLayout<UInt16>.size,
                       space: space, bitmapInfo: bitmapInfo,
                       provider: provider, decode: nil,
                       shouldInterpolate: true, intent: .defaultIntent)
    }

    /// Why the last `image(at:)` or source open on this thread failed, for logs.
    static var lastError: String { String(cString: exr_last_error()) }

    /// One-shot decode and transform. Returns nil on any failure — callers must
    /// fall back to the generic icon, never render something approximate.
    static func image(at url: URL, settings: Settings = .default) -> CGImage? {
        var result = EXRRenderResult()
        let ok = url.path.withCString { path in
            withOptions(settings) { exr_render(path, $0, &result) }
        }
        guard ok == 1 else { return nil }
        return makeImage(result)
    }

    /// A file decoded once, re-transformed cheaply.
    ///
    /// Dragging the exposure slider must not re-decode a 6K frame per event.
    /// Measured on a 6000×4000 DWAA plate: 471 ms per one-shot render versus
    /// 50 ms per cached re-render.
    final class Source {
        // A forward-declared C struct imports as OpaquePointer; no further
        // wrapping is needed or correct.
        private let handle: OpaquePointer
        let hasChromaticities: Bool
        /// The decoded layer has an alpha channel; without one, Alpha is disabled.
        let hasAlpha: Bool
        /// The id of the colourspace the file states, matched against our named
        /// list, or nil when it states none or names one we do not know. The
        /// picker shows this rather than the assumed default, so the control and
        /// the metadata panel never name different spaces.
        let chromaticitiesID: String?

        /// Everything the layer switcher can offer for this file. `isData` marks
        /// depth, position, motion, normals, masks, cryptomatte and bare
        /// channels — listed, per §6.3, never chosen while there is imagery, and
        /// rendered raw unless a view is picked.
        let layers: [(id: String, label: String, isData: Bool)]
        /// The layer actually decoded, so the picker shows what is on screen.
        let activeLayer: String
        /// The layer §6.3 would pick by itself. Choosing it is not an override.
        let autoLayer: String
        /// What the file states, as text: "ACES2065-1 (AP0)", "custom
        /// chromaticities", or "" when it states none.
        let chromaticitiesName: String

        init?(url: URL, maxEdge: Int32, useDataWindow: Bool, layer: String?) {
            func open(_ l: UnsafePointer<CChar>?) -> OpaquePointer? {
                url.path.withCString { exr_open($0, maxEdge, useDataWindow ? 1 : 0, l) }
            }
            guard let h = (layer.map { l in l.withCString { open($0) } } ?? open(nil))
            else { return nil }
            handle = h
            layers = (0..<Int(exr_source_layer_count(h))).compactMap { i in
                guard let id = exr_source_layer_id(h, Int32(i)),
                      let label = exr_source_layer_label(h, Int32(i)) else { return nil }
                return (String(cString: id), String(cString: label),
                        exr_source_layer_is_data(h, Int32(i)) == 1)
            }
            activeLayer = exr_source_active_layer(h).map { String(cString: $0) } ?? ""
            autoLayer = exr_source_auto_layer(h).map { String(cString: $0) } ?? ""
            hasChromaticities = exr_source_has_chromaticities(h) == 1
            hasAlpha = exr_source_has_alpha(h) == 1
            let stated = exr_source_chromaticities_name(h).map { String(cString: $0) } ?? ""
            chromaticitiesName = stated
            chromaticitiesID = stated.isEmpty
                ? nil
                : EXRRenderer.colorspaces.first { $0.name == stated }?.id
        }

        deinit { exr_close(handle) }

        func image(settings: Settings) -> CGImage? {
            var result = EXRRenderResult()
            let ok = withOptions(settings) { exr_source_render(handle, $0, &result) }
            guard ok == 1 else { return nil }
            return makeImage(result)
        }
    }

    /// Tab-separated "label\tvalue" lines for the metadata panel.
    static func describe(at url: URL) -> [(String, String)] {
        var buffer = [CChar](repeating: 0, count: 8192)
        let ok = url.path.withCString { exr_describe($0, &buffer, buffer.count) }
        guard ok == 1 else { return [] }
        return String(cString: buffer)
            .split(separator: "\n")
            .compactMap { line in
                let parts = line.split(separator: "\t", maxSplits: 1)
                guard parts.count == 2 else { return nil }
                return (String(parts[0]), String(parts[1]))
            }
    }

    static var views: [(id: String, name: String)] {
        (0..<Int(exr_view_count())).compactMap { i in
            guard let id = exr_view_id(Int32(i)),
                  let name = exr_view_display_name(Int32(i)) else { return nil }
            return (String(cString: id), String(cString: name))
        }
    }

    static var colorspaces: [(id: String, name: String)] {
        (0..<Int(exr_colorspace_count())).compactMap { i in
            guard let id = exr_colorspace_id(Int32(i)),
                  let name = exr_colorspace_display_name(Int32(i)) else { return nil }
            return (String(cString: id), String(cString: name))
        }
    }

    static var defaultViewID: String {
        exr_default_view_id().map { String(cString: $0) } ?? ""
    }
    static var defaultColorspaceID: String {
        exr_default_colorspace_id().map { String(cString: $0) } ?? "acescg"
    }
}
