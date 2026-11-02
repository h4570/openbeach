// InstallShield 6 cabinet reader of the converter (importer/src/source/cabinet.cpp) and its MD5. The synthetic cabinet is
// hand-made: our own names and contents, compressed as stored deflate blocks (no compressor needed). The "data." case
// identifies CD copies in the installation straight from their cabinets.

#include "unit_test.h"

#include "importer/src/source/cabinet.h"
#include "importer/src/source/cd_source.h"
#include "importer/src/source/md5.h"
#include "importer/src/source/source.h"
#include "importer/web/web_source.h"
#include "core/sha256.h"
#include "og_data_dir.h"
#include "synthetic_exe.h"
#include "synthetic_iso.h"
#include "temp_dir.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

std::span<const uint8_t> Bytes(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

void PutU16(std::vector<uint8_t>& b, size_t at, uint16_t v)
{
    if (b.size() < at + 2) b.resize(at + 2);
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

void PutU32(std::vector<uint8_t>& b, size_t at, uint32_t v)
{
    if (b.size() < at + 4) b.resize(at + 4);
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

void PutU64(std::vector<uint8_t>& b, size_t at, uint64_t v)
{
    PutU32(b, at, static_cast<uint32_t>(v));
    PutU32(b, at + 4, static_cast<uint32_t>(v >> 32));
}

void PutString(std::vector<uint8_t>& b, size_t at, std::string_view s)
{
    if (b.size() < at + s.size() + 1) b.resize(at + s.size() + 1);
    std::memcpy(&b[at], s.data(), s.size());
    b[at + s.size()] = 0;
}

void WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// One chunk of the cabinet's compressed stream: u16 length, then a raw deflate stream of stored blocks. `final` ends
// the stream with BFINAL; otherwise it ends like a sync flush, as the cabinets' chunks do.
std::vector<uint8_t> Chunk(std::string_view data, bool final)
{
    std::vector<uint8_t> block = {static_cast<uint8_t>(final ? 1 : 0)};
    const uint16_t len = static_cast<uint16_t>(data.size());
    PutU16(block, 1, len);
    PutU16(block, 3, static_cast<uint16_t>(~len));
    block.insert(block.end(), data.begin(), data.end());
    std::vector<uint8_t> chunk;
    PutU16(chunk, 0, static_cast<uint16_t>(block.size()));
    chunk.insert(chunk.end(), block.begin(), block.end());
    return chunk;
}

struct Entry {
    std::string directory; // backslash-separated as in the header
    std::string name; // Windows-1252 bytes
    std::string group;
    std::string content;
    uint16_t flags = CabinetFile::kCompressed;
    bool external = false;
};

constexpr uint32_t kDesc = 0x200;
constexpr uint32_t kTable = 0x400; // file table, relative to the descriptor
constexpr uint32_t kDescriptors = 0x300; // file descriptors, relative to the file table
constexpr uint32_t kGroups = 0x2000; // group nodes and descriptors, relative to the descriptor

const std::string kSpriteContent = std::string("2002:08:20 14:19:21", 19) + std::string(1, '\0') + "our own sprite header bytes";

// A CD root with data1.hdr and data1.cab holding our entries.
struct SyntheticCabinet {
    // Each fixture owns a unique directory; Write puts one volume set in a subdirectory of it.
    obl::test::TempPath dir{"obl_convert_cabinet"};
    std::vector<Entry> entries = {
            {"Data\\Sprites", "BLSprite.hdr", "EURO Comp", kSpriteContent},
            {"", "readme.txt", "Docs", "plain stored text", 0},
            {"", "\xC8\x96", "Docs", "", CabinetFile::kInvalid}, // an invalid placeholder with a garbage name
            {"", "BeachLife.exe", "EURO Uncomp", "", 0, true},
            {"", "BeachLife.ini", "Ess_A", "[A]"},
            {"", "BEACHLIFE.ini", "Ess_B", "[B]"},
            {"Docs",
                    "L\xE9"
                    "eme.htm",
                    "Docs", "", 0, true},
    };
    std::vector<uint8_t> hdr, cab;
    std::vector<size_t> descriptorAt; // header offset of each entry's file descriptor
    std::vector<size_t> storedAt; // volume offset of each entry's stored bytes

    SyntheticCabinet()
    {
        PutU32(hdr, 0, 0x28635349);
        PutU32(hdr, 4, 0x0100600C);
        PutU32(hdr, 12, kDesc);
        PutU32(cab, 0, 0x28635349);
        PutU32(cab, 4, 0x0100600C);
        cab.resize(0x100);

        std::vector<std::string> directories = {"", "Data\\Sprites", "Docs"};
        PutU32(hdr, kDesc + 0x0C, kTable);
        PutU32(hdr, kDesc + 0x1C, static_cast<uint32_t>(directories.size()));
        PutU32(hdr, kDesc + 0x28, static_cast<uint32_t>(entries.size()));
        PutU32(hdr, kDesc + 0x2C, kDescriptors);
        const size_t table = kDesc + kTable;
        uint32_t strings = 0x40;
        auto addString = [&](std::string_view s) {
            const uint32_t at = strings;
            PutString(hdr, table + at, s);
            strings += static_cast<uint32_t>(s.size() + 1);
            return at;
        };
        for (size_t i = 0; i < directories.size(); ++i) PutU32(hdr, table + 4 * i, addString(directories[i]));

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            const size_t fd = table + kDescriptors + i * 0x57;
            descriptorAt.push_back(fd);
            std::vector<uint8_t> stored;
            if (e.flags & CabinetFile::kCompressed) {
                // Two chunks: a sync-flushed one and a final one.
                const size_t half = e.content.size() / 2;
                stored = Chunk(std::string_view(e.content).substr(0, half), false);
                auto second = Chunk(std::string_view(e.content).substr(half), true);
                stored.insert(stored.end(), second.begin(), second.end());
            } else if (!e.external && !(e.flags & CabinetFile::kInvalid)) {
                stored.assign(e.content.begin(), e.content.end());
            }
            uint64_t offset = 0;
            if (!stored.empty()) {
                offset = cab.size();
                cab.insert(cab.end(), stored.begin(), stored.end());
            }
            storedAt.push_back(static_cast<size_t>(offset));
            hdr.resize(std::max(hdr.size(), fd + 0x57));
            PutU16(hdr, fd + 0x00, e.flags);
            PutU64(hdr, fd + 0x02, e.external ? 1234 : e.content.size());
            PutU64(hdr, fd + 0x0A, stored.size());
            PutU64(hdr, fd + 0x12, offset);
            const Md5Digest md5 = Md5Of(Bytes(e.content));
            std::copy(md5.begin(), md5.end(), hdr.begin() + static_cast<ptrdiff_t>(fd + 0x1A));
            PutU32(hdr, fd + 0x3A, addString(e.name));
            size_t directory = 0;
            while (directories[directory] != e.directory) ++directory;
            PutU16(hdr, fd + 0x3E, static_cast<uint16_t>(directory));
            PutU16(hdr, fd + 0x55, 1);
        }

        // One group per run of consecutive entries; slots 0 and 5 hold linked lists of them.
        uint32_t node = kGroups;
        std::vector<uint32_t> nodes;
        for (size_t first = 0; first < entries.size();) {
            size_t last = first;
            while (last + 1 < entries.size() && entries[last + 1].group == entries[first].group) ++last;
            const uint32_t descriptor = node + 0x10;
            PutU32(hdr, kDesc + node + 4, descriptor);
            PutU32(hdr, kDesc + descriptor, descriptor + 0x40);
            PutString(hdr, kDesc + descriptor + 0x40, entries[first].group);
            PutU32(hdr, kDesc + descriptor + 0x16, static_cast<uint32_t>(first));
            PutU32(hdr, kDesc + descriptor + 0x1A, static_cast<uint32_t>(last));
            nodes.push_back(node);
            node += 0x80;
            first = last + 1;
        }
        PutU32(hdr, kDesc + 0x3E + 4 * 0, nodes[0]);
        for (size_t i = 0; i + 1 < nodes.size(); ++i) PutU32(hdr, kDesc + nodes[i] + 8, i == 2 ? 0 : nodes[i + 1]);
        PutU32(hdr, kDesc + 0x3E + 4 * 5, nodes[3]);
    }

    std::filesystem::path Write(const std::string& name) const
    {
        const std::filesystem::path root = dir.path / name;
        std::filesystem::remove_all(root);
        WriteFile(root / "data1.hdr", hdr);
        WriteFile(root / "DATA1.CAB", cab); // volumes are found ignoring case, like on a mounted CD
        return root;
    }
};

std::optional<Cabinet> OpenCabinet(const SyntheticCabinet& fixture, const std::string& name, std::string& error)
{
    return Cabinet::Open(fixture.Write(name) / "data1.hdr", error);
}

std::string Extracted(const Cabinet& cabinet, const CabinetFile& file, std::string& error)
{
    std::vector<uint8_t> out;
    if (!cabinet.ExtractToMemory(file, out, error)) return "<failed>";
    return std::string(out.begin(), out.end());
}

} // namespace

OBL_TEST_CASE("convert.md5_rfc1321_vectors")
{
    OBL_CHECK_EQ(HexOf(Md5Of(Bytes(""))), std::string("d41d8cd98f00b204e9800998ecf8427e"));
    OBL_CHECK_EQ(HexOf(Md5Of(Bytes("a"))), std::string("0cc175b9c0f1b6a831c399e269772661"));
    OBL_CHECK_EQ(HexOf(Md5Of(Bytes("abc"))), std::string("900150983cd24fb0d6963f7d28e17f72"));
    OBL_CHECK_EQ(HexOf(Md5Of(Bytes("message digest"))), std::string("f96b697d7cb7938d525a2f31aaf161d0"));
    OBL_CHECK_EQ(HexOf(Md5Of(Bytes("abcdefghijklmnopqrstuvwxyz"))), std::string("c3fcd3d76192e4007dfb496cca67e13b"));
    const std::string digits = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    OBL_CHECK_EQ(HexOf(Md5Of(Bytes(digits))), std::string("57edf4a22be3c955ac49da2e2107b67a"));
    Md5 split; // the same digest when fed in uneven pieces
    for (size_t at = 0; at < digits.size(); at += 7) split.Update(Bytes(std::string_view(digits).substr(at, 7)));
    OBL_CHECK_EQ(HexOf(split.Digest()), std::string("57edf4a22be3c955ac49da2e2107b67a"));
}

OBL_TEST_CASE("convert.cabinet_lists_and_extracts")
{
    SyntheticCabinet fixture;
    std::string error;
    auto cabinet = OpenCabinet(fixture, "ok", error);
    if (!cabinet) std::printf("  error: %s\n", error.c_str());
    OBL_REQUIRE(cabinet.has_value());
    OBL_REQUIRE(cabinet->Files().size() == size_t{6}); // the invalid placeholder is dropped
    const CabinetFile& sprite = cabinet->Files()[0];
    OBL_CHECK_EQ(sprite.path, std::string("Data/Sprites/BLSprite.hdr"));
    OBL_CHECK_EQ(sprite.group, std::string("EURO Comp"));
    OBL_CHECK_EQ(cabinet->Files()[5].path,
            std::string("Docs/L\xC3\xA9"
                        "eme.htm")); // Windows-1252 name as UTF-8
    OBL_CHECK(cabinet->Files()[2].External());
    OBL_CHECK(!sprite.External());

    const CabinetFile* found = cabinet->Find("data/SPRITES/blsprite.hdr", error);
    OBL_REQUIRE(found == &sprite);
    OBL_CHECK_EQ(Extracted(*cabinet, sprite, error), kSpriteContent);
    const CabinetFile* readme = cabinet->Find("readme.txt", error);
    OBL_REQUIRE(readme != nullptr);
    OBL_CHECK_EQ(Extracted(*cabinet, *readme, error), std::string("plain stored text"));

    Source source;
    OBL_CHECK(!IdentifySource(*obl::convert::OpenDirectorySource(fixture.Write("root")), source, error));
    OBL_CHECK(error.find("BeachLife.exe not found") != std::string::npos);
}

OBL_TEST_CASE("convert.cabinet_refuses_malformed_headers")
{
    const auto openError = [](const SyntheticCabinet& fixture) {
        MemorySource disc;
        disc.Add("data1.hdr", fixture.hdr);
        disc.Add("data1.cab", fixture.cab);
        std::string error;
        return Cabinet::Open(disc, "data1.hdr", error) ? std::string("<opened>") : error;
    };
    const auto refusedWith = [&](const SyntheticCabinet& fixture, std::string_view why) {
        const std::string error = openError(fixture);
        if (error.find(why) == std::string::npos) std::printf("  expected %s, got %s\n", std::string(why).c_str(), error.c_str());
        return error.find(why) != std::string::npos;
    };
    const size_t table = kDesc + kTable;
    {
        SyntheticCabinet fixture;
        fixture.hdr[0] = 'X';
        OBL_CHECK(refusedWith(fixture, "not an InstallShield cabinet header"));
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, 12, 4); // the descriptor would overlap the common header
        OBL_CHECK(refusedWith(fixture, "truncated cab descriptor"));
        PutU32(fixture.hdr, 12, 0xFFFFFF00u);
        OBL_CHECK(refusedWith(fixture, "truncated cab descriptor"));
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, table, 0xFFFFFF00u);
        OBL_CHECK(refusedWith(fixture, "bad directory name 0"));
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, kDesc + 0x1C, 0); // no directory names to read first
        PutU32(fixture.hdr, kDesc + 0x28, 0);
        PutU32(fixture.hdr, kDesc + 0x3E, 0);
        fixture.hdr.resize(kDesc + 0x3E + 8);
        OBL_CHECK(refusedWith(fixture, "truncated file group table"));
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, kDesc + 0x3E, 0xFFFFFF00u);
        OBL_CHECK(refusedWith(fixture, "bad file group list"));
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, kDesc + kGroups + 8, kGroups); // a list that loops on itself
        OBL_CHECK(refusedWith(fixture, "in two groups"));
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, kDesc + kGroups + 0x10 + 0x1A, 100); // past the file count
        OBL_CHECK(refusedWith(fixture, "bad file group descriptor"));
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, kDesc + kGroups + 0x80 + 0x10 + 0x16, 0); // the second group overlaps the first
        OBL_CHECK(refusedWith(fixture, "is in two groups"));
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, kDesc + 0x2C, 0xFFFFFF00u);
        OBL_CHECK(refusedWith(fixture, "truncated file table"));
        PutU32(fixture.hdr, kDesc + 0x2C, kDescriptors);
        PutU32(fixture.hdr, kDesc + 0x28, 0xFFFFFFF0u); // a count no header can hold
        OBL_CHECK(refusedWith(fixture, "truncated file table"));
    }
    {
        SyntheticCabinet fixture;
        PutU16(fixture.hdr, fixture.descriptorAt[0] + 0x3E, 99);
        OBL_CHECK(refusedWith(fixture, "bad name of file 0"));
    }
    // No cut or single damaged field may read outside the header (the sanitizer job runs this too).
    const SyntheticCabinet whole;
    for (size_t size = 0; size < whole.hdr.size(); size += 7) {
        SyntheticCabinet fixture;
        fixture.hdr.resize(size);
        OBL_CHECK(!openError(fixture).empty());
    }
    for (size_t at = 0; at + 4 <= whole.hdr.size(); at += 4) {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, at, 0xFFFFFFF0u);
        OBL_CHECK(!openError(fixture).empty());
    }
}

OBL_TEST_CASE("convert.cabinet_refuses_damaged_volumes")
{
    const auto extractError = [](const SyntheticCabinet& fixture, size_t file) {
        MemorySource disc;
        disc.Add("data1.hdr", fixture.hdr);
        disc.Add("data1.cab", fixture.cab);
        std::string error;
        auto cabinet = Cabinet::Open(disc, "data1.hdr", error);
        OBL_REQUIRE(cabinet.has_value());
        std::vector<uint8_t> out;
        return cabinet->ExtractToMemory(cabinet->Files()[file], out, error) ? std::string("<extracted>") : error;
    };
    const auto contains = [](const std::string& text, std::string_view part) {
        if (text.find(part) == std::string::npos) std::printf("  expected %s, got %s\n", std::string(part).c_str(), text.c_str());
        return text.find(part) != std::string::npos;
    };
    constexpr size_t kSprite = 0, kPlain = 1;
    {
        SyntheticCabinet fixture;
        fixture.cab[4] ^= 1;
        OBL_CHECK(contains(extractError(fixture, kSprite), "not a matching cabinet volume"));
    }
    {
        SyntheticCabinet fixture;
        fixture.cab[fixture.storedAt[kSprite]] = 0xFF; // the first chunk claims more than the stored size
        fixture.cab[fixture.storedAt[kSprite] + 1] = 0xFF;
        OBL_CHECK(contains(extractError(fixture, kSprite), "chunk runs past the stored size"));
    }
    {
        SyntheticCabinet fixture;
        PutU64(fixture.hdr, fixture.descriptorAt[kSprite] + 0x0A, 1); // one byte cannot hold a chunk length
        OBL_CHECK(contains(extractError(fixture, kSprite), "truncated chunk header"));
    }
    {
        SyntheticCabinet fixture;
        fixture.cab[fixture.storedAt[kSprite] + 2] = 0x07; // a reserved deflate block type
        OBL_CHECK(contains(extractError(fixture, kSprite), "corrupt compressed data"));
    }
    {
        SyntheticCabinet fixture;
        PutU64(fixture.hdr, fixture.descriptorAt[kSprite] + 0x02, 4); // listed smaller than the stream
        OBL_CHECK(contains(extractError(fixture, kSprite), "larger than listed"));
    }
    {
        SyntheticCabinet fixture;
        PutU64(fixture.hdr, fixture.descriptorAt[kSprite] + 0x02, kSpriteContent.size() + 1); // listed larger
        OBL_CHECK(contains(extractError(fixture, kSprite), "size differs from the listed"));
    }
    {
        SyntheticCabinet fixture;
        fixture.cab[fixture.storedAt[kPlain]] ^= 0x20;
        OBL_CHECK(contains(extractError(fixture, kPlain), "MD5 mismatch"));
    }
    {
        SyntheticCabinet fixture;
        std::string error;
        MemorySource disc;
        disc.Add("data1.hdr", fixture.hdr);
        disc.Add("data1.cab", fixture.cab);
        auto cabinet = Cabinet::Open(disc, "data1.hdr", error);
        OBL_REQUIRE(cabinet.has_value());
        std::vector<uint8_t> out;
        OBL_CHECK(!cabinet->ExtractToMemory(cabinet->Files()[kSprite], out, error, 4));
        OBL_CHECK(contains(error, "too large to extract into memory"));
    }
}

OBL_TEST_CASE("convert.cabinet_reads_from_a_memory_source")
{
    // The browser hands the importer the picked files in memory; the volume next to a header in a subdirectory is found
    // ignoring case there as well.
    SyntheticCabinet fixture;
    MemorySource disc;
    disc.Add("disc/data1.hdr", fixture.hdr);
    disc.Add("disc/DATA1.CAB", fixture.cab);
    std::string error;
    auto cabinet = Cabinet::Open(disc, "disc/data1.hdr", error);
    if (!cabinet) std::printf("  error: %s\n", error.c_str());
    OBL_REQUIRE(cabinet.has_value());
    const CabinetFile* sprite = cabinet->Find("Data/Sprites/BLSprite.hdr", error);
    OBL_REQUIRE(sprite != nullptr);
    OBL_CHECK_EQ(Extracted(*cabinet, *sprite, error), kSpriteContent);
    const CabinetFile* readme = cabinet->Find("readme.txt", error);
    OBL_REQUIRE(readme != nullptr);
    OBL_CHECK_EQ(Extracted(*cabinet, *readme, error), std::string("plain stored text"));
}

OBL_TEST_CASE("convert.cabinet_refuses_what_it_cannot_read")
{
    std::string error;
    {
        SyntheticCabinet fixture;
        auto cabinet = OpenCabinet(fixture, "ambiguous", error);
        OBL_REQUIRE(cabinet.has_value());
        OBL_CHECK(cabinet->Find("beachlife.ini", error) == nullptr);
        OBL_CHECK(error.find("Ess_A") != std::string::npos && error.find("Ess_B") != std::string::npos);
        OBL_CHECK(cabinet->Find("missing.txt", error) == nullptr);
        OBL_CHECK(error.find("not in the cabinet") != std::string::npos);
        OBL_CHECK_EQ(Extracted(*cabinet, cabinet->Files()[2], error), std::string("<failed>")); // external
        OBL_CHECK(error.find("not stored in the cabinet") != std::string::npos);
    }
    {
        SyntheticCabinet fixture;
        PutU32(fixture.hdr, 4, 0x0100500C);
        OBL_CHECK(!OpenCabinet(fixture, "version", error).has_value());
        OBL_CHECK(error.find("unsupported InstallShield version") != std::string::npos);
    }
    {
        SyntheticCabinet fixture;
        fixture.hdr[0] = 'X';
        OBL_CHECK(!OpenCabinet(fixture, "signature", error).has_value());
    }
    {
        SyntheticCabinet fixture;
        PutU16(fixture.hdr, fixture.descriptorAt[0], CabinetFile::kCompressed | CabinetFile::kSplit);
        auto cabinet = OpenCabinet(fixture, "split", error);
        OBL_REQUIRE(cabinet.has_value());
        OBL_CHECK_EQ(Extracted(*cabinet, cabinet->Files()[0], error), std::string("<failed>"));
        OBL_CHECK(error.find("split") != std::string::npos);
    }
    {
        SyntheticCabinet fixture;
        PutU64(fixture.hdr, fixture.descriptorAt[1] + 0x12, fixture.cab.size() - 3); // runs past the volume's end
        auto cabinet = OpenCabinet(fixture, "range", error);
        OBL_REQUIRE(cabinet.has_value());
        OBL_CHECK_EQ(Extracted(*cabinet, cabinet->Files()[1], error), std::string("<failed>"));
        OBL_CHECK(error.find("outside data1.cab") != std::string::npos);
    }
    {
        SyntheticCabinet fixture;
        fixture.cab[fixture.storedAt[0] + 10] ^= 0x20; // one damaged byte inside the stored data
        auto cabinet = OpenCabinet(fixture, "damaged", error);
        OBL_REQUIRE(cabinet.has_value());
        OBL_CHECK_EQ(Extracted(*cabinet, cabinet->Files()[0], error), std::string("<failed>"));
        OBL_CHECK(error.find("MD5 mismatch") != std::string::npos);
    }
    {
        SyntheticCabinet fixture;
        auto root = fixture.Write("novolume");
        std::filesystem::remove(root / "DATA1.CAB");
        auto cabinet = Cabinet::Open(root / "data1.hdr", error);
        OBL_REQUIRE(cabinet.has_value());
        OBL_CHECK_EQ(Extracted(*cabinet, cabinet->Files()[0], error), std::string("<failed>"));
        OBL_CHECK(error.find("data1.cab not found") != std::string::npos);
    }
}

namespace {

// A spool in memory that records which of its files exist.
class CountingSpool final : public Spool {
public:
    bool Create(std::string_view name) override
    {
        names.emplace(name);
        return inner->Create(name);
    }
    bool Append(std::string_view name, std::span<const uint8_t> bytes) override { return inner->Append(name, bytes); }
    bool Read(std::string_view name, uint64_t offset, std::span<uint8_t> out) override { return inner->Read(name, offset, out); }
    void Remove(std::string_view name) override
    {
        names.erase(std::string(name));
        inner->Remove(name);
    }

    std::unique_ptr<Spool> inner = OpenMemorySpool();
    std::set<std::string> names;
};

std::vector<std::string> Names(const ByteSource& source, std::string_view directory)
{
    std::vector<std::string> names;
    for (const auto& entry : source.List(directory).value_or(std::vector<SourceEntry>()))
        names.push_back(entry.name + (entry.directory ? "/" : ""));
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

OBL_TEST_CASE("convert.cd_source_presents_the_installed_tree")
{
    SyntheticCabinet fixture;
    MemorySource disc;
    disc.Add("data1.hdr", fixture.hdr);
    disc.Add("DATA1.CAB", fixture.cab);
    disc.Add("BeachLife.exe", {'M', 'Z'});
    OBL_CHECK(IsCdRoot(disc));
    for (const uint64_t memory : {uint64_t{64} << 20, uint64_t{4}}) { // in memory, then spooled
        CountingSpool spool;
        std::string error;
        {
            const auto cd = OpenCdSource(disc, spool, error, {memory});
            if (!cd) std::printf("  error: %s\n", error.c_str());
            OBL_REQUIRE(cd != nullptr);
            OBL_CHECK(!IsCdRoot(*cd));
            const std::vector<std::string> root = {"BeachLife.exe", "Data/"};
            OBL_CHECK(Names(*cd, "") == root);
            OBL_CHECK(Names(*cd, "Data/Sprites") == std::vector<std::string>{"BLSprite.hdr"});
            OBL_CHECK(!FindFile(*cd, "readme.txt") && !FindFile(*cd, "BeachLife.ini"));
            const auto header = FindFile(*cd, "data/SPRITES/blsprite.HDR");
            OBL_REQUIRE(header.has_value());
            OBL_CHECK_EQ(cd->Size(*header).value_or(0), kSpriteContent.size());
            const auto bytes = cd->ReadAll(*header);
            OBL_REQUIRE(bytes.has_value());
            OBL_CHECK_EQ(std::string(bytes->begin(), bytes->end()), kSpriteContent);
            std::vector<uint8_t> range(5);
            OBL_CHECK(cd->Read(*header, 20, range));
            OBL_CHECK_EQ(std::string(range.begin(), range.end()), kSpriteContent.substr(20, 5));
            OBL_CHECK(!cd->Read(*header, kSpriteContent.size() - 2, range)); // past the end
            OBL_CHECK_EQ(cd->ReadAll("BeachLife.exe").value_or(std::vector<uint8_t>()).size(), size_t{2});
            OBL_CHECK_EQ(spool.names.size(), memory == 4 ? size_t{1} : size_t{0});
            OBL_CHECK(cd->Describe(*header).find("data1.hdr: Data/Sprites/BLSprite.hdr") != std::string::npos);
        }
        OBL_CHECK(spool.names.empty()); // removed with the source
    }
    {
        // A damaged stored file fails its MD5 check, in memory and spooled, and leaves no spool file.
        auto damaged = fixture.cab;
        damaged[fixture.storedAt[0] + 10] ^= 0x20;
        MemorySource bad;
        bad.Add("data1.hdr", fixture.hdr);
        bad.Add("data1.cab", damaged);
        for (const uint64_t memory : {uint64_t{64} << 20, uint64_t{4}}) {
            CountingSpool spool;
            std::string error;
            const auto cd = OpenCdSource(bad, spool, error, {memory});
            OBL_REQUIRE(cd != nullptr);
            OBL_CHECK(!cd->ReadAll("Data/Sprites/BLSprite.hdr").has_value());
            OBL_CHECK(spool.names.empty());
        }
    }
    {
        // No group holds the sprite header: not a Beach Life CD.
        SyntheticCabinet other;
        other.hdr[kDesc + kTable + 0x41 + 5] = 'X'; // the directory string "Data\Sprites" (after "") becomes "Data\Xprites"
        MemorySource notGame;
        notGame.Add("data1.hdr", other.hdr);
        notGame.Add("data1.cab", other.cab);
        CountingSpool spool;
        std::string error;
        OBL_CHECK(OpenCdSource(notGame, spool, error) == nullptr);
        OBL_CHECK(error.find("not in the cabinet") != std::string::npos);
    }
}

OBL_TEST_CASE("convert.cd_image_identifies_through_its_cabinets")
{
    // A whole synthetic CD in a disc image: the image source, the CD's installed tree, then the source identity with a
    // test table that knows the synthetic executable. Both the Joliet and the primary (upper-case) names are read.
    SyntheticCabinet fixture;
    obl::test::SyntheticExe exe;
    exe.AddSection(".text", 0x1000, 0x200, obl::test::SyntheticExe::kCodeFlags);
    const std::string hash = obl::sha256::Hex(exe.bytes);
    const KnownExe known[] = {{hash, ExeBuild::CdAug2002}};
    obl::test::SyntheticIso iso;
    iso.Add("data1.hdr", fixture.hdr);
    iso.Add("data1.cab", fixture.cab);
    iso.Add("BeachLife.exe", exe.bytes);
    iso.Add("Setup/setup.ini", std::vector<uint8_t>(5000, 'x')); // a file over two sectors in a subdirectory
    for (const bool joliet : {true, false}) {
        const std::vector<uint8_t> image = iso.Build(joliet);
        std::string error;
        const auto disc = web::OpenIsoSource(
                image.size(),
                [&](uint64_t offset, std::span<uint8_t> out) {
                    if (offset > image.size() || out.size() > image.size() - offset) return false;
                    std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
                    return true;
                },
                error);
        OBL_REQUIRE(disc != nullptr);
        OBL_CHECK(Names(*disc, "") ==
                (joliet ? std::vector<std::string>{"BeachLife.exe", "Setup/", "data1.cab", "data1.hdr"}
                        : std::vector<std::string>{"BEACHLIFE.EXE", "DATA1.CAB", "DATA1.HDR", "SETUP/"}));
        OBL_CHECK_EQ(disc->ReadAll(joliet ? "Setup/setup.ini" : "SETUP/SETUP.INI").value_or(std::vector<uint8_t>()).size(), size_t{5000});
        OBL_CHECK(IsCdRoot(*disc));
        const auto spool = OpenMemorySpool();
        const auto cd = OpenCdSource(*disc, *spool, error);
        OBL_REQUIRE(cd != nullptr);
        Source source;
        OBL_CHECK(!IdentifySource(*cd, source, error)); // the supported table does not know the synthetic executable
        OBL_REQUIRE(IdentifySource(*cd, source, error, known));
        OBL_CHECK(source.exe == ExeBuild::CdAug2002);
        OBL_CHECK(source.data == DataRevision::EuAug2002);
        OBL_CHECK_EQ(source.sha256[std::string(kSpriteHeaderPath)], obl::sha256::Hex(Bytes(kSpriteContent)));
        // The CD itself (no installed tree) is identified through its cabinet header as well.
        OBL_REQUIRE(IdentifySource(*disc, source, error, known));
        OBL_CHECK(source.data == DataRevision::EuAug2002);
    }
}

OBL_TEST_CASE("data.convert_cd_sources")
{
    // The CD copies identify straight from their cabinets (the sprite header is extracted and MD5-checked); the
    // European CD's sprite header is the installed one byte for byte (sources.md).
    auto ogData = obl::test::FindOgData();
    if (!ogData) OBL_SKIP("the installation was not found (set OBL_OGDATA)");
    Source installed;
    std::string error;
    const bool haveInstall = IdentifySource(*obl::convert::OpenDirectorySource(*ogData / "program-files" / "Beach Life"), installed, error);
    int checked = 0;
    for (const char* cd : obl::test::kCdCopies) {
        const std::filesystem::path root = *ogData / cd;
        if (!std::filesystem::exists(root / "data1.hdr")) continue;
        ++checked;
        Source source;
        if (!IdentifySource(*obl::convert::OpenDirectorySource(root), source, error)) std::printf("  %s: %s\n", cd, error.c_str());
        OBL_CHECK(source.exe == ExeBuild::CdAug2002);
        const bool european = std::string(cd) == "iso";
        OBL_CHECK(source.data == (european ? DataRevision::EuAug2002 : DataRevision::UkAug2002));
        if (european && haveInstall)
            OBL_CHECK_EQ(source.sha256[std::string(kSpriteHeaderPath)], installed.sha256[std::string(kSpriteHeaderPath)]);
    }
    if (checked == 0) OBL_SKIP("no CD copy with data1.hdr found");
}
