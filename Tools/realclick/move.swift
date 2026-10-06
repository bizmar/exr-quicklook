// Moves the pointer (no click) from a to b in small steps, and reports the
// uptime at which it arrived at b. Used to measure hover-event latency.
import CoreGraphics
import Foundation
let a = CommandLine.arguments.dropFirst().compactMap { Double($0) }
guard a.count == 4 else { print("usage: realmove x0 y0 x1 y1"); exit(2) }
let src = CGEventSource(stateID: .hidSystemState)
for i in 0...12 {
    let t = Double(i) / 12
    let p = CGPoint(x: a[0] + (a[2] - a[0]) * t, y: a[1] + (a[3] - a[1]) * t)
    CGEvent(mouseEventSource: src, mouseType: .mouseMoved, mouseCursorPosition: p,
            mouseButton: .left)?.post(tap: .cghidEventTap)
    if i == 12 { print(String(format: "ARRIVED uptime=%.3f", ProcessInfo.processInfo.systemUptime)) }
    usleep(8_000)
}
