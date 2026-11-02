#include "renderer_jpeg.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <limits>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "../../third_party/stb/stb_image_write.h"
#include "core/ascii_case.h"
#include "core/os.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace obl::renderer_jpeg {
std::vector<uint8_t> ToRgb(const uint16_t* bits, int pitchPixels, int width, int height, bool rgb555)
{
    if (!bits || width <= 0 || height <= 0 || pitchPixels < width || width > 65535 || height > 65535) return {};
    if (static_cast<size_t>(width) * height > static_cast<size_t>(std::numeric_limits<int>::max()) / 3) return {};
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned pixel = bits[static_cast<size_t>(y) * pitchPixels + x];
            const unsigned r = (pixel >> (rgb555 ? 10 : 11)) & 31;
            const unsigned g = (pixel >> 5) & (rgb555 ? 31 : 63);
            const unsigned b = pixel & 31;
            auto* out = rgb.data() + (static_cast<size_t>(y) * width + x) * 3;
            out[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
            out[1] = static_cast<uint8_t>(rgb555 ? (g << 3) | (g >> 2) : (g << 2) | (g >> 4));
            out[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
        }
    }
    return rgb;
}

std::vector<uint8_t> Encode(const uint16_t* bits, int pitchPixels, int width, int height, bool rgb555, int quality)
{
    auto rgb = ToRgb(bits, pitchPixels, width, height, rgb555);
    if (rgb.empty()) return {};
    std::vector<uint8_t> jpeg;
    auto append = [](void* context, void* data, int size) {
        auto& out = *static_cast<std::vector<uint8_t>*>(context);
        auto* bytes = static_cast<uint8_t*>(data);
        out.insert(out.end(), bytes, bytes + size);
    };
    if (!stbi_write_jpg_to_func(append, &jpeg, width, height, 3, rgb.data(), quality)) return {};
    return jpeg;
}

bool Write(const char* path, const uint16_t* bits, int pitchPixels, int width, int height, bool rgb555, int quality)
{
    auto jpeg = Encode(bits, pitchPixels, width, height, rgb555, quality);
    if (!path || !*path || jpeg.empty()) return false;
    std::ofstream file(os::ResolvePath(path), std::ios::binary);
    file.write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
    file.close();
    return !file.fail();
}

bool WriteBgr(const char* path, const void* bits, int width, int height, int quality)
{
    if (!path || !*path || !bits || width <= 0 || height <= 0 || width > 65535 || height > 65535) return false;
    if (static_cast<size_t>(width) * height > static_cast<size_t>(std::numeric_limits<int>::max()) / 3) return false;
    const auto* bgr = static_cast<const uint8_t*>(bits);
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0; i < rgb.size(); i += 3) {
        rgb[i] = bgr[i + 2];
        rgb[i + 1] = bgr[i + 1];
        rgb[i + 2] = bgr[i];
    }
    std::vector<uint8_t> jpeg;
    auto append = [](void* context, void* data, int size) {
        auto& out = *static_cast<std::vector<uint8_t>*>(context);
        auto* bytes = static_cast<uint8_t*>(data);
        out.insert(out.end(), bytes, bytes + size);
    };
    if (!stbi_write_jpg_to_func(append, &jpeg, width, height, 3, rgb.data(), std::clamp(quality, 1, 100))) return false;
    std::ofstream file(os::ResolvePath(path), std::ios::binary);
    file.write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
    file.close();
    return !file.fail();
}

std::string NextPhotoName(const std::vector<std::string>& names)
{
    int highest = -1;
    const std::string prefix = "BeachLife_Snapshot_";
    for (auto name : names) {
        for (char& c : name) c = ascii::Lower(c);
        // DOS '?' also matches an empty position before the dot. The number is read from the last eight bytes.
        if (name.size() < prefix.size() + 4 || name.size() > prefix.size() + 8 || name.compare(0, prefix.size(), "beachlife_snapshot_") ||
                name.compare(name.size() - 4, 4, ".jpg"))
            continue;
        highest = std::max(highest, std::atoi(name.c_str() + name.size() - 8));
    }
    char name[64];
    std::snprintf(name, sizeof(name), "BeachLife_Snapshot_%04d.jpg", highest + 1);
    return name;
}
} // namespace obl::renderer_jpeg
