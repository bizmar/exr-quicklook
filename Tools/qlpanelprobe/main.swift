// Drives the real Quick Look panel -- the one Finder's spacebar opens -- over a
// list of files, stepping to the next item every few seconds, the way pressing
// the down arrow does. Unlike QLPreviewView (qlpreviewprobe), the panel manages
// several items, so it shows how Quick Look prepares neighbours.
//
// Observe with:
//   /usr/bin/log stream --info --predicate 'subsystem == "io.github.bizmar.exr-quicklook"'
import AppKit
import Quartz

let urls = CommandLine.arguments.dropFirst().map { URL(fileURLWithPath: $0) }
guard !urls.isEmpty else {
    FileHandle.standardError.write(Data("usage: qlpanelprobe <file>...\n".utf8))
    exit(2)
}
let step = 4.0

final class Controller: NSObject, NSApplicationDelegate, QLPreviewPanelDataSource {
    let window = NSWindow(contentRect: NSRect(x: 80, y: 80, width: 200, height: 80),
                          styleMask: [.titled], backing: .buffered, defer: false)

    func applicationDidFinishLaunching(_ note: Notification) {
        window.title = "qlpanelprobe"
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
            QLPreviewPanel.shared().makeKeyAndOrderFront(nil)
            print("  panel open at item 0: \(urls[0].lastPathComponent)"); fflush(stdout)
            self.advance(to: 1)
        }
    }

    func advance(to i: Int) {
        DispatchQueue.main.asyncAfter(deadline: .now() + step) {
            guard i < urls.count else {
                QLPreviewPanel.shared().orderOut(nil)
                print("done"); exit(0)
            }
            QLPreviewPanel.shared().currentPreviewItemIndex = i
            print("  -> item \(i): \(urls[i].lastPathComponent)"); fflush(stdout)
            self.advance(to: i + 1)
        }
    }

    // The panel finds its controller through the responder chain; the app
    // delegate is the last stop on it.
    override func acceptsPreviewPanelControl(_ panel: QLPreviewPanel!) -> Bool { true }
    override func beginPreviewPanelControl(_ panel: QLPreviewPanel!) { panel.dataSource = self }
    override func endPreviewPanelControl(_ panel: QLPreviewPanel!) {}

    func numberOfPreviewItems(in panel: QLPreviewPanel!) -> Int { urls.count }
    func previewPanel(_ panel: QLPreviewPanel!, previewItemAt index: Int) -> QLPreviewItem! {
        urls[index] as NSURL
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let controller = Controller()
app.delegate = controller
app.run()
