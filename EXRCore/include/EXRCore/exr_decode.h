// Pixel decode. Plan §6.4.
//
// Reads only the channels the chosen layer needs, enforces every bound in
// exr_limits.h before allocating, crops to the display window (decision D6),
// and replaces NaN/Inf with defined values rather than propagating them.
#pragma once

#include "EXRCore/exr_layers.h"
#include "EXRCore/exr_reader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace exrcore {

// Scene-linear RGBA in the file's own primaries. Always sized to the display
// window. Alpha is carried but ignored by the default display path (§6.3).
struct Image {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<float> rgba;  // width * height * 4, row-major, top row first

    [[nodiscard]] bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
};

struct DecodeOptions {
    // Longest-edge cap for the decoded image, in pixels. Zero means full
    // resolution. A thumbnail request for a 6K frame must not materialise the
    // full-resolution float image first, so downsampling happens *during* the
    // band walk, box-filtered, never as a post-pass.
    int32_t max_edge = 0;

    // False crops to the display window (decision D6, the default). True shows
    // the whole data window, which is an inspection aid for overscan plates and
    // is offered only in the preview overlay, never for thumbnails.
    bool use_data_window = false;
};

// Decodes the selected layer from `path`. Returns false with `error` set on
// anything malformed, oversized, or unsupported. Never throws.
[[nodiscard]] bool decode_layer(const std::string& path, const FileInfo& info,
                                const LayerSelection& sel, Image& out, std::string& error,
                                const DecodeOptions& options = {});

}  // namespace exrcore
