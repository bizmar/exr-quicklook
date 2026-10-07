import SwiftUI

@main
struct EXRPreviewApp: App {
    var body: some Scene {
        Window("EXR Quick Look", id: "main") {
            AppWindow()
                .frame(width: 520)
                .fixedSize(horizontal: false, vertical: true)
        }
        // Sized to its content: the window has no settings to grow into, and a
        // fixed minimum height left it mostly empty once those were removed.
        .windowResizability(.contentSize)
    }
}

/// The host app exists because app extensions can only ship inside one
/// (plan §2.1). Its job is to say what the extensions do, how to switch them on,
/// and how to use the preview overlay — nothing more.
///
/// It deliberately offers no rendering preferences. macOS will not let the
/// extensions read shared settings without a Developer ID, so any control here
/// would look effective and do nothing. The preference plumbing is kept in
/// `EXRPreferences` for a fork that signs its own build; see phase1-status.md.
/// Everything adjustable lives in the preview overlay, where it works.
struct AppWindow: View {
    private static let extensionsPane = URL(
        string: "x-apple.systempreferences:com.apple.ExtensionsPreferences")!

    private var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "—"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            header
            Divider()
            enablingSection
            Divider()
            usingSection
            Divider()
            notesSection
        }
        .padding(22)
    }

    private var header: some View {
        HStack(alignment: .center, spacing: 14) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 64, height: 64)
            VStack(alignment: .leading, spacing: 3) {
                Text("EXR Quick Look").font(.title2.weight(.semibold))
                Text("Finder thumbnails and previews for OpenEXR, rendered through "
                     + "the ACES 2.0 output transform.")
                    .font(.callout).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                Text("Version \(version)")
                    .font(.caption).foregroundStyle(.tertiary)
            }
        }
    }

    private var enablingSection: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Turn it on").font(.headline)
            Text("Switch on both EXR Quick Look extensions under Login Items & "
                 + "Extensions → Quick Look. Finder will not use them until you do.")
                .font(.callout).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Button("Open Login Items & Extensions") {
                NSWorkspace.shared.open(Self.extensionsPane)
            }
        }
    }

    private var usingSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Using the preview").font(.headline)
            tip("space",
                "Select an EXR in Finder and press Space. The column view and the "
                + "Finder preview pane show the same image.")
            tip("slider.horizontal.3",
                "The slider button in the preview's corner opens exposure, layer, "
                + "alpha, view transform and input colourspace. An orange dot on it means "
                + "something differs from the defaults.")
            tip("info.circle",
                "The info button shows the file's compression, colourspace, "
                + "channels and data and display windows.")
            tip("square.3.layers.3d",
                "Depth, position and motion passes are in the layer list, marked "
                + "“data”. They show untransformed (Raw) unless you pick a view; use "
                + "exposure to bring depth into range.")
            tip("arrow.down.square",
                "Overrides carry to the next file as you arrow through a folder, so a "
                + "misapplied colourspace on a sequence is fixed once, not per frame. "
                + "Reset clears them; they also lapse after 30 minutes idle.")
        }
    }

    private var notesSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Good to know").font(.headline)
            tip("clock.arrow.circlepath",
                "macOS caches thumbnails. After installing or updating, existing "
                + "thumbnails may not change until you open a folder you have not "
                + "visited this session, or log out and back in.")
            tip("square.on.square.dashed",
                "Alpha is ignored by default, so images show as the comp shows them.")
            tip("exclamationmark.triangle",
                "Deep images, and files that fail to decode, keep the generic icon "
                + "rather than showing something approximate.")
        }
    }

    private func tip(_ symbol: String, _ text: String) -> some View {
        Label {
            Text(text).fixedSize(horizontal: false, vertical: true)
        } icon: {
            Image(systemName: symbol).frame(width: 18)
        }
        .font(.callout)
        .foregroundStyle(.secondary)
    }
}
