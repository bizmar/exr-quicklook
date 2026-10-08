// Renders the host app's window to PNGs in the three update states, so the
// layout can be checked without a newer release existing:
//   swiftc -parse-as-library -D SNAPSHOT EXRPreview/*.swift Shared/EXRPreferences.swift \
//          Tools/snapshot-window.swift -o build/snapshot-window && build/snapshot-window build/
import AppKit
import SwiftUI

@main
struct Snapshot {
    @MainActor static func main() {
        _ = NSApplication.shared
        let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
        let release = UpdateChecker.Release(
            version: "0.3.0",
            page: URL(string: "https://github.com/bizmar/exr-quicklook/releases/tag/v0.3.0")!)
        let states: [(String, UpdateChecker.Outcome?)] =
            [("available", .available(release)), ("uptodate", .upToDate), ("failed", .failed)]
        for (name, outcome) in states {
            let state = UpdateState()
            state.outcome = outcome
            let view = AppWindow(updates: state).frame(width: 520).fixedSize(horizontal: false, vertical: true)
                .background(Color(nsColor: .windowBackgroundColor))
            let renderer = ImageRenderer(content: view)
            renderer.scale = 2
            guard let cg = renderer.cgImage else { print("render failed: \(name)"); continue }
            let rep = NSBitmapImageRep(cgImage: cg)
            let url = URL(fileURLWithPath: out).appendingPathComponent("window-\(name).png")
            try? rep.representation(using: .png, properties: [:])?.write(to: url)
            print("wrote \(url.path)")
        }
    }
}
