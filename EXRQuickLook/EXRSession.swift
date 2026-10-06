import Foundation

/// Transient overlay state, carried between files within one preview session.
///
/// EXRs are usually image sequences: a folder of frames sharing one set of
/// settings, and when a profile has been misapplied it is misapplied to all of
/// them. So a correction has to survive arrowing to the next frame, or it has
/// to be redone on every file — which is the workflow this exists for.
///
/// Process-global rather than per-view-controller because Quick Look builds a
/// new controller per file while reusing the extension process. Verified
/// 2026-09-06: three files previewed in succession were handled by a single
/// EXRQuickLook pid, so in-memory state is the hot path and survives arrowing.
/// It is deliberately *not* persisted — these are overrides, not preferences.
///
/// Quick Look also prepares the neighbouring files *before* they are shown
/// (measured 2026-10-06: opening frame 1 prepared frames 2 and N at once), so
/// reading this at prepare time is not enough — a change made on frame 1 would
/// never reach the already-built frame 2. Changes are therefore announced with
/// `didChange`, and every live preview re-syncs.
enum EXRSession {

    /// Posted on the main thread after the session changes. `object` is the
    /// preview that changed it, so it can ignore its own announcement.
    static let didChange = Notification.Name("EXRSessionDidChange")

    /// Plan §8's backstop, so someone does not return hours later to everything
    /// at +2 stops with no memory of setting it.
    static let idleTimeout: TimeInterval = 30 * 60

    private static let lock = NSLock()
    private static var stored: EXRRenderer.Settings?
    private static var touched = Date.distantPast

    /// The carried settings, or nil once the session has gone stale.
    static var current: EXRRenderer.Settings? {
        lock.lock(); defer { lock.unlock() }
        guard let stored, Date().timeIntervalSince(touched) < idleTimeout else { return nil }
        return stored
    }

    static func remember(_ settings: EXRRenderer.Settings, from sender: AnyObject?) {
        lock.lock()
        stored = settings
        touched = Date()
        lock.unlock()
        NotificationCenter.default.post(name: didChange, object: sender)
    }

    static func clear(from sender: AnyObject?) {
        lock.lock()
        stored = nil
        touched = .distantPast
        lock.unlock()
        NotificationCenter.default.post(name: didChange, object: sender)
    }
}
