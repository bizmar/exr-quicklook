import AppKit
import Quartz
import os

/// Image preview plus the floating HUD from plan §8.
///
/// The file is decoded once into an `EXRRenderer.Source`; overlay changes only
/// re-apply the transform. Measured on a 6000×4000 DWAA plate that is 50 ms per
/// change instead of 471 ms, which is the difference between a slider that
/// tracks and one that does not.
@objc(PreviewViewController)
final class PreviewViewController: NSViewController, QLPreviewingController {

    private static let log = Logger(subsystem: "io.github.bizmar.exr-quicklook", category: "preview")

    /// A layer-backed view rather than `NSImageView`.
    ///
    /// `NSImageView` with `.scaleProportionallyUpOrDown` CPU-resamples the full
    /// image every time the view redraws, and in an out-of-process view any
    /// visual change redraws the whole surface — so toggling a HUD panel paid
    /// for a 2048 px resample. Handing the CGImage to the layer makes scaling
    /// GPU compositing, and unrelated redraws cost nothing.
    private let imageView: NSView = {
        let v = NSView()
        v.wantsLayer = true
        v.layer?.contentsGravity = .resizeAspect
        v.layer?.minificationFilter = .trilinear
        v.layer?.magnificationFilter = .trilinear
        v.layer?.backgroundColor = NSColor.black.cgColor
        return v
    }()
    private var overlay: OverlayPanel?
    private var sessionObserver: NSObjectProtocol?
    private var pendingSync: DispatchWorkItem?
    private var fileURL: URL?

    // Touched only on renderQueue after the initial decode, so the C source is
    // never used from two threads.
    private var source: EXRRenderer.Source?
    private let renderQueue = DispatchQueue(label: "io.github.bizmar.exr-quicklook.render", qos: .userInitiated)

    // Main-thread state.
    private var settings = EXRRenderer.Settings.default
    private var pending: EXRRenderer.Settings?
    private var rendering = false
    private var lastContentSize: NSSize = .zero
    private var renderedWithDataWindow = false
    private var renderedLayer: String?
    // Captured on the main thread at first decode. `source` itself is reassigned
    // on renderQueue, so reading through it from the main thread would be a race.
    private var fileStatesItsPrimaries = false
    /// The colourspace the file itself states, so the picker can show it rather
    /// than the assumed default — otherwise the control names one space while
    /// the metadata panel names another.
    private var fileColorspaceID: String?
    private var layerOptions: [(id: String, label: String, isData: Bool)] = []
    private var activeLayer = ""
    private var autoLayer = ""
    /// Names what applies when there is no override, for the picker's first entry.
    private var noOverrideLabel = ""

    /// Cap for the decode. The preview never needs more than a screen's worth,
    /// and decoding to this size is what keeps a 6K frame off the heap.
    private static let previewMaxEdge: Int32 = 2048

    override func loadView() {
        let root = NSView(frame: NSRect(x: 0, y: 0, width: 900, height: 600))
        root.wantsLayer = true
        root.layer?.backgroundColor = NSColor.black.cgColor

        imageView.translatesAutoresizingMaskIntoConstraints = false
        // NSImageView takes its intrinsic content size from the image. At
        // 2048 px that makes Auto Layout refuse to shrink the view below the
        // image size, so the host clips it and the user sees a magnified crop
        // instead of the whole frame. Let it shrink; imageScaling does the fit.
        imageView.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        imageView.setContentCompressionResistancePriority(.defaultLow, for: .vertical)
        imageView.setContentHuggingPriority(.defaultLow, for: .horizontal)
        imageView.setContentHuggingPriority(.defaultLow, for: .vertical)
        root.addSubview(imageView)
        NSLayoutConstraint.activate([
            imageView.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            imageView.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            imageView.topAnchor.constraint(equalTo: root.topAnchor),
            imageView.bottomAnchor.constraint(equalTo: root.bottomAnchor),
        ])
        view = root
    }

    // Lifecycle tracing: which controller instance is asked to prepare which
    // file, and when it is actually put on screen.
    private var tag: String { String(UInt(bitPattern: ObjectIdentifier(self).hashValue) & 0xFFFF, radix: 16) }
    override func viewWillAppear() {
        super.viewWillAppear()
        Self.log.info("viewWillAppear \(self.tag, privacy: .public) \(self.fileURL?.lastPathComponent ?? "-", privacy: .public)")
        // Backstop for the neighbour sync below: whatever was missed, the
        // preview matches the session the moment it is shown.
        syncWithSession()
    }
    override func viewDidAppear() {
        super.viewDidAppear()
        Self.log.info("viewDidAppear \(self.tag, privacy: .public) \(self.fileURL?.lastPathComponent ?? "-", privacy: .public)")
    }
    override func viewWillDisappear() {
        super.viewWillDisappear()
        Self.log.info("viewWillDisappear \(self.tag, privacy: .public) \(self.fileURL?.lastPathComponent ?? "-", privacy: .public)")
    }

    func preparePreviewOfFile(at url: URL, completionHandler handler: @escaping (Error?) -> Void) {
        Self.log.info("prepare \(self.tag, privacy: .public) \(url.lastPathComponent, privacy: .public) session=\(EXRSession.current?.inputColorspace ?? "nil", privacy: .public)")
        // Carry the session's overrides onto this file. Sequences share their
        // settings, including a wrong baked-in profile, so a correction made on
        // one frame has to apply to the next.
        //
        // This once reset to .default on the line after restoring, so nothing
        // ever carried. The carried values must also reach the first decode:
        // the layer and data-window toggle change what is decoded.
        settings = EXRSession.current ?? .default
        settings.maxEdge = Self.previewMaxEdge
        fileURL = url

        guard let src = EXRRenderer.Source(url: url, maxEdge: Self.previewMaxEdge,
                                           useDataWindow: settings.useDataWindow,
                                           layer: settings.layer),
              let image = src.image(settings: settings)
        else {
            Self.log.error("render failed for \(url.lastPathComponent, privacy: .public)")
            handler(CocoaError(.fileReadCorruptFile))
            return
        }
        source = src
        Self.log.info("""
            prepared \(url.lastPathComponent, privacy: .public) layer=\(src.activeLayer, privacy: .public) \
            carried=\(self.settings.isOverridden) exposure=\(self.settings.exposureStops) \
            view=\(self.settings.view ?? "auto", privacy: .public)
            """)
        fileStatesItsPrimaries = src.hasChromaticities
        fileColorspaceID = src.chromaticitiesID
        layerOptions = src.layers
        activeLayer = src.activeLayer
        autoLayer = src.autoLayer
        if src.hasChromaticities {
            noOverrideLabel = src.chromaticitiesName == "custom chromaticities"
                ? "From file — custom primaries"
                : "From file — \(src.chromaticitiesName)"
        } else {
            let assumed = EXRPreferences.assumedInputColorspace
            let name = EXRRenderer.colorspaces.first { $0.id == assumed }?.name ?? assumed
            noOverrideLabel = "Assumed — \(name)"
        }
        renderedWithDataWindow = settings.useDataWindow
        renderedLayer = settings.layer
        show(image)
        observeSession()

        // Do not block first paint (§8): complete, then build the overlay.
        handler(nil)
        DispatchQueue.main.async { [weak self] in self?.installOverlay() }
    }

    private func show(_ image: CGImage) {
        imageView.layer?.contents = image

        // Tell Quick Look the aspect ratio so the panel is shaped like the
        // image rather than letterboxing it into a default rectangle.
        //
        // Only when it actually changes. Assigning preferredContentSize asks
        // the Quick Look host to resize the panel, which is cross-process; doing
        // it on every exposure tick was a large part of why the UI felt frozen.
        let longest = CGFloat(max(image.width, image.height))
        let scale = longest > 1200 ? 1200 / longest : 1
        let size = NSSize(width: CGFloat(image.width) * scale,
                          height: CGFloat(image.height) * scale)
        if abs(size.width - lastContentSize.width) > 0.5 ||
           abs(size.height - lastContentSize.height) > 0.5 {
            lastContentSize = size
            preferredContentSize = size
        }
    }

    /// Queues a re-render.
    ///
    /// Rendering happens off the main thread and rapid changes coalesce to the
    /// most recent settings. Doing this synchronously made every click feel
    /// frozen: a popup could not open until the transform finished, and clicks
    /// queued behind it.
    private func apply(_ new: EXRRenderer.Settings) {
        render(new)
        EXRSession.remember(settings, from: self)
    }

    private func render(_ new: EXRRenderer.Settings) {
        var next = new
        next.maxEdge = Self.previewMaxEdge
        settings = next
        pending = next
        pumpRenderQueue()
    }

    /// Quick Look prepares neighbouring files before they are shown, so a
    /// preview can be built, then the user changes something on the current
    /// file, then arrows onto the stale one. This brings it up to date.
    private func syncWithSession() {
        guard fileURL != nil else { return }
        var want = EXRSession.current ?? .default
        want.maxEdge = Self.previewMaxEdge
        guard want != settings else { return }
        Self.log.info("sync \(self.tag, privacy: .public) \(self.fileURL?.lastPathComponent ?? "-", privacy: .public) -> space=\(want.inputColorspace ?? "nil", privacy: .public) exposure=\(want.exposureStops)")
        render(want)
        overlay?.sync(to: want)
    }

    /// Re-syncs after another preview changes the session. Debounced: an
    /// exposure drag announces every tick, and the neighbours only need to be
    /// right by the time the user arrows onto them.
    private func observeSession() {
        guard sessionObserver == nil else { return }
        sessionObserver = NotificationCenter.default.addObserver(
            forName: EXRSession.didChange, object: nil, queue: .main
        ) { [weak self] note in
            guard let self, (note.object as AnyObject?) !== self else { return }
            self.pendingSync?.cancel()
            let work = DispatchWorkItem { [weak self] in self?.syncWithSession() }
            self.pendingSync = work
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25, execute: work)
        }
    }

    deinit {
        if let sessionObserver { NotificationCenter.default.removeObserver(sessionObserver) }
    }

    private func pumpRenderQueue() {
        guard !rendering, let want = pending, let url = fileURL else { return }
        rendering = true
        pending = nil
        let needsRedecode = want.useDataWindow != renderedWithDataWindow
            || want.layer != renderedLayer

        renderQueue.async { [weak self] in
            guard let self else { return }
            if needsRedecode || self.source == nil {
                self.source = EXRRenderer.Source(url: url, maxEdge: Self.previewMaxEdge,
                                                 useDataWindow: want.useDataWindow, layer: want.layer)
            }
            let image = self.source?.image(settings: want)
            DispatchQueue.main.async {
                self.rendering = false
                self.renderedWithDataWindow = want.useDataWindow
                self.renderedLayer = want.layer
                if let image {
                    self.show(image)
                } else {
                    Self.log.error("re-render failed for \(url.lastPathComponent, privacy: .public)")
                }
                // A newer request arrived while this one was in flight.
                self.pumpRenderQueue()
            }
        }
    }

    private func installOverlay() {
        guard overlay == nil, let url = fileURL else { return }
        let panel = OverlayPanel(
            views: EXRRenderer.views,
            colorspaces: EXRRenderer.colorspaces,
            metadata: EXRRenderer.describe(at: url),
            fileStatesItsPrimaries: fileStatesItsPrimaries,
            fileColorspaceID: fileColorspaceID,
            noOverrideLabel: noOverrideLabel,
            layers: layerOptions,
            activeLayer: activeLayer,
            autoLayer: autoLayer,
            // Seed the controls from the carried settings so the panel shows
            // what is actually being rendered, not the defaults.
            initial: settings,
            onChange: { [weak self] new in self?.apply(new) },
            onReset: { [weak self] in
                EXRSession.clear(from: self)
                guard let self else { return }
                // Back to no overrides at all: the file's own primaries (or the
                // assumed default) and the automatic layer.
                self.apply(.default)
            })
        panel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(panel)
        NSLayoutConstraint.activate([
            panel.topAnchor.constraint(equalTo: view.topAnchor, constant: 12),
            panel.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -12),
            panel.leadingAnchor.constraint(greaterThanOrEqualTo: view.leadingAnchor, constant: 8),
            panel.bottomAnchor.constraint(lessThanOrEqualTo: view.bottomAnchor, constant: -8),
        ])
        overlay = panel
    }
}
