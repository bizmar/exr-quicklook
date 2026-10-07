// The layer rules, run against how real applications name things.
//
// Input: Tests/Fixtures/realworld/channels.tsv -- part and channel names from
// openly licensed production and test files (see SOURCES.md there). Names only;
// the rules never look at pixels.
//
// Two kinds of check:
//   1. Invariants that must hold for every file, whatever it is:
//        - if a file has any ordinary imagery, the automatic choice is imagery,
//          never a data pass;
//        - cryptomatte is never the automatic choice;
//        - every offered layer names channels the part actually has.
//   2. A snapshot: the full classification of every file, compared with
//      expected.txt. A rule change shows up as a reviewable diff of its effect
//      on real files. Run with --update to accept a change deliberately.
//
// usage: test_realworld <channels.tsv> <expected.txt> [--update]
#include "EXRCore/exr_layers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace exrcore;

struct File {
    std::string id, source;
    std::vector<PartInfo> parts;
};

static std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

static PartType part_type(const std::string& t) {
    if (t.find("deep") != std::string::npos) {
        return t.find("tile") != std::string::npos ? PartType::kDeepTiled : PartType::kDeepScanline;
    }
    return t.find("tile") != std::string::npos ? PartType::kTiled : PartType::kScanline;
}

static std::vector<File> load(const char* path) {
    std::vector<File> files;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto f = split_tabs(line);
        if (f[0] == "F" && f.size() >= 3) {
            files.push_back({f[1], f[2], {}});
        } else if (f[0] == "P" && !files.empty() && f.size() >= 3) {
            PartInfo p;
            p.name = f[1];
            p.type = part_type(f[2]);
            if (f.size() >= 4) p.color_interop_id = f[3];
            files.back().parts.push_back(p);
        } else if (f[0] == "C" && !files.empty() && !files.back().parts.empty() && f.size() >= 2) {
            files.back().parts.back().channels.push_back(ChannelInfo{f[1], PixelType::kHalf, 1, 1});
        }
    }
    return files;
}

static bool is_crypto(const std::string& s) {
    std::string l = s;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return std::tolower(c); });
    return l.find("crypto") != std::string::npos;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_realworld <channels.tsv> <expected.txt> [--update]\n");
        return 2;
    }
    const bool update = argc > 3 && std::string(argv[3]) == "--update";
    const std::vector<File> files = load(argv[1]);
    int failures = 0, checks = 0;
    auto fail = [&](const File& f, const std::string& why) {
        std::printf("  FAIL  %s: %s\n", f.id.c_str(), why.c_str());
        ++failures;
    };

    std::ostringstream snap;
    for (const File& f : files) {
        const auto opts = enumerate_layers(f.parts);
        const LayerSelection sel = select_primary_layer(f.parts);

        const LayerOption* chosen = nullptr;
        for (const auto& o : opts) {
            if (o.selection.part_index == sel.part_index && o.selection.layer_name == sel.layer_name &&
                o.selection.r == sel.r) chosen = &o;
        }
        const bool has_imagery = std::any_of(opts.begin(), opts.end(),
                                             [](const LayerOption& o) { return !o.data_pass; });

        ++checks;
        if (sel.valid() && has_imagery && chosen && chosen->data_pass) {
            fail(f, "automatic choice is a data pass although imagery exists: " + chosen->label);
        }
        // Cryptomatte is only ever the automatic choice in a cryptomatte-only
        // file, and then only its un-numbered colour preview, never a hash layer.
        ++checks;
        if (sel.valid() && (is_crypto(sel.layer_name) ||
                            is_crypto(f.parts[static_cast<std::size_t>(sel.part_index)].name))) {
            const bool crypto_only = std::all_of(opts.begin(), opts.end(), [&](const LayerOption& o) {
                return is_crypto(o.selection.layer_name) ||
                       is_crypto(f.parts[static_cast<std::size_t>(o.selection.part_index)].name);
            });
            const bool hash_layer = !sel.layer_name.empty() &&
                                    std::isdigit(static_cast<unsigned char>(sel.layer_name.back()));
            if (!crypto_only || hash_layer) fail(f, "cryptomatte chosen automatically: " + sel.layer_name);
        }
        for (const auto& o : opts) {
            ++checks;
            std::set<std::string> have;
            for (const auto& c : f.parts[static_cast<std::size_t>(o.selection.part_index)].channels) have.insert(c.name);
            for (const std::string* ch : {&o.selection.r, &o.selection.g, &o.selection.b, &o.selection.a}) {
                if (!ch->empty() && !have.count(*ch)) fail(f, "layer " + o.label + " names missing channel " + *ch);
            }
        }

        snap << f.id << "\n";
        snap << "  auto  " << (sel.valid() ? (chosen ? chosen->label : sel.layer_name) : "(none -- generic icon)") << "\n";
        std::string image, data;
        for (const auto& o : opts) (o.data_pass ? data : image) += (o.label + ", ");
        if (!image.empty()) snap << "  image " << image.substr(0, image.size() - 2) << "\n";
        if (!data.empty()) snap << "  data  " << data.substr(0, data.size() - 2) << "\n";
    }

    const std::string now = snap.str();
    if (update) {
        std::ofstream(argv[2]) << now;
        std::printf("  wrote %s (%zu files)\n", argv[2], files.size());
    } else {
        std::ifstream in(argv[2]);
        std::stringstream was;
        was << in.rdbuf();
        ++checks;
        if (was.str() != now) {
            std::printf("  FAIL  classification differs from %s -- run with --update and review the diff\n", argv[2]);
            ++failures;
        }
    }
    std::printf("\n  real world: %zu files, %d checks, %d failures\n", files.size(), checks, failures);
    return failures ? 1 : 0;
}
