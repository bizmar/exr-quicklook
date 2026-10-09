import AppKit
import EXRCore

/// A plain dark panel background.
///
/// Deliberately *not* an `NSVisualEffectView`. The extension's view is hosted
/// out of process, so vibrancy makes the host composite a blur behind the panel
/// on every surface update — measured at ~400 ms from click to the button
/// visibly tinting. A solid layer costs nothing and the contrast was already
/// coming from the scrim, not the blur.
final class HUDBackground: NSView {
    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor(calibratedWhite: 0.08, alpha: 0.92).cgColor
        layer?.cornerRadius = 10
        layer?.masksToBounds = true
        layer?.borderWidth = 1
        layer?.borderColor = NSColor(calibratedWhite: 1, alpha: 0.12).cgColor
    }
    required init?(coder: NSCoder) { fatalError("not used") }
}

/// The floating HUD from plan §8: two collapsed buttons in the upper right,
/// one for display controls and one for metadata, each expanding a translucent
/// panel.
///
/// Overrides are transient. They never write to committed preferences, and
/// whenever any is active the controls button is badged — an invisible override
/// is a bug report waiting to happen (§8, non-negotiable).
final class OverlayPanel: NSView {

    // MARK: - Chrome

    private let controlsButton = NSButton()
    private let metadataButton = NSButton()
    private let badgeDot = NSView()
    private let controlsBody = HUDBackground()
    private let metadataBody = HUDBackground()

    // MARK: - Controls

    private let exposureLabel = NSTextField(labelWithString: OverlayPanel.exposureText(0))

    /// "Exposure  0.0" at zero (no sign), otherwise signed: "+1.5", "-0.7".
    static func exposureText(_ stops: Float) -> String {
        stops == 0 ? "Exposure  0.0" : String(format: "Exposure  %+.1f", stops)
    }
    private let exposureSlider = NSSlider()
    private let channelControl = NSSegmentedControl(
        labels: ["RGB", "Alpha"],
        trackingMode: .selectOne, target: nil, action: nil)
    private let viewPopup = NSPopUpButton()
    private let layerPopup = NSPopUpButton()
    private let layerLabel = NSTextField(labelWithString: "Layer")
    private var layerIDs: [String] = []
    private var layerIsData: [Bool] = []
    private let spaceLabel = NSTextField(labelWithString: "Assumed input")
    private let spacePopup = NSPopUpButton()
    private let checkerToggle = NSButton(checkboxWithTitle: "Alpha over checkerboard",
                                         target: nil, action: nil)
    private let dataWindowToggle = NSButton(checkboxWithTitle: "Show data window (overscan)",
                                            target: nil, action: nil)
    private let resetButton = NSButton()
    private let metadataStack = NSStackView()

    private let viewIDs: [String]
    private let spaceChoices: [String?]
    private let autoLayer: String
    private let fileStatesItsPrimaries: Bool
    private let onChange: (EXRRenderer.Settings) -> Void
    private let onResetRequested: () -> Void
    private let fileColorspaceID: String?

    private var settings = EXRRenderer.Settings.default
    private var controlsExpanded = false

    // The metadata panel sits under the button until the controls panel has
    // been opened at least once, then permanently below it. Opening info on its
    // own should not leave it floating under an empty gap; once it has moved
    // down it can stay there, which avoids re-laying out on every toggle.
    //
    // Superseded 2026-10-08 (the user found info stranded low after closing
    // the controls): info sits under the controls only while both are open,
    // otherwise under the buttons. It is moved only when visible and in the
    // wrong place, so toggling the controls alone still costs no layout.
    private lazy var metaTopBelowButton = metadataBody.topAnchor.constraint(
        equalTo: controlsButton.bottomAnchor, constant: 6)
    private lazy var metaTopBelowControls = metadataBody.topAnchor.constraint(
        equalTo: controlsBody.bottomAnchor, constant: 6)
    private var metadataBelowControls = false
    private var metadataExpanded = false

    private static let channelOrder: [EXRChannelView] = [EXR_VIEW_RGB, EXR_VIEW_ALPHA]
    /// The decoded layer has alpha. Without it the Alpha button is disabled
    /// and RGB is shown, but an alpha choice still carries to the next file.
    private var hasAlpha: Bool

    /// Called after a re-decode (another layer, the data window).
    func setAlphaAvailable(_ available: Bool) {
        hasAlpha = available
        showChannel(settings.channel)
    }

    private func showChannel(_ channel: EXRChannelView) {
        let alpha = Self.channelOrder.firstIndex(of: EXR_VIEW_ALPHA) ?? 1
        channelControl.setEnabled(hasAlpha, forSegment: alpha)
        channelControl.setToolTip(hasAlpha ? nil : "This layer has no alpha", forSegment: alpha)
        let wanted = Self.channelOrder.firstIndex(of: channel) ?? 0
        channelControl.selectedSegment = (wanted == alpha && !hasAlpha) ? 0 : wanted
    }

    init(views: [(id: String, name: String)],
         colorspaces: [(id: String, name: String)],
         metadata: [(String, String)],
         fileStatesItsPrimaries: Bool,
         fileColorspaceID: String?,
         noOverrideLabel: String,
         layers: [(id: String, label: String, isData: Bool)],
         activeLayer: String,
         autoLayer: String,
         hasAlpha: Bool,
         initial: EXRRenderer.Settings,
         onChange: @escaping (EXRRenderer.Settings) -> Void,
         onReset: @escaping () -> Void) {
        self.viewIDs = views.map(\.id)
        // Entry 0 is "no override" (nil); a separator follows; then the named
        // spaces. The separator's slot is nil too but can never be selected.
        self.spaceChoices = [nil, nil] + colorspaces.map { Optional($0.id) }
        self.onChange = onChange
        self.onResetRequested = onReset
        self.fileColorspaceID = fileColorspaceID
        self.layerIDs = layers.map(\.id)
        self.layerIsData = layers.map(\.isData)
        self.autoLayer = autoLayer
        self.fileStatesItsPrimaries = fileStatesItsPrimaries
        self.hasAlpha = hasAlpha
        super.init(frame: .zero)

        // Always dark, whatever the system theme. The panel floats over
        // imagery on a fixed dark background, so following a Light system
        // appearance rendered dark-grey labels on it -- nearly unreadable.
        // Apple's own HUD panels make the same choice.
        appearance = NSAppearance(named: .darkAqua)

        configureButton(controlsButton, symbol: "slider.horizontal.3",
                        description: "Display options", action: #selector(toggleControls))
        configureButton(metadataButton, symbol: "info.circle",
                        description: "File information", action: #selector(toggleMetadata))

        // An explicit dot rather than only a tint: contentTintColor is not
        // reliably applied to a bordered button's symbol, which made the badge
        // invisible in practice.
        badgeDot.wantsLayer = true
        badgeDot.layer?.backgroundColor = NSColor.systemOrange.cgColor
        badgeDot.layer?.cornerRadius = 4
        badgeDot.layer?.borderWidth = 1
        badgeDot.layer?.borderColor = NSColor.black.withAlphaComponent(0.6).cgColor
        badgeDot.isHidden = true
        badgeDot.translatesAutoresizingMaskIntoConstraints = false

        configureBody(controlsBody)
        configureBody(metadataBody)

        exposureLabel.font = .monospacedDigitSystemFont(ofSize: 11, weight: .regular)
        exposureSlider.minValue = -6
        exposureSlider.maxValue = 6
        exposureSlider.doubleValue = 0
        exposureSlider.isContinuous = true
        exposureSlider.target = self
        exposureSlider.action = #selector(controlChanged)

        channelControl.selectedSegment = 0
        channelControl.segmentStyle = .roundRect
        channelControl.target = self
        channelControl.action = #selector(controlChanged)

        viewPopup.addItems(withTitles: views.map(\.name))
        viewPopup.target = self
        viewPopup.action = #selector(controlChanged)

        // The first entry is the absence of an override, named by what actually
        // applies -- the file's own primaries, or the assumed default when it
        // states none. Everything below it is an explicit override, which D9
        // (amended) allows even on a tagged file, because a misapplied profile
        // is baked into every frame of a sequence. Any override badges (D7).
        spacePopup.addItem(withTitle: noOverrideLabel)
        spacePopup.menu?.addItem(.separator())
        spacePopup.addItems(withTitles: colorspaces.map(\.name))
        spacePopup.target = self
        spacePopup.action = #selector(controlChanged)
        spacePopup.selectItem(at: 0)
        spaceLabel.stringValue = "Input colourspace"
        spaceLabel.textColor = .labelColor

        for toggle in [checkerToggle, dataWindowToggle] {
            toggle.target = self
            toggle.action = #selector(controlChanged)
            toggle.controlSize = .small
        }

        resetButton.title = "Reset to defaults"
        resetButton.bezelStyle = .rounded
        resetButton.controlSize = .small
        resetButton.target = self
        resetButton.action = #selector(resetTapped)

        buildControlsBody()
        buildMetadataBody(metadata)

        // Both panels occupy the same position and are mutually exclusive.
        //
        // They stay laid out permanently and are revealed by opacity, so a
        // toggle never changes the layout. That matters because the extension's
        // view is hosted out of process: changing intrinsic size forces a
        // cross-process layout pass, which is what made the buttons feel slow.
        // Stacking them vertically would have left the second panel floating
        // below an invisible first one, hence same-position plus exclusivity.
        addSubview(controlsBody)
        addSubview(metadataBody)
        addSubview(controlsButton)
        addSubview(metadataButton)
        addSubview(badgeDot)
        layoutChrome()
        // Layers are only worth showing when there is a choice to make.
        layerPopup.removeAllItems()
        for l in layers {
            // Depth, position, motion, normals, masks, cryptomatte and bare
            // channels are offered but marked, so it is clear they are not
            // ordinary imagery (§6.3) -- and why they appear untransformed.
            layerPopup.addItem(withTitle: l.isData ? "\(l.label)  ·  data" : l.label)
        }
        if let idx = layers.firstIndex(where: { $0.id == activeLayer }) {
            layerPopup.selectItem(at: idx)
        }
        layerPopup.target = self
        layerPopup.action = #selector(controlChanged)
        let multiple = layers.count > 1
        layerPopup.isHidden = !multiple
        layerLabel.isHidden = !multiple

        seed(from: initial)
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    // MARK: - Construction helpers

    private func configureButton(_ b: NSButton, symbol: String,
                                 description: String, action: Selector) {
        b.bezelStyle = .circular
        // A latching button so the click registers visually the instant it
        // happens, and the button stays lit while its panel is open, rather
        // than the UI appearing frozen until the panel finishes laying out.
        b.setButtonType(.pushOnPushOff)
        let image = NSImage(systemSymbolName: symbol, accessibilityDescription: description)
        image?.isTemplate = true
        b.image = image
        b.toolTip = description
        b.target = self
        b.action = action
        b.translatesAutoresizingMaskIntoConstraints = false
        // A dark disc and a soft shadow, so the buttons stay visible on any
        // image: on a white frame (a solid alpha shown straight, say) the bare
        // bezel disappeared, leaving only the badge (found by hand 2026-10-09).
        b.wantsLayer = true
        b.layer?.backgroundColor = NSColor(calibratedWhite: 0.05, alpha: 0.6).cgColor
        b.layer?.cornerRadius = 13
        b.layer?.shadowColor = NSColor.black.cgColor
        b.layer?.shadowOpacity = 0.5
        b.layer?.shadowRadius = 3
        b.layer?.shadowOffset = .zero
    }

    private func configureBody(_ v: HUDBackground) {
        v.alphaValue = 0
        v.translatesAutoresizingMaskIntoConstraints = false
    }

    private func separator() -> NSView {
        let line = NSBox()
        line.boxType = .separator
        return line
    }

    private func buildControlsBody() {
        let stack = NSStackView(views: [
            exposureLabel, exposureSlider,
            layerLabel,
            layerPopup,
            channelControl,
            separator(),
            viewPopup,
            spaceLabel, spacePopup,
            separator(),
            checkerToggle, dataWindowToggle,
            separator(),
            resetButton,
        ])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 6
        stack.translatesAutoresizingMaskIntoConstraints = false
        controlsBody.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: controlsBody.topAnchor, constant: 10),
            stack.leadingAnchor.constraint(equalTo: controlsBody.leadingAnchor, constant: 10),
            stack.trailingAnchor.constraint(equalTo: controlsBody.trailingAnchor, constant: -10),
            stack.bottomAnchor.constraint(equalTo: controlsBody.bottomAnchor, constant: -10),
            exposureSlider.widthAnchor.constraint(equalToConstant: 240),
            channelControl.widthAnchor.constraint(equalToConstant: 240),
            layerPopup.widthAnchor.constraint(lessThanOrEqualToConstant: 260),
            viewPopup.widthAnchor.constraint(equalToConstant: 240),
            spacePopup.widthAnchor.constraint(equalToConstant: 240),
        ])
    }

    private func buildMetadataBody(_ rows: [(String, String)]) {
        metadataStack.orientation = .vertical
        metadataStack.alignment = .leading
        metadataStack.spacing = 3
        metadataStack.translatesAutoresizingMaskIntoConstraints = false

        for (label, value) in rows {
            let l = NSTextField(labelWithString: label)
            l.font = .systemFont(ofSize: 10, weight: .semibold)
            l.textColor = .secondaryLabelColor
            let v = NSTextField(labelWithString: value)
            v.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
            v.lineBreakMode = .byTruncatingMiddle
            v.isSelectable = true
            let row = NSStackView(views: [l, v])
            row.orientation = .vertical
            row.alignment = .leading
            row.spacing = 0
            metadataStack.addArrangedSubview(row)
            v.widthAnchor.constraint(lessThanOrEqualToConstant: 260).isActive = true
        }
        if rows.isEmpty {
            metadataStack.addArrangedSubview(NSTextField(labelWithString: "No metadata"))
        }

        metadataBody.addSubview(metadataStack)
        NSLayoutConstraint.activate([
            metadataStack.topAnchor.constraint(equalTo: metadataBody.topAnchor, constant: 10),
            metadataStack.leadingAnchor.constraint(equalTo: metadataBody.leadingAnchor, constant: 10),
            metadataStack.trailingAnchor.constraint(equalTo: metadataBody.trailingAnchor, constant: -10),
            metadataStack.bottomAnchor.constraint(equalTo: metadataBody.bottomAnchor, constant: -10),
        ])
    }

    private func layoutChrome() {
        NSLayoutConstraint.activate([
            metadataButton.topAnchor.constraint(equalTo: topAnchor),
            metadataButton.trailingAnchor.constraint(equalTo: trailingAnchor),
            metadataButton.widthAnchor.constraint(equalToConstant: 26),
            metadataButton.heightAnchor.constraint(equalToConstant: 26),

            controlsButton.topAnchor.constraint(equalTo: topAnchor),
            controlsButton.trailingAnchor.constraint(equalTo: metadataButton.leadingAnchor,
                                                     constant: -6),
            controlsButton.widthAnchor.constraint(equalToConstant: 26),
            controlsButton.heightAnchor.constraint(equalToConstant: 26),

            badgeDot.widthAnchor.constraint(equalToConstant: 8),
            badgeDot.heightAnchor.constraint(equalToConstant: 8),
            badgeDot.trailingAnchor.constraint(equalTo: controlsButton.trailingAnchor, constant: 1),
            badgeDot.topAnchor.constraint(equalTo: controlsButton.topAnchor, constant: -1),

            controlsBody.topAnchor.constraint(equalTo: controlsButton.bottomAnchor, constant: 6),
            controlsBody.trailingAnchor.constraint(equalTo: trailingAnchor),
            controlsBody.widthAnchor.constraint(lessThanOrEqualToConstant: 280),

            // Stacked below the controls panel. Both stay laid out, so with the
            // controls closed the metadata panel floats lower — which is fine
            // and was preferred over co-locating them.
            metaTopBelowButton,
            metadataBody.trailingAnchor.constraint(equalTo: trailingAnchor),
            metadataBody.widthAnchor.constraint(lessThanOrEqualToConstant: 300),

            // The panel must be at least as tall as whichever body is taller.
            // Without this its height is unconstrained and collapses, and since
            // AppKit stops hit-testing at a view's bounds the buttons would
            // still draw but stop responding to clicks.
            bottomAnchor.constraint(greaterThanOrEqualTo: controlsBody.bottomAnchor),
            bottomAnchor.constraint(greaterThanOrEqualTo: metadataBody.bottomAnchor),
            leadingAnchor.constraint(lessThanOrEqualTo: controlsBody.leadingAnchor),
            leadingAnchor.constraint(lessThanOrEqualTo: metadataBody.leadingAnchor),
        ])

        // Hug the content, so the panel is no larger than the taller body and
        // does not claim space over the image that it is not using.
        for c in [heightAnchor.constraint(equalToConstant: 0),
                  widthAnchor.constraint(equalToConstant: 0)] {
            c.priority = .defaultLow
            c.isActive = true
        }
    }

    /// Both panels stay laid out at all times and are shown by opacity.
    ///
    /// Toggling `isHidden` changed the intrinsic size, which forced a layout
    /// pass on a *remote* view — the extension's view is hosted out of process,
    /// so that is cross-process IPC on every click, and it is what made the
    /// buttons feel slow. Opacity costs a compositing change and nothing else.
    override func hitTest(_ point: NSPoint) -> NSView? {
        // A closed (invisible) panel is skipped, not just refused: the info
        // panel now sits under the buttons, on top of the open controls, and
        // refusing a click there dropped it, so every control it covered went
        // dead (0.3.2, found by hand 2026-10-09). Front to back, like AppKit.
        let local = convert(point, from: superview)
        for v in subviews.reversed() where !v.isHidden && v !== badgeDot {
            if (v === controlsBody && !controlsExpanded) ||
               (v === metadataBody && !metadataExpanded) {
                continue
            }
            if let hit = v.hitTest(local) { return hit }
        }
        // Nothing open here: the click belongs to the image.
        return nil
    }

    // MARK: - Actions

    @objc private func toggleControls() {
        setPanels(controls: !controlsExpanded, metadata: metadataExpanded)
    }

    @objc private func toggleMetadata() {
        setPanels(controls: controlsExpanded, metadata: !metadataExpanded)
    }

    /// Opacity and button state, plus moving the info panel when it is shown
    /// and the controls opened or closed above it.
    private func setPanels(controls: Bool, metadata: Bool) {
        if metadata && controls != metadataBelowControls {
            metadataBelowControls = controls
            // Deactivate before activating, so the two never both hold.
            (controls ? metaTopBelowButton : metaTopBelowControls).isActive = false
            (controls ? metaTopBelowControls : metaTopBelowButton).isActive = true
        }
        controlsExpanded = controls
        metadataExpanded = metadata
        controlsBody.alphaValue = controls ? 1 : 0
        metadataBody.alphaValue = metadata ? 1 : 0
        controlsButton.state = controls ? .on : .off
        metadataButton.state = metadata ? .on : .off
    }

    /// What the view popup shows when nothing is picked: the committed default
    /// for imagery, raw for a data pass. Mirrors the C rule, so the popup names
    /// what is actually on screen.
    private func automaticView(forLayerAt index: Int) -> String {
        let isData = index >= 0 && index < layerIsData.count && layerIsData[index]
        return isData ? "raw" : EXRPreferences.defaultView
    }

    private func selectView(_ id: String) {
        if let idx = viewIDs.firstIndex(of: id) { viewPopup.selectItem(at: idx) }
    }

    @objc private func controlChanged(_ sender: Any?) {
        // Rounding a small negative value gives -0.0, which showed as "-0.0" and
        // was carried as an override. Zero is zero.
        var stops = Float((exposureSlider.doubleValue * 10).rounded() / 10)
        if stops == 0 { stops = 0 }
        settings.exposureStops = stops
        exposureLabel.stringValue = Self.exposureText(stops)

        // Picking the layer §6.3 would choose anyway is not an override.
        let li = layerPopup.indexOfSelectedItem
        let layer = (li >= 0 && li < layerIDs.count) ? layerIDs[li] : nil
        settings.layer = (layer == autoLayer) ? nil : layer
        // With no alpha the button shows RGB but the carried choice is kept.
        let seg = channelControl.selectedSegment
        if hasAlpha {
            settings.channel = (seg >= 0 && seg < Self.channelOrder.count)
                ? Self.channelOrder[seg] : EXR_VIEW_RGB
        }

        // The view follows the layer -- raw for a data pass, the default for
        // imagery -- unless the user picked something else. Picking what
        // automatic would show is not an override. Only the view popup itself
        // may set a view: on any other change it still shows the previous
        // automatic choice, which must not be mistaken for a pick.
        let automatic = automaticView(forLayerAt: li)
        if (sender as? NSPopUpButton) === viewPopup {
            let vi = viewPopup.indexOfSelectedItem
            let picked = (vi >= 0 && vi < viewIDs.count) ? viewIDs[vi] : nil
            settings.view = (picked == automatic) ? nil : picked
        } else {
            if settings.view == automatic { settings.view = nil }
            if settings.view == nil { selectView(automatic) }
        }

        // Likewise, explicitly choosing the space that would apply anyway --
        // the file's own, or the assumed default for an untagged file -- is
        // not an override. A file with unnamed custom primaries has no such
        // equivalent, so every named choice on it is a real override.
        let si = spacePopup.indexOfSelectedItem
        let space = (si >= 0 && si < spaceChoices.count) ? spaceChoices[si] : nil
        let wouldApply = fileStatesItsPrimaries
            ? fileColorspaceID
            : EXRPreferences.assumedInputColorspace
        settings.inputColorspace = (space == wouldApply) ? nil : space

        settings.alphaOverChecker = checkerToggle.state == .on
        settings.useDataWindow = dataWindowToggle.state == .on

        updateBadge()
        onChange(settings)
    }

    @objc private func resetTapped() {
        exposureSlider.doubleValue = 0
        channelControl.selectedSegment = 0
        spacePopup.selectItem(at: 0)               // no override
        if let idx = layerIDs.firstIndex(of: autoLayer) { layerPopup.selectItem(at: idx) }
        selectView(automaticView(forLayerAt: layerPopup.indexOfSelectedItem))
        checkerToggle.state = .off
        dataWindowToggle.state = .off
        exposureLabel.stringValue = Self.exposureText(0)
        settings = .default
        updateBadge()
        onResetRequested()
    }

    /// Re-seeds the controls after the settings were changed from another
    /// preview. Quick Look builds neighbouring files ahead of time, so their
    /// panels were seeded before the change happened.
    func sync(to s: EXRRenderer.Settings) {
        let layer = s.layer.flatMap { layerIDs.firstIndex(of: $0) }
            ?? layerIDs.firstIndex(of: autoLayer)
        if let layer { layerPopup.selectItem(at: layer) }
        seed(from: s)
    }

    /// nil selects the "no override" entry.
    private func selectSpace(_ id: String?) {
        let idx = id.flatMap { want in spaceChoices.firstIndex(where: { $0 == want }) } ?? 0
        spacePopup.selectItem(at: idx)
    }

    /// Seeds the controls from settings carried over from the previous file, so
    /// the panel shows what is actually being rendered rather than defaults.
    private func seed(from carried: EXRRenderer.Settings) {
        var s = carried
        // A view carried from another layer may be this layer's automatic one.
        if s.view == automaticView(forLayerAt: layerPopup.indexOfSelectedItem) { s.view = nil }
        exposureSlider.doubleValue = Double(s.exposureStops)
        exposureLabel.stringValue = Self.exposureText(s.exposureStops)
        showChannel(s.channel)
        selectView(s.view ?? automaticView(forLayerAt: layerPopup.indexOfSelectedItem))
        selectSpace(s.inputColorspace)
        checkerToggle.state = s.alphaOverChecker ? .on : .off
        dataWindowToggle.state = s.useDataWindow ? .on : .off
        // Adopt the carried settings and badge them now. Without this a
        // carried override rendered correctly but showed no badge until the
        // first control change -- an invisible override, which §8 forbids.
        settings = s
        updateBadge()
    }

    private func updateBadge() {
        let active = settings.isOverridden
        badgeDot.isHidden = !active
        controlsButton.contentTintColor = active ? .systemOrange : nil
        controlsButton.toolTip = active
            ? String(format: "Overrides active (exposure %+.1f). Click to adjust or reset.",
                     settings.exposureStops)
            : "Display options"
    }
}
