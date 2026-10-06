#pragma once
#include <cstdint>
#include <string>
namespace exrcli {
// Writes 8-bit RGBA as an sRGB-tagged PNG. Debug output for the harness only;
// the shipping path emits 16-bit Display P3 (decision D5).
bool write_png(const std::string& path, const std::uint8_t* rgba, int width, int height,
               std::string& error);
}
