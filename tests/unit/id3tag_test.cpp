// ID3v1 reader (src/core/id3tag.h): synthetic blocks and files, the shipped soundtrack as oracle.

#include "unit_test.h"

#include "core/id3tag.h"
#include "temp_dir.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace obl::id3;

namespace {

// ReadTag joins directory and file name by plain concatenation, so the directory carries its own separator.
const std::string kSeparator(1, static_cast<char>(std::filesystem::path::preferred_separator));

std::vector<uint8_t> Block(const char* title, const char* artist)
{
    std::vector<uint8_t> block(kTagSize, 0);
    std::memcpy(block.data(), "TAG", 3);
    std::strncpy(reinterpret_cast<char*>(block.data() + 3), title, 30);
    std::strncpy(reinterpret_cast<char*>(block.data() + 33), artist, 30);
    return block;
}

std::filesystem::path WriteFile(const char* name, const std::vector<uint8_t>& prefix, const std::vector<uint8_t>& tail)
{
    auto path = obl::test::UniqueTempPath(name, ".mp3");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
    out.write(reinterpret_cast<const char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
    return path;
}

} // namespace

OBL_TEST_CASE("id3tag.parses_title_and_artist")
{
    Id3v1Text text;
    OBL_CHECK(ParseTag(Block("Bopalong", "Kinobe").data(), text));
    OBL_CHECK_EQ(std::string(text.title), "Bopalong");
    OBL_CHECK_EQ(std::string(text.artist), "Kinobe");
}

OBL_TEST_CASE("id3tag.fields_are_cut_at_30_bytes_and_at_nul")
{
    auto block = Block(std::string(40, 't').c_str(), "");
    std::memset(block.data() + 33, 'a', 30); // the artist fills its field up to the comment that follows
    block[63] = 'c';
    Id3v1Text text;
    OBL_CHECK(ParseTag(block.data(), text));
    OBL_CHECK_EQ(std::strlen(text.title), size_t(30));
    OBL_CHECK_EQ(std::strlen(text.artist), size_t(30));
    OBL_CHECK_EQ(text.title[30], '\0');
    OBL_CHECK_EQ(text.artist[30], '\0');
    // Bytes after the first NUL belong to nobody.
    block[4] = '\0';
    block[5] = 'x';
    OBL_CHECK(ParseTag(block.data(), text));
    OBL_CHECK_EQ(std::strlen(text.title), size_t(1));
}

OBL_TEST_CASE("id3tag.rejects_blocks_without_the_signature")
{
    auto block = Block("a", "b");
    block[2] = 'Q';
    Id3v1Text text;
    std::memcpy(text.title, "keep", 5);
    OBL_CHECK(!ParseTag(block.data(), text));
    OBL_CHECK_EQ(std::string(text.title), "keep");
}

OBL_TEST_CASE("id3tag.reads_the_last_128_bytes_of_a_file")
{
    std::vector<uint8_t> audio(3000, 0x55);
    // A tag-looking block earlier in the file must not matter, only the end counts.
    auto early = Block("Early", "Early");
    std::copy(early.begin(), early.end(), audio.begin() + 100);
    auto path = WriteFile("obl_id3_test", audio, Block("Night", "Artist"));
    Id3v1Text text;
    const auto dir = path.parent_path().string() + kSeparator;
    OBL_CHECK(ReadTag(dir.c_str(), path.filename().string().c_str(), text));
    OBL_CHECK_EQ(std::string(text.title), "Night");
    OBL_CHECK_EQ(std::string(text.artist), "Artist");

    auto without = WriteFile("obl_id3_test_none", audio, std::vector<uint8_t>(200, 0x41));
    OBL_CHECK(!ReadTag(dir.c_str(), without.filename().string().c_str(), text));
    OBL_CHECK(!ReadTag(dir.c_str(), "obl_id3_missing.mp3", text));
    std::filesystem::remove(path);
    std::filesystem::remove(without);
}

OBL_TEST_CASE("id3tag.short_files_have_no_tag")
{
    auto block = Block("t", "a");
    auto path = WriteFile("obl_id3_test_short", {}, std::vector<uint8_t>(block.begin(), block.begin() + 100));
    Id3v1Text text;
    const auto dir = path.parent_path().string() + kSeparator;
    OBL_CHECK(!ReadTag(dir.c_str(), path.filename().string().c_str(), text));
    std::filesystem::remove(path);
}

OBL_TEST_CASE("data.id3tag.shipped_soundtrack")
{
    const std::filesystem::path file = std::filesystem::path(OBL_GAME_DIR) / "Data" / "soundtrack" / "Day1.mp3";
    if (!std::filesystem::exists(file)) OBL_SKIP("run/Data/soundtrack/Day1.mp3 is missing");
    Id3v1Text text;
    const auto dir = file.parent_path().string() + kSeparator;
    OBL_CHECK(ReadTag(dir.c_str(), "Day1.mp3", text));
    OBL_CHECK_EQ(std::string(text.title), "Bopalong");
    OBL_CHECK_EQ(std::string(text.artist), "Kinobe");
}
