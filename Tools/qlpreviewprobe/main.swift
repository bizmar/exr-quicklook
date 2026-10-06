// Hosts a real QLPreviewView and points it at each file in turn. QLPreviewView
// is the same class the Finder preview pane, column view and the spacebar
// panel use, and it hosts the preview extension out of process.
//
// The extension's view is a remote view, so it cannot be read back into a
// bitmap by this process. Two ways to observe it:
//   - the unified log:
//       /usr/bin/log stream --predicate 'subsystem == "io.github.bizmar.exr-quicklook"'
//   - --hold, which parks the window at a known position so a screen capture
//     can be taken of it. This avoids driving Finder with blind keystrokes.
import AppKit
import Quartz

var paths: [String] = []
var holdSeconds: Double = 0
var argv = Array(CommandLine.arguments.dropFirst())
while let arg = argv.first {
    argv.removeFirst()
    switch arg {
    case "--hold":
        holdSeconds = Double(argv.first ?? "") ?? 30
        if !argv.isEmpty { argv.removeFirst() }
    default:
        paths.append(arg)
    }
}
guard !paths.isEmpty else {
    FileHandle.standardError.write(Data("usage: qlpreviewprobe [--hold SECONDS] <file>...\n".utf8))
    exit(2)
}

let app = NSApplication.shared
// --hold is for visual inspection, so the window must be able to become
// frontmost; a click-safety guard refuses to act otherwise.
app.setActivationPolicy(holdSeconds > 0 ? .regular : .accessory)

let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 600),
                      styleMask: [.titled, .closable],
                      backing: .buffered, defer: false)
guard let preview = QLPreviewView(frame: window.contentLayoutRect, style: .normal) else {
    print("QLPreviewView could not be created"); exit(1)
}
preview.autoresizingMask = [.width, .height]
window.contentView = preview
window.title = "qlpreviewprobe"
window.setFrameOrigin(NSPoint(x: 60, y: 60))
window.orderFrontRegardless()
if holdSeconds > 0 {
    window.makeKeyAndOrderFront(nil)
    app.activate(ignoringOtherApps: true)
}

var remaining = paths
func next() {
    guard !remaining.isEmpty else {
        if holdSeconds > 0 {
            // Park the window so a screen capture can be taken of it. Printing
            // the frame lets the caller target the capture precisely instead of
            // grabbing the whole screen.
            let f = window.frame
            print(String(format: "HOLD %.0f,%.0f,%.0f,%.0f",
                         f.origin.x, f.origin.y, f.size.width, f.size.height))
            fflush(stdout)
            DispatchQueue.main.asyncAfter(deadline: .now() + holdSeconds) {
                print("done"); exit(0)
            }
        } else {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                print("done"); exit(0)
            }
        }
        return
    }
    let p = remaining.removeFirst()
    let url = URL(fileURLWithPath: p)
    print("  -> \(url.lastPathComponent)")
    preview.previewItem = url as NSURL
    preview.refreshPreviewItem()
    DispatchQueue.main.asyncAfter(deadline: .now() + 3.0, execute: next)
}

DispatchQueue.main.asyncAfter(deadline: .now() + 0.5, execute: next)
app.run()
