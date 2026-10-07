// Header inspection. Plan §6.2: extract everything describable without
// decoding a single pixel. Every dimension is validated against exr_limits.h
// before it is trusted.
#pragma once

#include "EXRCore/exr_layers.h"
#include "EXRCore/exr_limits.h"

#include <string>
#include <vector>

namespace exrcore {

struct PartDetail {
    Box2i data_window;
    Box2i display_window;
    std::string compression;       // human-readable, incl. DWA level if present
    std::string type_name;         // scanlineimage / tiledimage / deep*
    bool has_chromaticities = false;
    float chromaticities[8] = {};  // rx ry gx gy bx by wx wy
    std::string chromaticities_name;  // "ACEScg", "ACES2065-1", "Rec.709" or ""
    // A colour space a renderer recorded by name, and the attribute it came
    // from ("arnold/color_space"). Empty when absent.
    std::string writer_colorspace;
    std::string writer_colorspace_attr;
    bool has_preview = false;
    float pixel_aspect_ratio = 1.0f;
    int64_t pixel_count = 0;
};

struct FileInfo {
    std::vector<PartInfo> parts;      // channels + part type, for layer selection
    std::vector<PartDetail> details;  // parallel to parts
    std::vector<std::string> attributes;  // "name = value" for the metadata panel
};

// Returns false and sets `error` on anything malformed, out of bounds, or
// otherwise not worth trusting. Never throws.
[[nodiscard]] bool inspect_file(const std::string& path, FileInfo& out, std::string& error);

// Names a chromaticities set if it matches a known primary set within a small
// tolerance. Empty string when unrecognised.
[[nodiscard]] std::string name_chromaticities(const float c[8]);

}  // namespace exrcore
