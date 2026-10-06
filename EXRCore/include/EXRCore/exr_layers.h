// Primary layer selection. See plan §6.3.
//
// Deliberately free of any OpenEXR type so it can be unit-tested on its own --
// this is the most bug-prone logic in the project and gets the heaviest tests.
#pragma once

#include <string>
#include <vector>

namespace exrcore {

enum class PixelType { kUInt, kHalf, kFloat };

struct ChannelInfo {
    std::string name;              // full EXR name, e.g. "diffuse.R"
    PixelType type = PixelType::kHalf;
    int x_sampling = 1, y_sampling = 1;
};

enum class PartType { kScanline, kTiled, kDeepScanline, kDeepTiled };

struct PartInfo {
    std::string name;              // empty for a single-part file
    PartType type = PartType::kScanline;
    std::vector<ChannelInfo> channels;
    // The OpenEXR 3.4 colorInteropID attribute, "" when absent. "data" marks
    // non-colour content, so every layer of the part is a data pass.
    std::string color_interop_id;

    [[nodiscard]] bool is_deep() const {
        return type == PartType::kDeepScanline || type == PartType::kDeepTiled;
    }
};

// How the selected layer should be interpreted downstream.
enum class LayerKind {
    kNone,        // nothing selectable
    kRGB,         // three colour channels, optional alpha
    kLumaChroma,  // Y / RY / BY, possibly subsampled
    kGrey,        // single channel rendered as greyscale
};

struct LayerSelection {
    LayerKind kind = LayerKind::kNone;
    int part_index = -1;
    std::string layer_name;        // "" for the unprefixed default layer

    // Full channel names, ready to hand to a FrameBuffer. Empty string means
    // the channel is absent.
    std::string r, g, b, a;

    [[nodiscard]] bool valid() const { return kind != LayerKind::kNone; }
    [[nodiscard]] bool has_alpha() const { return !a.empty(); }
};

// Split "char.diffuse.R" into layer "char.diffuse" and base "R".
// A name with no dot has an empty layer and is part of the default layer.
void split_channel(const std::string& full, std::string& layer, std::string& base);

// True for layers that must never be selected automatically: cryptomatte,
// depth, normals, motion vectors, ids and masks. They remain listable in the
// UI; this only governs the default. Plan §6.3.
[[nodiscard]] bool is_never_auto_layer(const std::string& layer_name);

// Preferred "beauty" layer/part names, in the plan's order.
[[nodiscard]] bool is_preferred_name(const std::string& name);

// The whole decision. Returns an invalid selection if nothing is renderable.
[[nodiscard]] LayerSelection select_primary_layer(const std::vector<PartInfo>& parts);

/// One entry in the preview's layer switcher.
struct LayerOption {
    std::string id;        // stable across files: "<part index>:<layer name>"
    std::string label;     // for the picker
    LayerSelection selection;
    bool data_pass = false;  // cryptomatte, depth, normals, masks...
};

/// Every layer a user could ask to see, including the ones that are never
/// chosen automatically — plan §6.3 says those stay listed in the UI, they just
/// never become the default. Deep parts are omitted entirely: they cannot be
/// rendered, so offering them would only produce a dead entry.
[[nodiscard]] std::vector<LayerOption> enumerate_layers(const std::vector<PartInfo>& parts);

}  // namespace exrcore
