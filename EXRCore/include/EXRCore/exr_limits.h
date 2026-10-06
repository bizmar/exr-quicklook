// Hard bounds and checked arithmetic. See plan §6.6.
//
// CVE-2026-28977 in Apple's decoder was an unchecked allocation-size
// multiplication with no upper bound on image dimensions, and its one overflow
// check guarded the wrong product. Every size computation in EXRCore goes
// through these helpers. Reject, never clamp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace exrcore {

// Chosen to be generous for real production frames and still far below any
// value that could overflow a size_t computation on a 64-bit host.
struct Limits {
    static constexpr int64_t kMaxDimension   = 65535;        // per axis
    static constexpr int64_t kMaxPixels      = 300'000'000;  // ~4x 8K
    static constexpr int64_t kMaxChannels    = 1024;         // per part
    static constexpr int64_t kMaxParts       = 256;
    static constexpr int64_t kMaxTiles       = 4'000'000;
    static constexpr int64_t kMaxBytes       = 4LL << 30;    // 4 GiB decode ceiling
    static constexpr int     kDecodeThreads  = 3;            // sandboxed appex
    static constexpr int64_t kDeadlineMillis = 2000;         // plan §6.7 hard ceiling
};

// Checked arithmetic. Each returns false on overflow or on a negative operand;
// callers must treat false as "reject this file", never as "use a default".
[[nodiscard]] inline bool checked_mul(int64_t a, int64_t b, int64_t& out) {
    if (a < 0 || b < 0) return false;
    return !__builtin_mul_overflow(a, b, &out);
}

[[nodiscard]] inline bool checked_add(int64_t a, int64_t b, int64_t& out) {
    if (a < 0 || b < 0) return false;
    return !__builtin_add_overflow(a, b, &out);
}

// Inclusive box, as EXR stores dataWindow/displayWindow.
struct Box2i {
    int32_t min_x = 0, min_y = 0, max_x = -1, max_y = -1;

    [[nodiscard]] bool valid() const { return max_x >= min_x && max_y >= min_y; }

    // Width/height as int64 to keep the +1 from overflowing int32 at extremes.
    [[nodiscard]] bool extent(int64_t& w, int64_t& h) const {
        if (!valid()) return false;
        if (!checked_add(int64_t(max_x) - int64_t(min_x), 1, w)) return false;
        if (!checked_add(int64_t(max_y) - int64_t(min_y), 1, h)) return false;
        return w <= Limits::kMaxDimension && h <= Limits::kMaxDimension;
    }

    // Total pixel count, bounded. False means reject.
    [[nodiscard]] bool pixel_count(int64_t& n) const {
        int64_t w = 0, h = 0;
        if (!extent(w, h)) return false;
        if (!checked_mul(w, h, n)) return false;
        return n <= Limits::kMaxPixels;
    }
};

// Bytes needed for `pixels` samples of `bytes_per_sample` across `channels`.
// False means the file is rejected before a single byte is allocated.
[[nodiscard]] inline bool buffer_bytes(int64_t pixels, int64_t channels,
                                       int64_t bytes_per_sample, int64_t& out) {
    int64_t per_pixel = 0;
    if (!checked_mul(channels, bytes_per_sample, per_pixel)) return false;
    if (!checked_mul(pixels, per_pixel, out)) return false;
    return out <= Limits::kMaxBytes;
}

}  // namespace exrcore
