#include "importer/src/writers/media_convert.h"

#include "importer/src/source/source.h"
#include "core/ascii_case.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <system_error>
#include <vector>

namespace obl::convert {

namespace {

// The magic bytes that open a file of each kind; an MP3 is an ID3v2 tag or an MPEG audio frame sync (11 set bits).
bool StartsLikeMp3(const std::array<uint8_t, 4>& head)
{
    if (head[0] == 'I' && head[1] == 'D' && head[2] == '3') return true;
    return head[0] == 0xFF && (head[1] & 0xE0) == 0xE0;
}

bool StartsLikeMpeg(const std::array<uint8_t, 4>& head)
{
    return head[0] == 0x00 && head[1] == 0x00 && head[2] == 0x01 && head[3] == 0xBA;
}

// The file name of a track: lowercase ASCII, the extension dropped by the caller; false when it is not a content key.
bool TrackName(const std::string& fileName, std::string& name)
{
    name = fileName;
    for (char& c : name) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
        c = ascii::Lower(c);
    }
    if (name.empty()) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// Copies one file after its head is checked. A track or the intro is tens of MB at most, so it is read whole: the
// browser sink takes whole files anyway.
bool CopyChecked(const ByteSource& source, const std::string& from, PackSink& out, const std::string& to,
        bool (*looksLike)(const std::array<uint8_t, 4>&), const char* kind, std::string& error)
{
    std::array<uint8_t, 4> head{};
    if (!source.Read(from, 0, head) || !looksLike(head)) {
        error = source.Describe(from) + " is not " + kind;
        return false;
    }
    const auto bytes = source.ReadAll(from);
    if (!bytes) {
        error = "cannot copy " + source.Describe(from) + " to " + out.Describe(to);
        return false;
    }
    return out.Write(to, *bytes, error);
}

} // namespace

bool ConvertMedia(const ByteSource& source, PackSink& out, int& files, std::string& error)
{
    files = 0;
    try {
        const auto soundtrack = FindFile(source, "Data/Soundtrack");
        const auto intro = FindFile(source, "Data/Movies/EidosMovie.mpg");
        const auto entries = soundtrack ? source.List(*soundtrack) : std::nullopt;
        if (!entries || !intro) {
            error = "missing installation Data/Soundtrack or Data/Movies/EidosMovie.mpg";
            return false;
        }
        for (const SourceEntry& entry : *entries) {
            if (entry.directory) continue;
            // Extension and stem as std::filesystem splits them: a leading dot starts the stem, not an extension.
            const size_t dot = entry.name.rfind('.');
            std::string extension = dot == std::string::npos || dot == 0 ? std::string() : entry.name.substr(dot);
            for (char& c : extension) c = ascii::Lower(c);
            if (extension != ".mp3") continue;
            std::string name;
            if (!TrackName(entry.name.substr(0, dot), name)) {
                error = "track " + entry.name + " has a name that is not a content key";
                return false;
            }
            if (!CopyChecked(source, *soundtrack + "/" + entry.name, out, "music/" + name + ".mp3", StartsLikeMp3, "an MP3", error))
                return false;
            ++files;
        }
        if (!CopyChecked(source, *intro, out, "video/intro.mpg", StartsLikeMpeg, "an MPEG-1 program stream", error)) return false;
        ++files;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::convert
