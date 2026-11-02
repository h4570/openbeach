#include "core/id3tag.h"
#include "core/os.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace obl::id3 {
namespace {
void CopyField(char (&dest)[kFieldSize + 1], const uint8_t* source)
{
    std::memset(dest, 0, sizeof(dest));
    std::strncpy(dest, reinterpret_cast<const char*>(source), kFieldSize);
}
} // namespace

bool ParseTag(const uint8_t* block, Id3v1Text& out)
{
    if (std::memcmp(block, "TAG", 3) != 0) return false;
    CopyField(out.title, block + 3);
    CopyField(out.artist, block + 33);
    return true;
}

bool ReadTag(const char* directory, const char* file, Id3v1Text& out)
{
    const std::string path = std::string(directory) + file;
    FILE* fp = os::OpenFile(path, "rb");
    if (!fp) return false;
    uint8_t block[kTagSize];
    const bool whole = std::fseek(fp, -static_cast<long>(kTagSize), SEEK_END) == 0 && std::fread(block, kTagSize, 1, fp) == 1;
    std::fclose(fp);
    return whole && ParseTag(block, out);
}

} // namespace obl::id3
