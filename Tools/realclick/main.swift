// Posts a genuine mouse click (CGEvent down + up) at x,y -- the path a real
// user's click takes, through WindowServer and Quick Look's event forwarding.
//
// System Events' `click at` does NOT do this: it resolves the accessibility
// element under the point and presses it, which skips mouse delivery entirely.
// Latency measured that way says nothing about what a user feels.
import ApplicationServices
import CoreGraphics
import Foundation

let a = CommandLine.arguments
// `realclick --trusted` reports whether posting is permitted, and clicks nothing.
if a.count == 2 && a[1] == "--trusted" { print(AXIsProcessTrusted()); exit(0) }
guard a.count >= 3, let x = Double(a[1]), let y = Double(a[2]) else {
    print("usage: realclick x y [hold-ms]"); exit(2)
}
let holdMs = a.count > 3 ? (UInt32(a[3]) ?? 60) : 60
let p = CGPoint(x: x, y: y)
let src = CGEventSource(stateID: .hidSystemState)
func post(_ t: CGEventType) {
    CGEvent(mouseEventSource: src, mouseType: t, mouseCursorPosition: p,
            mouseButton: .left)?.post(tap: .cghidEventTap)
}
post(.mouseMoved)
usleep(30_000)
// Same clock the extension logs with, so post -> receipt is directly comparable.
let postedAt = ProcessInfo.processInfo.systemUptime
post(.leftMouseDown)
usleep(holdMs * 1000)
let upAt = ProcessInfo.processInfo.systemUptime
post(.leftMouseUp)
print(String(format: "POSTED down=%.3f up=%.3f (held %u ms)", postedAt, upAt, holdMs))
