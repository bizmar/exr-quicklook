import EXRCore
import Foundation
import Security

/// The two committed preferences from decision D9, shared between the host app
/// and both extensions.
///
/// Backed by `UserDefaults(suiteName:)` on the App Group.
///
/// **Known limitation under ad-hoc signing.** The extensions cannot read this;
/// they fall back to the factory defaults. Measured 2026-09-06:
///
/// - The host app writes fine — its value reaches
///   `~/Library/Group Containers/group.io.github.bizmar.exr-quicklook/Library/Preferences/`.
/// - The extension is refused, by *System Policy* rather than by its own sandbox
///   profile: 9 denials for the appex against 0 for the app, on both
///   `file-read-data` and `file-write-create`, for every path in the container.
/// - macOS also gates group containers behind a TCC prompt ("would like to
///   access data from other apps"). Granting it cleared the app's access but the
///   extension was still denied, so the two are separate gates.
///
/// Direct file access is no better — same EPERM — so this is not a matter of
/// choosing a different storage route. It needs a Developer ID, at which point
/// the group identifier gains a team prefix and the entitlement is honoured.
/// Deliberately left as-is: preferences reaching the extensions is a
/// nice-to-have, not a requirement, and the fallback is correct and silent.
enum EXRPreferences {

    static let appGroup = "group.io.github.bizmar.exr-quicklook"

    private static let assumedInputKey = "assumedInputColorspace"
    private static let defaultViewKey = "defaultViewTransform"

    /// Evaluated once: the signing check is not free, and it cannot change
    /// while the process is alive.
    private static let sharingWorks = canReachExtensions

    /// The App Group is only touched when it can actually do something.
    ///
    /// Reaching for it unsigned achieves nothing and makes macOS prompt the user
    /// with "would like to access data from other apps" on every launch — a
    /// permission request for a capability the build cannot use. A fork signed
    /// with its own Developer ID gets the shared store automatically.
    private static var store: UserDefaults? {
        sharingWorks ? UserDefaults(suiteName: appGroup) : .standard
    }

    /// D9(a): assumed input colourspace for files carrying no chromaticities.
    /// A file that states its own primaries always wins over this.
    static var assumedInputColorspace: String {
        get {
            let v = store?.string(forKey: assumedInputKey) ?? factoryInputColorspace
            return EXRRenderer.colorspaces.contains { $0.id == v } ? v : factoryInputColorspace
        }
        set { store?.set(newValue, forKey: assumedInputKey) }
    }

    /// D9(b): the view transform both extensions render with by default.
    static var defaultView: String {
        get {
            let v = store?.string(forKey: defaultViewKey) ?? factoryView
            return EXRRenderer.views.contains { $0.id == v } ? v : factoryView
        }
        set { store?.set(newValue, forKey: defaultViewKey) }
    }

    static var factoryInputColorspace: String {
        exr_default_colorspace_id().map { String(cString: $0) } ?? ""
    }

    static var factoryView: String {
        exr_default_view_id().map { String(cString: $0) } ?? ""
    }

    static var isFactory: Bool {
        assumedInputColorspace == factoryInputColorspace && defaultView == factoryView
    }

    /// Whether preferences can actually reach the extensions.
    ///
    /// Kept, though nothing currently displays it: a fork signed with its own
    /// Developer ID gets working preferences for free, and this is the check
    /// that tells it so. Not surfaced in the UI because this project will not
    /// be signed, and a permanent warning about a permanent condition is just
    /// noise.
    ///
    /// App Groups are honoured for app extensions only when the entitlement is
    /// backed by a Team ID. Ad-hoc signed builds have none, and the system
    /// denies the extension by System Policy while still allowing the host app
    /// — measured 2026-09-06: 9 denials for the appex, 0 for the app. Under
    /// that condition the extensions silently fall back to factory defaults,
    /// so the app needs to say so rather than let a setting look effective.
    static var canReachExtensions: Bool {
        var code: SecCode?
        guard SecCodeCopySelf([], &code) == errSecSuccess, let code else { return false }
        var staticCode: SecStaticCode?
        guard SecCodeCopyStaticCode(code, [], &staticCode) == errSecSuccess,
              let staticCode else { return false }
        var info: CFDictionary?
        guard SecCodeCopySigningInformation(staticCode, SecCSFlags(rawValue: kSecCSSigningInformation),
                                            &info) == errSecSuccess,
              let dict = info as? [String: Any] else { return false }
        let team = dict[kSecCodeInfoTeamIdentifier as String] as? String
        return !(team ?? "").isEmpty
    }

    static func resetToFactory() {
        store?.removeObject(forKey: assumedInputKey)
        store?.removeObject(forKey: defaultViewKey)
    }
}
