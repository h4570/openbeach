// image-load <path>: loads one TGA/TIFF through the game's loader entry (images::LoadTga/LoadTiff, the C++ decoders) and logs
// the size and a 32-bit hash of the result.

#include <new>

#include "image_load.h"
#include "game/ui/image.h"

#include "game/game/game_accessors.h"
#include "core/ascii_case.h"
#include "core/image_decode.h"
#include "core/log.h"

namespace obl::test {

bool ExecuteImageLoadCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "image-load" || args.size() != 1) return false;
    std::string ext = args[0].substr(args[0].find_last_of('.') == std::string::npos ? args[0].size() : args[0].find_last_of('.'));
    for (char& c : ext) c = ascii::Lower(c);
    using Load = void*(__cdecl*)(const char*);
    Load loader = ext == ".tga" ? &ui::images::LoadTga : (ext == ".tif" || ext == ".tiff") ? &ui::images::LoadTiff : nullptr;
    if (!loader) {
        OBL_LOG("autotest: FAILED image-load: unsupported extension '%s'", ext.c_str());
        return true;
    }
    void* object = loader(args[0].c_str());
    image::Image image = ui::images::Read(object);
    uint64_t hash = image::Hash(image);
    OBL_LOG("autotest: image-load %s loaded=%d width=%d height=%d bpp=%d palette=%d hash=%u", args[0].c_str(), object ? 1 : 0, image.width,
            image.height, image.bpp, image.hasPalette() ? 1 : 0, static_cast<uint32_t>(hash ^ (hash >> 32)));
    // The object belongs to this command; its other owners release it with images::Free.
    if (object) {
        ui::images::Destruct(object);
        ::operator delete(object);
    }
    return true;
}

} // namespace obl::test
