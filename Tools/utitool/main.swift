// Prints the UTI LaunchServices assigns to each path, and which Quick Look
// content types that UTI conforms to. Used by Tools/status.sh to confirm
// the control file is really typed as our own UTI and not sniffed as EXR.
import Foundation
import UniformTypeIdentifiers

for arg in CommandLine.arguments.dropFirst() {
    let url = URL(fileURLWithPath: arg)
    let name = url.lastPathComponent
    guard let t = try? url.resourceValues(forKeys: [.contentTypeKey]).contentType else {
        print("  \(name): no content type"); continue
    }
    let flags = [t.isDeclared ? "declared" : "undeclared", t.isDynamic ? "dynamic" : "static"]
    print("  \(name)")
    print("      UTI:      \(t.identifier)  (\(flags.joined(separator: ", ")))")
    print("      conforms: \(t.supertypes.map(\.identifier).sorted().joined(separator: ", "))")
}
