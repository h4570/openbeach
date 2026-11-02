// The browser adapter's portable part (importer/web/): the disc image and picked-files sources, the storage sink and the
// whole-import pipeline's failure paths, on synthetic bytes only. The wasm job runs these under Node as well.

#include "unit_test.h"

#include "formats/schemas.h"
#include "importer/web/storage_sink.h"
#include "importer/web/web_source.h"
#include "openbeach/importer/import.h"

#include "core/json_schema.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

constexpr size_t kSector = 2048;

std::vector<uint8_t> Bytes(std::string_view s)
{
    return {s.begin(), s.end()};
}

std::string Text(const std::vector<uint8_t>& bytes)
{
    return {bytes.begin(), bytes.end()};
}

void PutBothEndian32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<uint8_t>(v >> (8 * i));
        p[7 - i] = static_cast<uint8_t>(v >> (8 * i));
    }
}

// One directory record (ECMA-119 9.1); an even-length identifier is followed by a padding byte.
std::vector<uint8_t> Record(uint32_t sector, uint32_t size, uint8_t flags, const std::vector<uint8_t>& id)
{
    std::vector<uint8_t> r(33 + id.size() + (id.size() % 2 == 0 ? 1 : 0));
    r[0] = static_cast<uint8_t>(r.size());
    PutBothEndian32(&r[2], sector);
    PutBothEndian32(&r[10], size);
    r[25] = flags;
    r[28] = 1;
    r[31] = 1;
    r[32] = static_cast<uint8_t>(id.size());
    std::copy(id.begin(), id.end(), r.begin() + 33);
    return r;
}

std::vector<uint8_t> Ucs2(std::u16string_view name)
{
    std::vector<uint8_t> out;
    for (char16_t c : name) {
        out.push_back(static_cast<uint8_t>(c >> 8));
        out.push_back(static_cast<uint8_t>(c));
    }
    return out;
}

struct Image {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(40 * kSector);

    uint8_t* At(size_t sector) { return bytes.data() + sector * kSector; }

    void Descriptor(size_t sector, uint8_t type, uint32_t rootSector, bool joliet = false)
    {
        uint8_t* d = At(sector);
        d[0] = type;
        std::memcpy(d + 1, "CD001", 5);
        d[6] = 1;
        if (joliet) std::memcpy(d + 88, "%/E", 3);
        const auto root = Record(rootSector, kSector, 0x02, {0});
        std::copy(root.begin(), root.end(), d + 156);
    }

    void Directory(size_t sector, uint32_t parent, const std::vector<std::vector<uint8_t>>& records)
    {
        size_t at = sector * kSector;
        for (const auto& r : {Record(static_cast<uint32_t>(sector), kSector, 0x02, {0}), Record(parent, kSector, 0x02, {1})}) {
            std::copy(r.begin(), r.end(), bytes.begin() + at);
            at += r.size();
        }
        for (const auto& r : records) {
            std::copy(r.begin(), r.end(), bytes.begin() + at);
            at += r.size();
        }
    }

    void File(size_t sector, std::string_view content) { std::copy(content.begin(), content.end(), At(sector)); }

    web::ReadAt Reader() const
    {
        return [this](uint64_t offset, std::span<uint8_t> out) {
            if (offset > bytes.size() || out.size() > bytes.size() - offset) return false;
            std::copy_n(bytes.begin() + static_cast<ptrdiff_t>(offset), out.size(), out.begin());
            return true;
        };
    }
};

Image MakeImage(bool joliet)
{
    Image image;
    image.Descriptor(16, 1, 20);
    image.Descriptor(17, joliet ? 2 : 3, 25, true);
    image.Descriptor(18, 255, 0);
    image.Directory(20, 20, {Record(21, kSector, 0x02, Bytes("DATA")), Record(22, 5, 0, Bytes("SETUP.EXE;1"))});
    image.Directory(21, 20, {Record(23, 3, 0x80, Bytes("BIG.BIN;1")), Record(24, 4, 0, Bytes("BIG.BIN;1"))});
    image.File(22, "hello");
    image.File(23, "abc");
    image.File(24, "defg");
    image.Directory(25, 25, {Record(26, kSector, 0x02, Ucs2(u"Data")), Record(22, 5, 0, Ucs2(u"Setup.exe;1"))});
    image.Directory(
            26, 25, {Record(23, 3, 0x80, Ucs2(u"Big.bin")), Record(24, 4, 0, Ucs2(u"Big.bin")), Record(22, 5, 0, Ucs2(u"Café.txt"))});
    return image;
}

std::vector<std::string> Names(const std::optional<std::vector<SourceEntry>>& entries)
{
    std::vector<std::string> names;
    if (entries)
        for (const auto& e : *entries) names.push_back(e.name + (e.directory ? "/" : ""));
    return names;
}

class MapStorage final : public web::PackStorage {
public:
    bool Put(std::string_view key, std::span<const uint8_t> bytes) override
    {
        if (refuse) return false;
        values.insert_or_assign(std::string(key), std::vector<uint8_t>(bytes.begin(), bytes.end()));
        return true;
    }
    std::optional<std::vector<uint8_t>> Get(std::string_view key) const override
    {
        const auto it = values.find(key);
        if (it == values.end()) return std::nullopt;
        return it->second;
    }
    bool Delete(std::string_view key) override
    {
        if (const auto it = values.find(key); it != values.end()) values.erase(it);
        return true;
    }
    std::vector<std::string> Keys(std::string_view prefix) const override
    {
        std::vector<std::string> keys;
        for (const auto& [key, value] : values)
            if (key.starts_with(prefix)) keys.push_back(key);
        return keys;
    }

    std::map<std::string, std::vector<uint8_t>, std::less<>> values;
    bool refuse = false;
};

} // namespace

OBL_TEST_CASE("convert.web_iso_primary_names")
{
    const Image image = MakeImage(false);
    std::string error;
    const auto iso = web::OpenIsoSource(image.bytes.size(), image.Reader(), error);
    OBL_REQUIRE(iso != nullptr);
    OBL_CHECK(Names(iso->List("")) == (std::vector<std::string>{"DATA/", "SETUP.EXE"}));
    OBL_CHECK(Names(iso->List("DATA")) == (std::vector<std::string>{"BIG.BIN"}));
    OBL_CHECK(!iso->List("SETUP.EXE").has_value());
    OBL_CHECK(!iso->List("MISSING").has_value());
    OBL_CHECK_EQ(Text(iso->ReadAll("SETUP.EXE").value_or(std::vector<uint8_t>())), std::string("hello"));
    // A multi-extent file reads as one, also across the extent boundary.
    OBL_CHECK_EQ(iso->Size("DATA/BIG.BIN").value_or(0), uint64_t{7});
    OBL_CHECK_EQ(Text(iso->ReadAll("DATA/BIG.BIN").value_or(std::vector<uint8_t>())), std::string("abcdefg"));
    std::vector<uint8_t> middle(3);
    OBL_CHECK(iso->Read("DATA/BIG.BIN", 2, middle));
    OBL_CHECK_EQ(Text(middle), std::string("cde"));
    OBL_CHECK(!iso->Read("DATA/BIG.BIN", 5, middle)); // past the end
    OBL_CHECK(!iso->Size("DATA").has_value());
    // The converters' case-insensitive lookup works on the image.
    OBL_CHECK_EQ(FindFile(*iso, "data/big.bin").value_or(""), std::string("DATA/BIG.BIN"));
}

OBL_TEST_CASE("convert.web_iso_joliet_names")
{
    const Image image = MakeImage(true);
    std::string error;
    const auto iso = web::OpenIsoSource(image.bytes.size(), image.Reader(), error);
    OBL_REQUIRE(iso != nullptr);
    OBL_CHECK(Names(iso->List("")) == (std::vector<std::string>{"Data/", "Setup.exe"}));
    OBL_CHECK(Names(iso->List("Data")) == (std::vector<std::string>{"Big.bin", "Caf\xc3\xa9.txt"}));
    OBL_CHECK_EQ(Text(iso->ReadAll("Data/Big.bin").value_or(std::vector<uint8_t>())), std::string("abcdefg"));
    OBL_CHECK_EQ(Text(iso->ReadAll("Data/Caf\xc3\xa9.txt").value_or(std::vector<uint8_t>())), std::string("hello"));
}

OBL_TEST_CASE("convert.web_iso_malformed")
{
    std::string error;
    Image blank;
    OBL_CHECK(web::OpenIsoSource(blank.bytes.size(), blank.Reader(), error) == nullptr);
    OBL_CHECK(error.find("ISO 9660") != std::string::npos);
    // Too small to hold the descriptors.
    Image image = MakeImage(false);
    OBL_CHECK(web::OpenIsoSource(16 * kSector, image.Reader(), error) == nullptr);
    // A file extent past the image's end makes its directory unreadable instead of reading out of bounds.
    const auto bad = Record(1000, 5, 0, Bytes("FAR.BIN;1"));
    std::copy(bad.begin(), bad.end(),
            image.At(20) + 34 * 2 + Record(21, kSector, 0x02, Bytes("DATA")).size() + Record(22, 5, 0, Bytes("SETUP.EXE;1")).size());
    const auto iso = web::OpenIsoSource(image.bytes.size(), image.Reader(), error);
    OBL_REQUIRE(iso != nullptr);
    OBL_CHECK(!iso->List("").has_value());
    OBL_CHECK(!iso->ReadAll("SETUP.EXE").has_value());
    // A record shorter than its fixed part.
    Image truncated = MakeImage(false);
    truncated.At(21)[34 * 2] = 20;
    const auto iso2 = web::OpenIsoSource(truncated.bytes.size(), truncated.Reader(), error);
    OBL_REQUIRE(iso2 != nullptr);
    OBL_CHECK(Names(iso2->List("")).size() == 2u);
    OBL_CHECK(!iso2->List("DATA").has_value());
}

OBL_TEST_CASE("convert.web_picked_files_root")
{
    const std::vector<std::string> contents = {"exe", "hdr", "readme", "other"};
    const std::vector<web::PickedFile> files = {
            {"Games/Beach Life/BeachLife.exe", 3},
            {"Games/Beach Life/Data/Sprites/BLSprite.hdr", 3},
            {"Games/readme.txt", 6},
            {"Games/Other/beachlife.EXE.bak", 5},
    };
    std::vector<size_t> reads;
    std::string error;
    const auto source = web::OpenPickedFilesSource(
            files,
            [&](size_t index, uint64_t offset, std::span<uint8_t> out) {
                reads.push_back(index);
                const std::string& c = contents[index];
                std::copy_n(c.begin() + static_cast<ptrdiff_t>(offset), out.size(), out.begin());
                return true;
            },
            error);
    OBL_REQUIRE(source != nullptr);
    // The game folder is the root; files outside it are not part of the source.
    OBL_CHECK(Names(source->List("")) == (std::vector<std::string>{"BeachLife.exe", "Data/"}));
    OBL_CHECK(Names(source->List("Data/Sprites")) == (std::vector<std::string>{"BLSprite.hdr"}));
    OBL_CHECK_EQ(FindFile(*source, "data/sprites/blsprite.HDR").value_or(""), std::string("Data/Sprites/BLSprite.hdr"));
    OBL_CHECK_EQ(Text(source->ReadAll("Data/Sprites/BLSprite.hdr").value_or(std::vector<uint8_t>())), std::string("hdr"));
    OBL_CHECK(reads == std::vector<size_t>{1});
    std::vector<uint8_t> tooLong(4);
    OBL_CHECK(!source->Read("BeachLife.exe", 0, tooLong));
    OBL_CHECK(!source->Size("readme.txt").has_value());
}

OBL_TEST_CASE("convert.web_picked_files_cd_root_and_errors")
{
    const auto noRead = [](size_t, uint64_t, std::span<uint8_t>) { return false; };
    std::string error;
    // A CD is found by its cabinet header; backslashes from some browsers count as separators.
    const auto cd = web::OpenPickedFilesSource({{"CD\\DATA1.HDR", 1}, {"CD\\data1.cab", 1}}, noRead, error);
    OBL_REQUIRE(cd != nullptr);
    OBL_CHECK(Names(cd->List("")) == (std::vector<std::string>{"DATA1.HDR", "data1.cab"}));

    OBL_CHECK(web::OpenPickedFilesSource({{"Beach Life/readme.txt", 1}}, noRead, error) == nullptr);
    OBL_CHECK(error.find("no Beach Life installation") != std::string::npos);
    OBL_CHECK(web::OpenPickedFilesSource({{"Beach Life/BeachLife.exe", 1}, {"Beach Life/../x", 1}}, noRead, error) == nullptr);
    OBL_CHECK(error.find("unsafe") != std::string::npos);
    OBL_CHECK(web::OpenPickedFilesSource({{"A/BeachLife.exe", 1}, {"A/BeachLife.exe", 1}}, noRead, error) == nullptr);
    OBL_CHECK(web::OpenPickedFilesSource({{"A/BeachLife.exe", 1}, {"A/Data", 1}, {"A/Data/x", 1}}, noRead, error) == nullptr);
    OBL_CHECK(error.find("Data") != std::string::npos);
}

OBL_TEST_CASE("convert.web_storage_sink")
{
    MapStorage storage;
    storage.values["packs/l0/pack.json"] = Bytes("other pack");
    const auto sink = web::OpenStorageSink(storage, "packs/base");
    std::string error;
    OBL_CHECK(sink->WriteText("gui/a.json", "{}", error));
    OBL_CHECK(sink->WriteText("pack.json", "manifest", error));
    OBL_CHECK(storage.values.contains("packs/base/gui/a.json"));
    OBL_CHECK_EQ(Text(sink->Read("gui/a.json").value_or(std::vector<uint8_t>())), std::string("{}"));
    auto files = sink->Files();
    std::sort(files.begin(), files.end());
    OBL_CHECK(files == (std::vector<std::string>{"gui/a.json", "pack.json"}));
    OBL_CHECK(sink->Remove("gui/a.json"));
    OBL_CHECK(sink->Remove("gui/missing.json"));
    OBL_CHECK(!sink->Read("gui/a.json").has_value());
    // Paths never leave the root.
    for (const char* bad : {"", "../l0/pack.json", "gui//a.json", "/abs", "gui\\a.json", "gui/./a.json", "gui/"}) {
        OBL_CHECK(!sink->WriteText(bad, "x", error));
        OBL_CHECK(!sink->Read(bad).has_value());
    }
    OBL_CHECK_EQ(Text(storage.values["packs/l0/pack.json"]), std::string("other pack"));
    storage.refuse = true;
    OBL_CHECK(!sink->WriteText("big.bin", "x", error));
    OBL_CHECK(error.find("packs/base/big.bin") != std::string::npos);
}

OBL_TEST_CASE("convert.web_import_failures")
{
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    MemorySource empty;
    MemorySink pack;
    std::vector<std::string> steps;
    ImportOptions options;
    options.progress = [&](std::string_view step) { steps.emplace_back(step); };
    // No language from the caller and no BeachLife.ini: nothing runs.
    OBL_CHECK(!ImportPack(empty, pack, schemas, options, error));
    OBL_CHECK(error.find("Language") != std::string::npos);
    OBL_CHECK(steps.empty());
    // With a language the first step names what is missing.
    options.language = "en";
    OBL_CHECK(!ImportPack(empty, pack, schemas, options, error));
    OBL_CHECK(steps == std::vector<std::string>{"gui-tables"});
    OBL_CHECK(error.starts_with("gui-tables: BeachLife.exe not found"));
    OBL_CHECK(pack.files().empty());
}
