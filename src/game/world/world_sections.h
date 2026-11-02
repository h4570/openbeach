#pragma once

// Per-section digests of a world-hash snapshot (docs/testing/world-hashes.md, "Sections"). The total hash says whether two runs
// match; the sections say where they differ, so a mismatch report can name every differing subsystem and an intended fix re-records
// a reviewable scope. Pure code over the snapshot's TSV text, shared by the game, the headless host and the replay file.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obl {

// The order is the order of the replay file and the reports. A new section changes the digests of the records it takes over, so
// re-record the reference replays with it (their total hashes stay the same).
enum class WorldSection : uint8_t {
    World, // World fields outside economy: clock, speed, flags, power, counters, opinions, goals, script
    Economy, // World income/cost/profit/fine fields and the Player (cash)
    Map, // WorldMap grid, Path
    Objects, // ObjectManager header (counts, id list) and objects of no other section (sharks)
    Scenery, // object type 1 (scenery, plants, litter, bins)
    Buildings, // types 2 and 5 (buildings and their tables)
    Guests, // type 3
    Staff, // types 6-12 and 14-20
    Vehicles, // type 4
    Ferry, // FerryManager (ferry and events)
    Repair, // RepairManager
    Feedback, // FeedbackManager
    Messages, // MessageHandler (delayed messages)
    Cleaning, // CleaningManager
    Construction, // ConstructionManager up to the weather block
    Weather, // the Weather block saved under the ConstructionManager class (from light_index on)
    Rng, // rand-calls
    Other, // a class none of the above names
    Count,
};
inline constexpr size_t kWorldSectionCount = static_cast<size_t>(WorldSection::Count);

std::string_view SectionName(WorldSection section);
std::optional<WorldSection> SectionByName(std::string_view name);

// FNV-1a (32 bits) over the section's records in snapshot order, each as its TSV line with the newline; `fields` counts them.
struct SectionDigest {
    static constexpr uint32_t kEmptyHash = 2166136261u;
    uint32_t hash = kEmptyHash;
    uint32_t fields = 0;
    bool operator==(const SectionDigest&) const = default;
};
using SectionDigests = std::array<SectionDigest, kWorldSectionCount>;

// Assigns every record of a snapshot text (`OBL-world-v3` TSV) to its section, in one pass.
class SectionClassifier {
public:
    // `className` and `field` are the first two TSV columns, `value` the hex bytes.
    WorldSection Classify(std::string_view className, std::string_view field, std::string_view value);

private:
    std::string_view m_class;
    WorldSection m_object = WorldSection::Objects; // ObjectManager: section of the current object (header before the first)
    bool m_weather = false; // ConstructionManager: inside the Weather block
};

SectionDigests DigestSections(std::string_view text);

// `name=hhhhhhhh/fields` for every non-empty section, space-separated, in section order (the replay checkpoint suffix).
std::string FormatSectionDigests(const SectionDigests& digests);
// Reads FormatSectionDigests text; a section not named is empty. Nothing for an unknown name, a repeated one or a bad token.
std::optional<SectionDigests> ParseSectionDigests(std::string_view text, std::string* error = nullptr);

struct SectionDifference {
    WorldSection section;
    SectionDigest expected;
    SectionDigest actual;
};
std::vector<SectionDifference> DiffSections(const SectionDigests& expected, const SectionDigests& actual);
// The differing section names joined by commas (`guests,staff`), `none` when every digest matches (only the order of records across
// sections changed).
std::string SectionNames(const std::vector<SectionDifference>& differences);
// `hhhhhhhh/fields`, the spelling of one digest in the reports.
std::string FormatSectionDigest(const SectionDigest& digest);

} // namespace obl
