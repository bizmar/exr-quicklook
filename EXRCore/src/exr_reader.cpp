#include "EXRCore/exr_reader.h"

#include <ImfChannelList.h>
#include <ImfChromaticities.h>
#include <ImfCompression.h>
#include <ImfFloatAttribute.h>
#include <ImfStringAttribute.h>
#include <ImfHeader.h>
#include <ImfMultiPartInputFile.h>
#include <ImfPartType.h>
#include <ImfStandardAttributes.h>
#include <ImfTileDescription.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>

namespace exrcore {
namespace {

const char* compression_name(Imf::Compression c) {
    switch (c) {
        case Imf::NO_COMPRESSION:    return "none";
        case Imf::RLE_COMPRESSION:   return "RLE";
        case Imf::ZIPS_COMPRESSION:  return "ZIPS";
        case Imf::ZIP_COMPRESSION:   return "ZIP";
        case Imf::PIZ_COMPRESSION:   return "PIZ";
        case Imf::PXR24_COMPRESSION: return "PXR24";
        case Imf::B44_COMPRESSION:   return "B44";
        case Imf::B44A_COMPRESSION:  return "B44A";
        case Imf::DWAA_COMPRESSION:  return "DWAA";
        case Imf::DWAB_COMPRESSION:  return "DWAB";
        default:                     return "unknown";
    }
}

PixelType convert(Imf::PixelType t) {
    switch (t) {
        case Imf::UINT:  return PixelType::kUInt;
        case Imf::FLOAT: return PixelType::kFloat;
        case Imf::HALF:
        default:         return PixelType::kHalf;
    }
}

PartType part_type_of(const Imf::Header& h) {
    if (!h.hasType()) return PartType::kScanline;
    const std::string& t = h.type();
    if (t == Imf::DEEPSCANLINE) return PartType::kDeepScanline;
    if (t == Imf::DEEPTILE)     return PartType::kDeepTiled;
    if (t == Imf::TILEDIMAGE)   return PartType::kTiled;
    return PartType::kScanline;
}

Box2i convert(const Imath::Box2i& b) {
    Box2i out;
    out.min_x = b.min.x; out.min_y = b.min.y;
    out.max_x = b.max.x; out.max_y = b.max.y;
    return out;
}

bool close_to(float a, float b) { return std::fabs(a - b) < 0.001f; }

// The memory OpenEXR needs for one part, which our own buffer checks never
// see: one decompressed chunk holds every channel of the part, and a tiled
// part read through the scanline interface caches a full-width row of tiles.
// Also enforces the tile count. False means reject the file.
bool chunk_within_limits(const Imf::Header& h, const Box2i& dw, std::string& why) {
    int64_t w = 0, ht = 0;
    if (!dw.extent(w, ht)) { why = "dataWindow"; return false; }

    int64_t pixel_bytes = 0;   // all channels, ignoring subsampling: an upper bound
    for (auto it = h.channels().begin(); it != h.channels().end(); ++it) {
        const int64_t size = it.channel().type == Imf::HALF ? 2 : 4;
        if (!checked_add(pixel_bytes, size, pixel_bytes)) { why = "channel bytes"; return false; }
    }

    int64_t unit = 0, cache = 0;
    if (h.hasTileDescription()) {
        const Imf::TileDescription& t = h.tileDescription();
        const int64_t tw = t.xSize, th = t.ySize;
        if (tw <= 0 || th <= 0) { why = "tile size"; return false; }
        // Level 0, then the other levels: a mip pyramid adds under 1/3, a
        // rip map under 3x.
        const int64_t levels = t.mode == Imf::ONE_LEVEL ? 1
                             : t.mode == Imf::MIPMAP_LEVELS ? 2 : 4;
        int64_t tiles = 0;
        if (!checked_mul((w + tw - 1) / tw, (ht + th - 1) / th, tiles) ||
            !checked_mul(tiles, levels, tiles) || tiles > Limits::kMaxTiles) {
            why = "tile count";
            return false;
        }
        // A tile is clipped to the data window, so a tile larger than the
        // image costs only the image. The cached row is up to four float
        // slices (R, G, B, A) across the full width.
        const int64_t cw = std::min(tw, w), ch = std::min(th, ht);
        if (!checked_mul(cw, ch, unit) || !checked_mul(w, ch, cache) ||
            !checked_mul(cache, 4 * int64_t(sizeof(float)), cache)) {
            why = "tile size";
            return false;
        }
    } else if (!checked_mul(std::min<int64_t>(Imf::getCompressionNumScanlines(h.compression()), ht),
                            w, unit)) {
        why = "chunk size";
        return false;
    }
    int64_t chunk = 0;
    if (!checked_mul(unit, pixel_bytes, chunk) ||
        chunk > Limits::kMaxChunkBytes || cache > Limits::kMaxChunkBytes) {
        why = "decompressed chunk size";
        return false;
    }
    return true;
}

// OpenEXR's own size caps, so it rejects an absurd window or tile while
// reading the header, before allocating anything. Ours still apply after.
// Process-wide defaults, so set exactly once.
void set_library_limits() {
    static std::once_flag once;
    std::call_once(once, [] {
        const int d = static_cast<int>(Limits::kMaxDimension);
        Imf::Header::setMaxImageSize(d, d);
        Imf::Header::setMaxTileSize(d, d);
    });
}

bool matches(const float c[8], const float ref[8]) {
    for (int i = 0; i < 8; ++i) {
        if (!close_to(c[i], ref[i])) return false;
    }
    return true;
}

}  // namespace

std::string name_chromaticities(const float c[8]) {
    static const float kACEScg[8]  = {0.713f, 0.293f, 0.165f, 0.830f,
                                      0.128f, 0.044f, 0.32168f, 0.33767f};
    static const float kACES2065[8] = {0.7347f, 0.2653f, 0.0f, 1.0f,
                                       0.0001f, -0.077f, 0.32168f, 0.33767f};
    static const float kRec709[8]  = {0.64f, 0.33f, 0.30f, 0.60f,
                                      0.15f, 0.06f, 0.3127f, 0.3290f};
    static const float kP3D65[8]   = {0.680f, 0.320f, 0.265f, 0.690f,
                                      0.150f, 0.060f, 0.3127f, 0.3290f};
    static const float kRec2020[8] = {0.708f, 0.292f, 0.170f, 0.797f,
                                      0.131f, 0.046f, 0.3127f, 0.3290f};
    if (matches(c, kACEScg))  return "ACEScg (AP1)";
    if (matches(c, kACES2065)) return "ACES2065-1 (AP0)";
    if (matches(c, kRec709))  return "Rec.709 / sRGB";
    if (matches(c, kP3D65))   return "P3-D65";   // the picker's label, so the two match
    if (matches(c, kRec2020)) return "Rec.2020";
    return "";
}

bool inspect_file(const std::string& path, FileInfo& out, std::string& error) {
    error.clear();
    set_library_limits();
    try {
        Imf::MultiPartInputFile file(path.c_str());
        const int parts = file.parts();
        if (parts <= 0 || parts > Limits::kMaxParts) {
            error = "part count out of range: " + std::to_string(parts);
            return false;
        }

        for (int i = 0; i < parts; ++i) {
            const Imf::Header& h = file.header(i);

            PartInfo info;
            info.name = h.hasName() ? h.name() : std::string();
            info.type = part_type_of(h);

            int64_t channel_count = 0;
            for (auto it = h.channels().begin(); it != h.channels().end(); ++it) {
                if (++channel_count > Limits::kMaxChannels) {
                    error = "channel count exceeds limit in part " + std::to_string(i);
                    return false;
                }
                const Imf::Channel& c = it.channel();
                info.channels.push_back(ChannelInfo{
                    it.name(), convert(c.type), c.xSampling, c.ySampling});
            }

            PartDetail detail;
            detail.data_window = convert(h.dataWindow());
            detail.display_window = convert(h.displayWindow());
            if (!detail.data_window.pixel_count(detail.pixel_count)) {
                error = "dataWindow rejected by limits in part " + std::to_string(i);
                return false;
            }
            int64_t dw = 0, dh = 0;
            if (!detail.display_window.extent(dw, dh)) {
                error = "displayWindow rejected by limits in part " + std::to_string(i);
                return false;
            }
            std::string why;
            if (!chunk_within_limits(h, detail.data_window, why)) {
                error = why + " rejected by limits in part " + std::to_string(i);
                return false;
            }
            detail.compression = compression_name(h.compression());
            // Read the attribute itself rather than Header::dwaCompressionLevel(),
            // which returns the encoder default (45) when the file never stated
            // a level -- the info panel would then report a level the file does
            // not contain. The free functions that checked presence are
            // deprecated in OpenEXR 3.4.
            if (h.compression() == Imf::DWAA_COMPRESSION ||
                h.compression() == Imf::DWAB_COMPRESSION) {
                if (const auto* lvl =
                        h.findTypedAttribute<Imf::FloatAttribute>("dwaCompressionLevel")) {
                    // Converting an out-of-range or NaN float to int is
                    // undefined behaviour, and the value comes from the file.
                    const float v = lvl->value();
                    if (std::isfinite(v) && v >= 0.0f && v <= 1e6f) {
                        detail.compression += " (level " +
                            std::to_string(static_cast<int>(v)) + ")";
                    }
                }
            }
            detail.type_name = h.hasType() ? h.type() : "scanlineimage";
            detail.pixel_aspect_ratio = h.pixelAspectRatio();
            detail.has_preview = h.hasPreviewImage();

            // OpenEXR 3.4's Color Interop Forum ID. A hostile file could put
            // anything here; an implausibly long one is not a real ID.
            if (Imf::hasColorInteropID(h)) {
                const std::string& id = Imf::colorInteropID(h);
                if (id.size() <= 128) info.color_interop_id = id;
            }

            // A renderer's own record of its working colour space. Arnold
            // writes the OCIO name; nothing else is read for now.
            if (const auto* cs = h.findTypedAttribute<Imf::StringAttribute>("arnold/color_space")) {
                if (cs->value().size() <= 128) {
                    detail.writer_colorspace = cs->value();
                    detail.writer_colorspace_attr = "arnold/color_space";
                }
            }

            if (Imf::hasChromaticities(h)) {
                const Imf::Chromaticities& ch = Imf::chromaticities(h);
                const float vals[8] = {ch.red.x, ch.red.y, ch.green.x, ch.green.y,
                                       ch.blue.x, ch.blue.y, ch.white.x, ch.white.y};
                detail.has_chromaticities = true;
                for (int k = 0; k < 8; ++k) detail.chromaticities[k] = vals[k];
                detail.chromaticities_name = name_chromaticities(vals);
            }

            out.parts.push_back(std::move(info));
            out.details.push_back(std::move(detail));
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown error while reading header";
        return false;
    }
}

}  // namespace exrcore
