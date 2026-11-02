#include "unit_test.h"
#include "importer/src/writers/media_convert.h"
#include "importer/src/source/source.h"
#include "temp_dir.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

void WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

std::vector<uint8_t> ReadAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// An ID3-tagged MP3 and an MPEG-1 program stream, each with a payload that makes the copy checkable byte by byte.
std::vector<uint8_t> Mp3(const char* magic, size_t total)
{
    std::vector<uint8_t> bytes(total);
    for (size_t i = 0; i < total; ++i) bytes[i] = uint8_t(i * 7 + 3);
    std::memcpy(bytes.data(), magic, std::strlen(magic));
    return bytes;
}

std::vector<uint8_t> Mpeg(size_t total)
{
    std::vector<uint8_t> bytes(total);
    for (size_t i = 0; i < total; ++i) bytes[i] = uint8_t(i * 5 + 1);
    const uint8_t pack[4] = {0x00, 0x00, 0x01, 0xBA};
    std::memcpy(bytes.data(), pack, sizeof(pack));
    return bytes;
}

// A small installation tree in `root`: two tracks (one with an upper-case name) and the intro movie.
void MakeInstallation(const std::filesystem::path& root)
{
    WriteFile(root / "Data" / "Soundtrack" / "Night5.MP3", Mp3("ID3", 300));
    WriteFile(root / "Data" / "Soundtrack" / "day1.mp3", Mp3("\xFF\xFB", 200));
    WriteFile(root / "Data" / "Movies" / "EidosMovie.mpg", Mpeg(500));
}

} // namespace

OBL_TEST_CASE("convert.media_copies_tracks_and_intro")
{
    const obl::test::TempPath installation("obl-media-test-copy");
    const obl::test::TempPath output("obl-media-test-copy-out");
    const auto& root = installation.path;
    const auto& out = output.path;
    MakeInstallation(root);
    std::string error;
    int files = 0;
    OBL_CHECK(ConvertMedia(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), files, error));
    OBL_CHECK(error.empty());
    OBL_CHECK(files == 3);
    // Names are lowercased; every copy is the same bytes as its source.
    OBL_CHECK(ReadAll(out / "music" / "night5.mp3") == ReadAll(root / "Data" / "Soundtrack" / "Night5.MP3"));
    OBL_CHECK(ReadAll(out / "music" / "day1.mp3") == ReadAll(root / "Data" / "Soundtrack" / "day1.mp3"));
    OBL_CHECK(ReadAll(out / "video" / "intro.mpg") == ReadAll(root / "Data" / "Movies" / "EidosMovie.mpg"));
}

OBL_TEST_CASE("convert.media_refuses_wrong_kinds_and_names")
{
    // A track that is not an MP3, a track name that is not a content key, and an intro that is not an MPEG program stream.
    std::string error;
    int files = 0;
    {
        const obl::test::TempPath installation("obl-media-test-bad-mp3");
        const obl::test::TempPath output("obl-media-test-bad-mp3-out");
        MakeInstallation(installation.path);
        WriteFile(installation.path / "Data" / "Soundtrack" / "day2.mp3", Mpeg(100));
        OBL_CHECK(!ConvertMedia(
                *obl::convert::OpenDirectorySource(installation.path), *obl::convert::OpenDirectorySink(output.path), files, error));
        OBL_CHECK(error.find("day2.mp3") != std::string::npos);
    }
    {
        const obl::test::TempPath installation("obl-media-test-bad-name");
        const obl::test::TempPath output("obl-media-test-bad-name-out");
        MakeInstallation(installation.path);
        WriteFile(installation.path / "Data" / "Soundtrack" / "day-2.mp3", Mp3("ID3", 100));
        OBL_CHECK(!ConvertMedia(
                *obl::convert::OpenDirectorySource(installation.path), *obl::convert::OpenDirectorySink(output.path), files, error));
        OBL_CHECK(error.find("content key") != std::string::npos);
    }
    {
        const obl::test::TempPath installation("obl-media-test-bad-intro");
        const obl::test::TempPath output("obl-media-test-bad-intro-out");
        MakeInstallation(installation.path);
        WriteFile(installation.path / "Data" / "Movies" / "EidosMovie.mpg", Mp3("ID3", 100));
        OBL_CHECK(!ConvertMedia(
                *obl::convert::OpenDirectorySource(installation.path), *obl::convert::OpenDirectorySink(output.path), files, error));
        OBL_CHECK(error.find("EidosMovie.mpg") != std::string::npos);
    }
}

OBL_TEST_CASE("data.convert_media_of_installation")
{
    // Every installed track and the intro are copied byte for byte.
    const auto soundtrack = FindInstalledFile(OBL_GAME_DIR, "Data/Soundtrack");
    const auto intro = FindInstalledFile(OBL_GAME_DIR, "Data/Movies/EidosMovie.mpg");
    if (!soundtrack || !intro) OBL_SKIP("no installation music or movie");
    const obl::test::TempPath output("obl-media-test-installation");
    const auto& out = output.path;
    std::string error;
    int files = 0;
    OBL_REQUIRE(ConvertMedia(*obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(out), files, error));
    int tracks = 0;
    for (const auto& entry : std::filesystem::directory_iterator(*soundtrack)) {
        if (entry.path().extension() != ".mp3" && entry.path().extension() != ".MP3") continue;
        std::string name = entry.path().stem().string();
        for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        OBL_CHECK(ReadAll(out / "music" / (name + ".mp3")) == ReadAll(entry.path()));
        ++tracks;
    }
    OBL_CHECK(tracks > 0);
    OBL_CHECK(files == tracks + 1);
    OBL_CHECK(ReadAll(out / "video" / "intro.mpg") == ReadAll(*intro));
}
