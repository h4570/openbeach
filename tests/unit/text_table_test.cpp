// Language text table (src/game/tables/text_table.h): quote alternation on synthetic units and the shipped language files.

#include "unit_test.h"

#include "game/tables/text_table.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

using namespace obl::text;

namespace {

constexpr uint32_t kModulus = 6151; // The hash table next prime routine(4500)

Parsed ParseText(const std::u16string& body, uint32_t modulus = kModulus)
{
    std::u16string units = u"﻿" + body; // the files start with a BOM, which the walk never inspects
    return Parse(units.data(), static_cast<int>(units.size()), modulus);
}

} // namespace

OBL_TEST_CASE("text_table.alternates_keys_and_values")
{
    Parsed parsed = ParseText(u"// note\r\n\"ONE\" \"first\"\r\n\"TWO\"\t\"second line\"\r\n");
    OBL_REQUIRE(parsed.entries.size() == 2);
    OBL_CHECK_EQ(parsed.entries[0].key, std::string("ONE"));
    OBL_CHECK(parsed.entries[0].value == u"first");
    OBL_CHECK_EQ(parsed.entries[1].key, std::string("TWO"));
    OBL_CHECK(parsed.entries[1].value == u"second line");
    OBL_CHECK_EQ(parsed.hashed, 2);
}

OBL_TEST_CASE("text_table.quotes_in_comments_take_part")
{
    // The walk does not know about comments: the quoted word in the comment becomes a key and shifts the pairing.
    Parsed parsed = ParseText(u"// say \"hi\"\r\n\"KEY\" \"value\"\r\n");
    OBL_REQUIRE(parsed.entries.size() == 1);
    OBL_CHECK_EQ(parsed.entries[0].key, std::string("hi"));
    OBL_CHECK(parsed.entries[0].value == u"KEY");
}

OBL_TEST_CASE("text_table.trailing_key_and_unterminated_quote")
{
    OBL_CHECK_EQ(ParseText(u"\"A\" \"1\" \"B\"").entries.size(), static_cast<size_t>(1)); // key without value is dropped
    OBL_CHECK_EQ(ParseText(u"\"A\" \"1\" \"unterminated").entries.size(), static_cast<size_t>(1));
    OBL_CHECK_EQ(ParseText(u"").entries.size(), static_cast<size_t>(0));
    // A quote in the very first unit is never seen: without a BOM the pairing starts one quote late and
    // pairs the gap between the strings with the (unterminated) next one.
    std::u16string noBom = u"\"A\" \"1\"";
    Parsed parsed = Parse(noBom.data(), static_cast<int>(noBom.size()), kModulus);
    OBL_REQUIRE(parsed.entries.size() == 1);
    OBL_CHECK_EQ(parsed.entries[0].key, std::string(" "));
    OBL_CHECK(parsed.entries[0].value.empty());
}

OBL_TEST_CASE("text_table.hash_and_probing")
{
    // h = h*5 + c: "AB" = 65*5 + 66.
    OBL_CHECK_EQ(HashKey("AB"), 391u);
    OBL_CHECK_EQ(HashKey(""), 0u);
    OBL_CHECK_EQ(HashKey("\xff"), 0xffffffffu); // signed char: -1

    // Keys that collide land in consecutive slots; the table wraps around and a full table skips the entry.
    Parsed small = ParseText(u"\"A\" \"1\" \"A\" \"2\" \"A\" \"3\" \"A\" \"4\"", 3);
    OBL_CHECK_EQ(small.entries.size(), static_cast<size_t>(4));
    OBL_CHECK_EQ(small.hashed, 3);
    uint32_t home = HashKey("A") % 3;
    OBL_CHECK_EQ(small.slots[home], 0);
    OBL_CHECK_EQ(small.slots[(home + 1) % 3], 1);
    OBL_CHECK_EQ(small.slots[(home + 2) % 3], 2);
}

OBL_TEST_CASE("text_table.append_adds_only_missing_keys")
{
    // Our own texts join the table unless the file already has the key (any case): a language file can override them.
    Parsed parsed = ParseText(u"\"ONE\" \"file\"");
    const Entry extra[] = {{"one", u"ours"}, {"TWO", u"second"}, {"THREE", u"third"}};
    Append(parsed, extra, kModulus, 2);
    OBL_REQUIRE(parsed.entries.size() == 2);
    OBL_CHECK(parsed.entries[0].value == u"file");
    OBL_CHECK_EQ(parsed.entries[1].key, std::string("TWO"));
    OBL_CHECK_EQ(parsed.hashed, 2);
    OBL_CHECK_EQ(parsed.slots[HashKey("TWO") % kModulus], 1);
}

OBL_TEST_CASE("text_table.find_key_ignores_ascii_case")
{
    const std::vector<Entry> entries = {{"STRING_IDENTIFIER", u"x"}, {"Other", u"y"}, {"Other", u"y"}};
    const Table table = MakeTable("t", kModulus, entries);
    OBL_CHECK_EQ(table.FindKey("string_identifier"), 0u);
    OBL_CHECK_EQ(table.FindKey("OTHER"), 1u); // first match
    OBL_CHECK_EQ(table.FindKey(""), 0xffffffffu);
    OBL_CHECK(MakeTable("t", kModulus, std::span(entries).first(1)).FindKey("other") == 0xffffffffu);
}

// Oracle: counts from an independent re-implementation of the walk (Python, same algorithm) over the shipped files.
OBL_TEST_CASE("data.text_table_parses_shipped_languages")
{
    const std::filesystem::path dir = std::filesystem::path(OBL_GAME_DIR) / "Data";
    if (!std::filesystem::exists(dir / "text_polish.ble")) OBL_SKIP("run/ has no imported game data");
    struct Expected {
        const char* file;
        size_t entries;
    };
    for (const Expected& e : {Expected{"text_french.ble", 4339}, Expected{"text_german.ble", 4339}, Expected{"text_italian.ble", 4339},
                 Expected{"text_polish.ble", 4342}, Expected{"text_spanish.ble", 4339}}) {
        std::ifstream in(dir / e.file, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (char& b : bytes) b = static_cast<char>(~b);
        Parsed parsed = Parse(reinterpret_cast<const char16_t*>(bytes.data()), static_cast<int>(bytes.size() / 2), kModulus);
        OBL_CHECK_EQ(parsed.entries.size(), e.entries);
        OBL_CHECK_EQ(static_cast<size_t>(parsed.hashed), e.entries); // no collisions overflow a 6151-slot table
        OBL_REQUIRE(!parsed.entries.empty());
        OBL_CHECK_EQ(parsed.entries.front().key, std::string("STRING_IDENTIFIER"));
        OBL_CHECK_EQ(parsed.entries.back().key, std::string("MAINSS_SANDBOX12_DESC"));
    }
}

// ---- native text::Table (MakeTable, the reads) ---------------------------------------------------------------------------

namespace {

// Writes `text` as a .ble file (UTF-16LE with BOM, every byte inverted) next to a `.uni` name and returns the `.uni` path.
std::string WriteTable(const std::filesystem::path& dir, const std::string& name, const std::u16string& body)
{
    std::filesystem::create_directories(dir);
    std::filesystem::path base = dir / name;
    std::u16string units = u"﻿" + body;
    std::string bytes(reinterpret_cast<const char*>(units.data()), units.size() * 2);
    for (char& b : bytes) b = static_cast<char>(~b);
    std::ofstream out(std::filesystem::path(base).replace_extension(".ble"), std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return std::filesystem::path(base).replace_extension(".uni").string();
}

// The pairs of a written table as the pack loader hands them to Construct: the walk over the file's units (a missing file has none).
std::vector<Entry> FileEntries(const std::string& uniPath)
{
    std::ifstream in(std::filesystem::path(uniPath).replace_extension(".ble"), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), {});
    std::u16string units(bytes.size() / 2, u'\0');
    for (size_t i = 0; i < units.size(); ++i) {
        const auto lo = static_cast<unsigned char>(bytes[2 * i]) ^ 0xFFu;
        const auto hi = static_cast<unsigned char>(bytes[2 * i + 1]) ^ 0xFFu;
        units[i] = static_cast<char16_t>(lo | (hi << 8));
    }
    return Parse(units.data(), static_cast<int>(units.size()), kModulus).entries;
}

} // namespace

OBL_TEST_CASE("text_table.make_table_places_entries_by_hash_slot")
{
    const obl::test::TempPath dir("obl_tt");
    std::string path = WriteTable(dir.path, "basic", u"// c\r\n\"ONE\" \"first\"\r\n\"TWO\" \"second\"\r\n\"A\" \"x\" \"A\" \"y\"");
    const Table table = MakeTable(path, kModulus, FileEntries(path));

    OBL_CHECK_EQ(table.entries.size(), size_t{4});
    OBL_CHECK_EQ(table.hashed, 4);
    OBL_CHECK_EQ(table.modulus, kModulus);
    OBL_CHECK_EQ(table.slots.size(), size_t{kModulus});
    OBL_CHECK_EQ(table.path, path);
    OBL_CHECK_EQ(table.entries[0].key, std::string("ONE"));
    OBL_CHECK(std::u16string(table.Value(1)) == u"second");
    OBL_CHECK_EQ(table.FindKey("two"), 1u);

    // Linear probing from the hash: the home slot holds the first entry, the duplicate key takes the next free one.
    OBL_CHECK_EQ(table.slots[HashKey("ONE") % kModulus], 0);
    const uint32_t homeA = HashKey("A") % kModulus;
    OBL_CHECK_EQ(table.slots[homeA], 2);
    OBL_CHECK_EQ(table.slots[(homeA + 1) % kModulus], 3);
    int occupied = 0;
    for (int32_t slot : table.slots) occupied += slot >= 0;
    OBL_CHECK_EQ(occupied, 4);
}

OBL_TEST_CASE("text_table.make_table_handles_no_entries_and_pair_limit")
{
    const Table empty = MakeTable("text/none.json", kModulus, {});
    OBL_CHECK(empty.entries.empty());
    OBL_CHECK_EQ(empty.hashed, 0);
    OBL_CHECK_EQ(empty.path, std::string("text/none.json"));
    OBL_CHECK_EQ(empty.slots.size(), size_t{kModulus});
    const char16_t kFallback[] = u"";
    OBL_CHECK(empty.Lookup("ANY", kFallback) == kFallback);
    OBL_CHECK(MakeTable("t", 0, {}).Lookup("ANY", kFallback) == kFallback);

    std::vector<Entry> many;
    for (size_t i = 0; i < kMaxPairs + 50; ++i) many.push_back({"K" + std::to_string(i), u"v"});
    const std::vector<Entry> extra = {{"OWN_TEXT", u"own"}};
    OBL_CHECK_EQ(MakeTable("t", kModulus, many, extra).entries.size(), kMaxPairs);
}

OBL_TEST_CASE("text_table.own_texts_append_only_missing_keys")
{
    const std::vector<Entry> entries = {{"CREDITS", u"translated"}};
    const std::vector<Entry> extra = {{"credits", u"english"}, {"OWN", u"own"}};
    const Table table = MakeTable("t", kModulus, entries, extra);
    OBL_CHECK_EQ(table.entries.size(), size_t{2});
    OBL_CHECK(std::u16string(table.Lookup("CREDITS", u"")) == u"translated");
    OBL_CHECK(std::u16string(table.Lookup("own", u"")) == u"own");
}

// The text lookup routine: the hash probe finds keys ignoring case, past collisions and around the end of the slots.
OBL_TEST_CASE("text_table.lookup_probes_past_collisions_and_wraps")
{
    const char16_t kFallback[] = u"";
    const Table small = MakeTable("t", 7, std::vector<Entry>{{"HELLO", u"v0"}, {"WORLD", u"v1"}, {"Zed", u"v2"}});
    OBL_CHECK(small.Lookup("HELLO", kFallback) == small.Value(0));
    OBL_CHECK(small.Lookup("world", kFallback) == small.Value(1));
    OBL_CHECK(small.Lookup("ZED", kFallback) == small.Value(2));
    OBL_CHECK(small.Lookup("C", kFallback) == kFallback);
    OBL_CHECK(small.Lookup(nullptr, kFallback) == kFallback);

    // Every slot of a 3-slot table taken: collisions are certain, the last slot wraps to 0, a miss still ends.
    const Table full = MakeTable("t", 3, std::vector<Entry>{{"K1", u"a"}, {"K2", u"b"}, {"K3", u"c"}});
    OBL_CHECK_EQ(full.hashed, 3);
    for (int n = 0; n < 3; ++n) OBL_CHECK(full.Lookup(full.entries[static_cast<size_t>(n)].key.c_str(), kFallback) == full.Value(n));
    OBL_CHECK(full.Lookup("NOPE", kFallback) == kFallback);
}

// The native table must hold the Python-checked entry counts and a slot per entry for the shipped files.
OBL_TEST_CASE("data.text_table_construct_matches_parser")
{
    const std::filesystem::path dir = std::filesystem::path(OBL_GAME_DIR) / "Data";
    if (!std::filesystem::exists(dir / "text_polish.ble")) OBL_SKIP("run/ has no imported game data");
    const std::string path = (dir / "text_polish.uni").string();
    const Table table = MakeTable(path, kModulus, FileEntries(path));
    OBL_CHECK_EQ(table.entries.size(), size_t{4342});
    OBL_CHECK_EQ(table.hashed, 4342);
    OBL_CHECK_EQ(table.entries[0].key, std::string("STRING_IDENTIFIER"));
}
