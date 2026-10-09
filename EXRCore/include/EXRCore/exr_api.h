// C API over EXRCore, for the Swift app extensions.
//
// The appexes never see C++. Two entry points matter:
//
//   exr_render        one-shot: decode + transform. Used by the thumbnail,
//                     which renders once and never adjusts anything.
//   exr_open / exr_source_render
//                     decode once, re-transform many times. Used by the
//                     preview, where dragging an exposure slider must not
//                     re-decode a 6K frame per event.
//
// Both funnel through the same transform code, so the thumbnail and a preview
// with no override active produce identical pixels (decision D7).
#ifndef EXRCORE_EXR_API_H
#define EXRCORE_EXR_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Which channels reach the display. Plan §8 "channel isolation".
typedef enum {
    EXR_VIEW_RGB = 0,
    EXR_VIEW_RED,
    EXR_VIEW_GREEN,
    EXR_VIEW_BLUE,
    EXR_VIEW_ALPHA,
    EXR_VIEW_LUMINANCE,
} EXRChannelView;

typedef struct EXRRenderOptions {
    int32_t max_edge;          // longest-edge cap in pixels; 0 = natural size
    float exposure_stops;      // manual only; D8 forbids auto-exposure
    // Explicit view transform: a baked LUT id, or "raw" for none at all. NULL
    // means automatic -- `default_view` for imagery, raw for a data pass
    // (depth, position, motion...), since the ACES curve makes data unreadable
    // (plan §8). Callers badge any non-NULL value that differs from automatic.
    const char* view;

    int32_t channel_view;      // EXRChannelView
    int32_t alpha_over_checker;// 1 = composite over a checkerboard, preview only
    int32_t use_data_window;   // 1 = show overscan instead of cropping (D6)

    // Explicit input-primaries override, applied whether or not the file states
    // its own (D9, amended 2026-09-06: a misapplied profile is baked into every
    // frame of a sequence, so the user must be able to correct a tagged file).
    // NULL = no override. Callers badge any non-NULL value.
    const char* input_colorspace;

    // D9(a): the primaries to assume for a file that states none, when there is
    // no override. NULL = ACEScg. Never applied to a file that states its own.
    const char* assumed_colorspace;

    // D9(b): the committed default view for imagery when `view` is NULL.
    // NULL = the shipped default (ACES 2.0 SDR 100 nits, P3 D65).
    const char* default_view;
} EXRRenderOptions;

typedef struct EXRRenderResult {
    int32_t width;
    int32_t height;
    uint16_t* pixels;          // width*height*4, 16-bit RGBA, Display P3 (D5)
} EXRRenderResult;

// One-shot render. Returns 1 on success, 0 on any failure -- callers must fall
// back to the generic icon, never render something approximate.
int exr_render(const char* path, const EXRRenderOptions* options, EXRRenderResult* out);
void exr_render_free(EXRRenderResult* result);

// Why the last exr_render or exr_open on the calling thread failed, for logs.
// Empty after a success. Valid until the next call on that thread.
const char* exr_last_error(void);

// A decoded file, held so the transform can be re-applied cheaply.
typedef struct EXRSource EXRSource;

// Decodes once. `max_edge` and `use_data_window` are fixed for the lifetime of
// the source because they change the decode itself; everything else in
// EXRRenderOptions can vary per call to exr_source_render.
// `layer` selects which layer to decode, using an id from exr_source_layer_id.
// NULL means the automatic choice (plan §6.3). An id that is not present in the
// file falls back to the automatic choice rather than failing, which is what
// makes sticky-by-name selection safe across a sequence.
EXRSource* exr_open(const char* path, int32_t max_edge, int32_t use_data_window,
                    const char* layer);

// The layer switcher's contents. Deep parts are absent. Data passes --
// cryptomatte, depth, position, motion, normals, masks and bare channels -- are
// present but flagged: §6.3 says they stay listed and never become the default
// while the file has imagery, and they render raw unless a view is chosen.
int exr_source_layer_count(const EXRSource* source);
const char* exr_source_layer_id(const EXRSource* source, int index);
const char* exr_source_layer_label(const EXRSource* source, int index);
int exr_source_layer_is_data(const EXRSource* source, int index);
// The id actually in use, so the picker can show what is being rendered.
const char* exr_source_active_layer(const EXRSource* source);
// The id the automatic selection (§6.3) would choose. Lets the caller tell an
// explicit choice from the default, so picking the default is not an override.
const char* exr_source_auto_layer(const EXRSource* source);
void exr_close(EXRSource* source);
int exr_source_render(EXRSource* source, const EXRRenderOptions* options,
                      EXRRenderResult* out);

// 1 when the file states its own chromaticities. The input-colourspace override
// is meaningless then, and the UI should say so rather than silently ignoring
// the control.
int exr_source_has_chromaticities(const EXRSource* source);
// 1 when the decoded layer has an alpha channel. Without one the alpha view
// shows RGB, and the overlay disables its Alpha button.
int exr_source_has_alpha(const EXRSource* source);

// The file's own colourspace, named where recognised ("ACES2065-1 (AP0)"), or
// "custom chromaticities" when it states primaries we do not have a name for,
// or "" when it states none. The overlay shows this instead of the assumed
// default when the file speaks for itself, so the two never disagree on screen.
const char* exr_source_chromaticities_name(const EXRSource* source);

// Tab-separated "label\tvalue" lines for the metadata panel. Returns 1 on
// success. Writes a NUL-terminated UTF-8 string truncated to `len`.
int exr_describe(const char* path, char* buffer, size_t len);

// View transforms for the overlay picker: the baked LUTs, then "raw".
int exr_view_count(void);
const char* exr_view_id(int index);
const char* exr_view_display_name(int index);
const char* exr_default_view_id(void);

// Named input colour spaces, for the D9 override picker.
int exr_colorspace_count(void);
const char* exr_colorspace_id(int index);
const char* exr_colorspace_display_name(int index);
const char* exr_default_colorspace_id(void);

#ifdef __cplusplus
}
#endif
#endif
