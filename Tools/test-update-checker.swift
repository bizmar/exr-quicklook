// Checks UpdateChecker's parsing and version logic, then asks the real GitHub
// API once. Run by Tools/test-all.sh:
//   swiftc -parse-as-library EXRPreview/UpdateChecker.swift Tools/test-update-checker.swift
import Foundation

var failures = 0
func expect(_ ok: Bool, _ what: String) {
    print("  \(ok ? "ok  " : "FAIL")  \(what)")
    if !ok { failures += 1 }
}

func json(_ tag: String, _ url: String, draft: Bool = false, pre: Bool = false) -> Data {
    Data("""
    {"tag_name": "\(tag)", "html_url": "\(url)", "draft": \(draft), "prerelease": \(pre)}
    """.utf8)
}
let page = "https://github.com/bizmar/exr-quicklook/releases/tag/v0.3.0"

@main
struct Main {
    static func main() async {
        typealias U = UpdateChecker
        expect(U.isNewer("0.3.0", than: "0.2.0"), "0.3.0 is newer than 0.2.0")
        expect(U.isNewer("0.10.0", than: "0.9.1"), "numeric, not alphabetical: 0.10.0 > 0.9.1")
        expect(U.isNewer("1.0", than: "0.9.9"), "two-part versions compare")
        expect(!U.isNewer("0.2.0", than: "0.2.0"), "the same version is not an update")
        expect(!U.isNewer("0.1.9", than: "0.2.0"), "an older version is not an update")
        expect(!U.isNewer("0.3.0-beta", than: "0.2.0"), "a non-numeric version is ignored")

        expect(U.outcome(for: json("v0.3.0", page), current: "0.2.0") ==
               .available(.init(version: "0.3.0", page: URL(string: page)!)), "a newer release is offered")
        expect(U.outcome(for: json("v0.2.0", page), current: "0.2.0") == .upToDate, "the current release is up to date")
        expect(U.outcome(for: json("v9.0.0", "https://evil.example/x"), current: "0.2.0") == .failed,
               "a link outside this repository's releases is refused")
        expect(U.outcome(for: json("v9.0.0", page, draft: true), current: "0.2.0") == .failed, "a draft is refused")
        expect(U.outcome(for: json("v9.0.0", page, pre: true), current: "0.2.0") == .failed, "a pre-release is refused")
        expect(U.outcome(for: Data("not json".utf8), current: "0.2.0") == .failed, "garbage is a quiet failure")

        // The real thing, once. Offline is not a test failure.
        let live = await U.check(current: "0.0.1")
        switch live {
        case .available(let r): print("  live: latest release is \(r.version) at \(r.page)")
        case .upToDate: print("  live: unexpectedly up to date"); failures += 1
        case .failed: print("  live: GitHub not reachable (skipped)")
        }
        print("  update checker: \(failures) failures")
        exit(failures == 0 ? 0 : 1)
    }
}
