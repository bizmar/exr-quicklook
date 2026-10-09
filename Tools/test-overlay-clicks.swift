// Hosts the real PreviewViewController in an ordinary (never shown) window,
// opens and closes the overlay's two panels in every combination, and checks
// that a click at the centre of every visible control reaches that control.
//
//   test-overlay-clicks <file.exr> [expect-alpha: yes|no]
//
// Written after 0.3.2 shipped with every control under the (closed, invisible)
// info panel dead: the overlay's hit test refused clicks there instead of
// passing them on. Screenshots cannot catch that; AppKit's hit test can.
import AppKit
import Quartz

@main
struct OverlayClicks {
    static func main() {
        let app = NSApplication.shared
        app.setActivationPolicy(.prohibited)
        let args = CommandLine.arguments
        guard args.count > 1 else { print("usage: test-overlay-clicks <file.exr> [yes|no]"); exit(2) }
        let path = args[1]
        let expectAlpha = args.count > 2 ? args[2] == "yes" : nil

        let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1200, height: 800),
                           styleMask: [.titled], backing: .buffered, defer: false)
        let vc = PreviewViewController()
        win.contentViewController = vc
        vc.preparePreviewOfFile(at: URL(fileURLWithPath: path)) { error in
            if let error { print("  FAIL  prepare: \(error)"); exit(1) }
        }

        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
            let root = vc.view
            func all<T: NSView>(_ v: NSView, _ t: T.Type) -> [T] {
                ((v as? T).map { [$0] } ?? []) + v.subviews.flatMap { all($0, t) }
            }
            let buttons = all(root, NSButton.self)
            guard let settings = buttons.first(where: { $0.toolTip == "Display options" }),
                  let info = buttons.first(where: { $0.toolTip == "File information" }) else {
                print("  FAIL  overlay buttons not found"); exit(1)
            }
            var failures = 0
            func check(_ state: String) {
                root.layoutSubtreeIfNeeded()
                var reached = 0
                for c in all(root, NSControl.self) where !(c is NSTextField) {
                    var v: NSView? = c, shown = true
                    while let n = v { if n.isHidden || n.alphaValue == 0 { shown = false }; v = n.superview }
                    guard shown else { continue }
                    let f = c.convert(c.bounds, to: root)
                    let p = root.convert(NSPoint(x: f.midX, y: f.midY), to: root.superview)
                    var hit = root.hitTest(p), mine = false
                    while let h = hit { if h === c { mine = true; break }; hit = h.superview }
                    if mine { reached += 1 } else {
                        failures += 1
                        print("  FAIL  \(state): \(type(of: c)) at \(Int(f.minX)),\(Int(f.minY)) does not receive its clicks")
                    }
                }
                print("  ok    \(state): \(reached) controls receive their clicks")
            }
            settings.performClick(nil); check("display options open")
            if let expectAlpha, let seg = all(root, NSSegmentedControl.self).first {
                let enabled = seg.isEnabled(forSegment: 1)
                if enabled == expectAlpha {
                    print("  ok    Alpha button \(enabled ? "enabled" : "disabled"), as the layer \(enabled ? "has" : "lacks") alpha")
                } else {
                    failures += 1
                    print("  FAIL  Alpha button \(enabled ? "enabled" : "disabled") for a layer that \(expectAlpha ? "has" : "lacks") alpha")
                }
            }
            info.performClick(nil); check("both open")
            settings.performClick(nil); check("file info only")
            info.performClick(nil); check("both closed")
            exit(failures == 0 ? 0 : 1)
        }
        app.run()
    }
}
