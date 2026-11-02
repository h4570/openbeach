#include "game/world/world_sections.h"

#include <charconv>

namespace obl {
namespace {

constexpr std::array<std::string_view, kWorldSectionCount> kNames{"world", "economy", "map", "objects", "scenery", "buildings", "guests",
        "staff", "vehicles", "ferry", "repair", "feedback", "messages", "cleaning", "construction", "weather", "rng", "other"};

WorldSection ObjectSection(unsigned type)
{
    switch (type) {
    case 1: return WorldSection::Scenery;
    case 2:
    case 5: return WorldSection::Buildings;
    case 3: return WorldSection::Guests;
    case 4: return WorldSection::Vehicles;
    case 13: return WorldSection::Objects; // shark
    default: return type >= 6 && type <= 20 ? WorldSection::Staff : WorldSection::Objects;
    }
}

bool IsEconomyField(std::string_view field)
{
    for (std::string_view word : {"income", "cost", "profit", "fine"})
        if (field.find(word) != std::string_view::npos) return true;
    return false;
}

void Mix(uint32_t& hash, std::string_view text)
{
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 16777619u;
    }
}

bool ParseHex32(std::string_view text, uint32_t& out)
{
    if (text.size() != 8) return false;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out, 16);
    return ec == std::errc{} && end == text.data() + text.size();
}

} // namespace

std::string_view SectionName(WorldSection section)
{
    const auto index = static_cast<size_t>(section);
    return index < kNames.size() ? kNames[index] : std::string_view("?");
}

std::optional<WorldSection> SectionByName(std::string_view name)
{
    for (size_t i = 0; i < kNames.size(); ++i)
        if (kNames[i] == name) return static_cast<WorldSection>(i);
    return std::nullopt;
}

WorldSection SectionClassifier::Classify(std::string_view className, std::string_view field, std::string_view value)
{
    // Every class block starts with its `class` record (WorldSnapshot::Class); the Weather and object state belong to one block.
    if (field == "class" || className != m_class) {
        m_class = className;
        m_object = WorldSection::Objects;
        m_weather = false;
    }
    if (className == "World") return IsEconomyField(field) ? WorldSection::Economy : WorldSection::World;
    if (className == "Player") return WorldSection::Economy;
    if (className == "WorldMap" || className == "Path") return WorldSection::Map;
    if (className == "ObjectManager") {
        // Objects have no individual class headers; object.type marks each object's records for normalization and section hashing.
        if (field == "object.type") {
            unsigned type = 0;
            const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), type, 16);
            m_object = ec == std::errc{} && end == value.data() + value.size() && value.size() == 2 ? ObjectSection(type)
                                                                                                    : WorldSection::Objects;
        }
        return m_object;
    }
    if (className == "ConstructionManager") {
        if (field == "light_index") m_weather = true;
        return m_weather ? WorldSection::Weather : WorldSection::Construction;
    }
    if (className == "FerryManager") return WorldSection::Ferry;
    if (className == "RepairManager") return WorldSection::Repair;
    if (className == "FeedbackManager") return WorldSection::Feedback;
    if (className == "MessageHandler") return WorldSection::Messages;
    if (className == "CleaningManager") return WorldSection::Cleaning;
    if (className == "RNG") return WorldSection::Rng;
    return WorldSection::Other;
}

SectionDigests DigestSections(std::string_view text)
{
    SectionDigests digests{};
    SectionClassifier classifier;
    bool header = true;
    for (size_t pos = 0; pos < text.size();) {
        size_t newline = text.find('\n', pos);
        if (newline == std::string_view::npos) newline = text.size();
        const std::string_view line = text.substr(pos, newline - pos);
        pos = newline + 1;
        if (header) {
            header = false; // `OBL-world-v3`
            continue;
        }
        const size_t tab1 = line.find('\t');
        const size_t tab2 = tab1 == std::string_view::npos ? tab1 : line.find('\t', tab1 + 1);
        const size_t tab3 = tab2 == std::string_view::npos ? tab2 : line.find('\t', tab2 + 1);
        const std::string_view className = line.substr(0, tab1);
        const std::string_view field = tab1 == std::string_view::npos ? std::string_view() : line.substr(tab1 + 1, tab2 - tab1 - 1);
        const std::string_view value = tab3 == std::string_view::npos ? std::string_view() : line.substr(tab3 + 1);
        SectionDigest& digest = digests[static_cast<size_t>(classifier.Classify(className, field, value))];
        // Match the total checksum by hashing record types and bytes, so field labels do not change section digests.
        Mix(digest.hash, tab2 == std::string_view::npos ? line : line.substr(tab2 + 1));
        Mix(digest.hash, "\n");
        ++digest.fields;
    }
    return digests;
}

std::string FormatSectionDigest(const SectionDigest& digest)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text(8, '0');
    uint32_t hash = digest.hash;
    for (int i = 7; i >= 0; --i, hash >>= 4) text[i] = kHex[hash & 15];
    return text + '/' + std::to_string(digest.fields);
}

std::string FormatSectionDigests(const SectionDigests& digests)
{
    std::string text;
    for (size_t i = 0; i < digests.size(); ++i) {
        if (!digests[i].fields) continue;
        if (!text.empty()) text += ' ';
        text += kNames[i];
        text += '=';
        text += FormatSectionDigest(digests[i]);
    }
    return text;
}

std::optional<SectionDigests> ParseSectionDigests(std::string_view text, std::string* error)
{
    auto fail = [&](std::string why) -> std::optional<SectionDigests> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    SectionDigests digests{};
    std::array<bool, kWorldSectionCount> seen{};
    while (!text.empty()) {
        const size_t space = text.find(' ');
        const std::string_view token = text.substr(0, space);
        text = space == std::string_view::npos ? std::string_view() : text.substr(space + 1);
        if (token.empty()) continue;
        const size_t equals = token.find('=');
        const size_t slash = token.find('/');
        if (equals == std::string_view::npos || slash == std::string_view::npos || slash < equals)
            return fail("expected '<section>=<8 hex digits>/<fields>', got '" + std::string(token) + "'");
        const auto section = SectionByName(token.substr(0, equals));
        if (!section) return fail("unrecognized section '" + std::string(token.substr(0, equals)) + "'");
        const auto index = static_cast<size_t>(*section);
        if (seen[index]) return fail("section '" + std::string(kNames[index]) + "' repeated");
        seen[index] = true;
        SectionDigest& digest = digests[index];
        const std::string_view fields = token.substr(slash + 1);
        const auto [end, ec] = std::from_chars(fields.data(), fields.data() + fields.size(), digest.fields);
        if (!ParseHex32(token.substr(equals + 1, slash - equals - 1), digest.hash) || fields.empty() || ec != std::errc{} ||
                end != fields.data() + fields.size() || digest.fields == 0)
            return fail("expected '<section>=<8 hex digits>/<fields above 0>', got '" + std::string(token) + "'");
    }
    return digests;
}

std::vector<SectionDifference> DiffSections(const SectionDigests& expected, const SectionDigests& actual)
{
    std::vector<SectionDifference> differences;
    for (size_t i = 0; i < expected.size(); ++i)
        if (expected[i] != actual[i]) differences.push_back({static_cast<WorldSection>(i), expected[i], actual[i]});
    return differences;
}

std::string SectionNames(const std::vector<SectionDifference>& differences)
{
    if (differences.empty()) return "none";
    std::string names;
    for (const SectionDifference& difference : differences) {
        if (!names.empty()) names += ',';
        names += SectionName(difference.section);
    }
    return names;
}

} // namespace obl
