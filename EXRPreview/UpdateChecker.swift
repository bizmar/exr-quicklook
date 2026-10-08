import Foundation
import os

/// Asks GitHub once, when the app window opens, whether a newer release
/// exists. Nothing is downloaded or installed: an available update is shown
/// with a link to its release page, and the user installs it like the first
/// time. The app is unsigned, so macOS would ask them to approve it again
/// anyway, and a silent self-installer would be a far bigger thing to trust.
///
/// Only the host app does this. The Quick Look extensions have no network
/// access at all and never will.
enum UpdateChecker {

    static let latestReleaseAPI =
        URL(string: "https://api.github.com/repos/bizmar/exr-quicklook/releases/latest")!

    /// Release pages we are willing to open. The link comes from the network,
    /// so anything else is ignored rather than handed to the browser.
    static let releasePagePrefix = "https://github.com/bizmar/exr-quicklook/releases/"

    /// The user's choice, in the host app's own defaults. On unless turned off.
    static let enabledKey = "checkForUpdates"

    private static let log = Logger(subsystem: "io.github.bizmar.exr-quicklook", category: "update")

    struct Release: Equatable {
        let version: String   // "0.3.0", without the tag's "v"
        let page: URL
    }

    enum Outcome: Equatable {
        case upToDate
        case available(Release)
        case failed
    }

    /// The parts of GitHub's "latest release" answer that matter. GitHub
    /// already excludes drafts and pre-releases from this endpoint; they are
    /// rejected here too in case that ever changes.
    static func parse(_ data: Data) -> Release? {
        struct Payload: Decodable {
            let tag_name: String
            let html_url: String
            let draft: Bool?
            let prerelease: Bool?
        }
        guard let p = try? JSONDecoder().decode(Payload.self, from: data),
              p.draft != true, p.prerelease != true,
              p.html_url.hasPrefix(releasePagePrefix),
              let page = URL(string: p.html_url)
        else { return nil }
        let version = p.tag_name.hasPrefix("v") ? String(p.tag_name.dropFirst()) : p.tag_name
        guard components(version) != nil else { return nil }
        return Release(version: version, page: page)
    }

    /// "1.2.3" -> [1, 2, 3]. Two or three numeric parts, nothing else.
    static func components(_ version: String) -> [Int]? {
        let parts = version.split(separator: ".", omittingEmptySubsequences: false)
        guard (2...3).contains(parts.count) else { return nil }
        let numbers = parts.compactMap { part -> Int? in
            guard !part.isEmpty, part.allSatisfy(\.isASCII), part.allSatisfy(\.isNumber) else { return nil }
            return Int(part)
        }
        return numbers.count == parts.count ? numbers : nil
    }

    static func isNewer(_ candidate: String, than current: String) -> Bool {
        guard var a = components(candidate), var b = components(current) else { return false }
        while a.count < 3 { a.append(0) }
        while b.count < 3 { b.append(0) }
        return b.lexicographicallyPrecedes(a)
    }

    static func outcome(for data: Data, current: String) -> Outcome {
        guard let release = parse(data) else { return .failed }
        return isNewer(release.version, than: current) ? .available(release) : .upToDate
    }

    /// One HTTPS request: no cookies, no cache, a short timeout. Any failure
    /// (offline, rate-limited, unexpected answer) is reported as `.failed`
    /// and shown quietly; it never blocks the window.
    static func check(current: String) async -> Outcome {
        var request = URLRequest(url: latestReleaseAPI, timeoutInterval: 10)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("EXR-Quick-Look/\(current)", forHTTPHeaderField: "User-Agent")
        let session = URLSession(configuration: .ephemeral)
        defer { session.finishTasksAndInvalidate() }
        let data: Data, status: Int
        do {
            let (d, response) = try await session.data(for: request)
            data = d
            status = (response as? HTTPURLResponse)?.statusCode ?? 0
        } catch {
            log.info("update check failed: \(error.localizedDescription, privacy: .public)")
            return .failed
        }
        guard status == 200 else {
            log.info("update check failed: HTTP \(status)")
            return .failed
        }
        let result = outcome(for: data, current: current)
        log.info("update check: \(String(describing: result), privacy: .public)")
        return result
    }
}
