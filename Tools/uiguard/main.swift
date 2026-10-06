// Prints "OK x,y,w,h" for the on-screen window owned by <app>, but only when
// that app is frontmost; otherwise "BLOCKED ...". Used by the UI test scripts
// before any synthetic click, so a click can only ever land in our own window
// and never in whatever else happens to be in front.
import AppKit

let want = CommandLine.arguments.dropFirst().first ?? ""
let front = NSWorkspace.shared.frontmostApplication?.localizedName ?? "?"
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements],
                                      kCGNullWindowID) as? [[String: Any]] ?? []
let mine = list.first { w in
    ((w[kCGWindowOwnerName as String] as? String) ?? "") == want
        && ((w[kCGWindowBounds as String] as? [String: CGFloat])?["Width"] ?? 0) > 100
}
guard let mine, let b = mine[kCGWindowBounds as String] as? [String: CGFloat] else {
    print("BLOCKED no on-screen window owned by \(want)"); exit(1)
}
// An accessory-policy process (the probe) is never "frontmost", so for it the
// check is that it is the topmost normal window instead.
let topmost = list.first { (($0[kCGWindowLayer as String] as? Int) ?? 1) == 0 }
let topOwner = (topmost?[kCGWindowOwnerName as String] as? String) ?? "?"
guard front == want || topOwner == want else {
    print("BLOCKED frontmost is \(front), topmost window is \(topOwner), not \(want)"); exit(1)
}
// Optional "--at x,y": also require that the topmost window under that exact
// point, across every layer (alerts and permission dialogs float above normal
// windows), belongs to <app>. Without this a click could land on a system
// dialog sitting over our window -- including on its "Allow" button.
let args = Array(CommandLine.arguments.dropFirst())
if let i = args.firstIndex(of: "--at"), i + 1 < args.count {
    let xy = args[i + 1].split(separator: ",").compactMap { Double($0) }
    if xy.count == 2 {
        let all = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID)
            as? [[String: Any]] ?? []
        let hit = all.first { w in
            guard let r = w[kCGWindowBounds as String] as? [String: CGFloat],
                  ((w[kCGWindowAlpha as String] as? Double) ?? 1) > 0.01 else { return false }
            return CGRect(x: r["X"]!, y: r["Y"]!, width: r["Width"]!, height: r["Height"]!)
                .contains(CGPoint(x: xy[0], y: xy[1]))
        }
        let owner = (hit?[kCGWindowOwnerName as String] as? String) ?? "nothing"
        guard owner == want else {
            print("BLOCKED point \(args[i + 1]) is covered by \(owner), not \(want)"); exit(1)
        }
    }
}
print("OK \(Int(b["X"]!)),\(Int(b["Y"]!)),\(Int(b["Width"]!)),\(Int(b["Height"]!))")
