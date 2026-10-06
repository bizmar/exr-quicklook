#include "png_writer.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

namespace exrcli {

bool write_png(const std::string& path, const std::uint8_t* rgba, int width, int height,
               std::string& error) {
    error.clear();
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!cs) { error = "could not create sRGB colour space"; return false; }

    CGContextRef ctx = CGBitmapContextCreate(
        const_cast<std::uint8_t*>(rgba), static_cast<size_t>(width),
        static_cast<size_t>(height), 8, static_cast<size_t>(width) * 4, cs,
        kCGImageAlphaPremultipliedLast);
    if (!ctx) { CGColorSpaceRelease(cs); error = "could not create bitmap context"; return false; }

    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    if (!image) { error = "could not create image"; return false; }

    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    CGImageDestinationRef dest =
        CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    CFRelease(url);
    if (!dest) { CGImageRelease(image); error = "could not create PNG destination"; return false; }

    CGImageDestinationAddImage(dest, image, nullptr);
    const bool ok = CGImageDestinationFinalize(dest);
    CFRelease(dest);
    CGImageRelease(image);
    if (!ok) error = "PNG write failed";
    return ok;
}

}  // namespace exrcli
