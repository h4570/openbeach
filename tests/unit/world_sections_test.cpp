// Per-section world-hash digests (src/game/world/world_sections.h, docs/testing/world-hashes.md "Sections").

#include "unit_test.h"

#include "game/world/world_sections.h"

using obl::DigestSections;
using obl::SectionDigest;
using obl::SectionDigests;
using obl::WorldSection;

namespace {

// The same records as the fixture of tests/tools/test_world_hash_compare.ps1, whose section counts must agree with these.
const char* const kSnapshot = "OBL-world-v3\n"
                              "World\tclass\t0\t576f726c64\n"
                              "World\tgame_time\t3\t367f1444\n"
                              "World\tincome_today\t3\t00607d44\n"
                              "World\taverage_cost\t3\t00000000\n"
                              "World\ttotal_fines\t3\t00000000\n"
                              "World\taverage_beauty\t3\t00000000\n"
                              "WorldMap\tclass\t0\t576f726c644d6170\n"
                              "WorldMap\tflags[]\t7\t0000\n"
                              "Path\tclass\t0\t50617468\n"
                              "ObjectManager\tclass\t0\t4f626a6563744d616e61676572\n"
                              "ObjectManager\tnext_id\t4\t10000000\n"
                              "ObjectManager\tobject.type\t9\t01\n"
                              "ObjectManager\tbase.id\t4\t01000000\n"
                              "ObjectManager\tobject.type\t9\t02\n"
                              "ObjectManager\tbase.id\t4\t02000000\n"
                              "ObjectManager\tbuilding.open_status\t2\t01\n"
                              "ObjectManager\tobject.type\t9\t05\n"
                              "ObjectManager\tobject.type\t9\t03\n"
                              "ObjectManager\tguest.cash\t3\t00002041\n"
                              "ObjectManager\tobject.type\t9\t04\n"
                              "ObjectManager\tobject.type\t9\t0b\n"
                              "ObjectManager\tstaff.wage\t3\t00002042\n"
                              "ObjectManager\tobject.type\t9\t0d\n"
                              "ObjectManager\tobject.type\t9\t14\n"
                              "Player\tclass\t0\t506c61796572\n"
                              "Player\tCash\t3\t0000803f\n"
                              "FerryManager\tclass\t0\t00\n"
                              "RepairManager\tclass\t0\t00\n"
                              "FeedbackManager\tclass\t0\t00\n"
                              "MessageHandler\tclass\t0\t00\n"
                              "CleaningManager\tclass\t0\t00\n"
                              "ConstructionManager\tclass\t0\t00\n"
                              "ConstructionManager\tbuilders\t4\t01000000\n"
                              "ConstructionManager\tlight_index\t4\t00000000\n"
                              "ConstructionManager\tstate\t4\t00000000\n"
                              "Mystery\tclass\t0\t00\n"
                              "RNG\tclass\t0\t524e47\n"
                              "RNG\trand-calls\t14\t0200000000000000\n";

uint32_t Fields(const SectionDigests& digests, WorldSection section)
{
    return digests[static_cast<size_t>(section)].fields;
}

} // namespace

OBL_TEST_CASE("world_sections.records_go_to_their_sections")
{
    const SectionDigests digests = DigestSections(kSnapshot);
    OBL_CHECK_EQ(Fields(digests, WorldSection::World), 3u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Economy), 5u); // income, average cost, fines; Player class and cash
    OBL_CHECK_EQ(Fields(digests, WorldSection::Map), 3u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Objects), 3u); // class, next id (header) and the shark
    OBL_CHECK_EQ(Fields(digests, WorldSection::Scenery), 2u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Buildings), 4u); // building and table
    OBL_CHECK_EQ(Fields(digests, WorldSection::Guests), 2u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Vehicles), 1u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Staff), 3u); // cleaner (11) and aerobic (20)
    OBL_CHECK_EQ(Fields(digests, WorldSection::Ferry), 1u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Repair), 1u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Feedback), 1u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Messages), 1u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Cleaning), 1u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Construction), 2u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Weather), 2u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Other), 1u);
    OBL_CHECK_EQ(Fields(digests, WorldSection::Rng), 2u);
    uint32_t total = 0;
    for (const SectionDigest& digest : digests) total += digest.fields;
    OBL_CHECK_EQ(total, 38u); // every record but the header line
}

OBL_TEST_CASE("world_sections.a_change_touches_only_its_section")
{
    std::string changed = kSnapshot;
    const size_t at = changed.find("guest.cash\t3\t00002041");
    OBL_REQUIRE(at != std::string::npos);
    changed.replace(at, 21, "guest.cash\t3\t00002042");
    const auto diff = obl::DiffSections(DigestSections(kSnapshot), DigestSections(changed));
    OBL_REQUIRE(diff.size() == 1);
    OBL_CHECK(diff[0].section == WorldSection::Guests);
    OBL_CHECK_EQ(diff[0].expected.fields, diff[0].actual.fields);
    OBL_CHECK(diff[0].expected.hash != diff[0].actual.hash);
    OBL_CHECK_EQ(obl::SectionNames(diff), std::string("guests"));
    OBL_CHECK_EQ(obl::SectionNames({}), std::string("none"));

    // A new guest adds fields to the guests only; the objects after it keep their sections.
    std::string added = kSnapshot;
    const size_t vehicle = added.find("ObjectManager\tobject.type\t9\t04");
    added.insert(vehicle, "ObjectManager\tobject.type\t9\t03\n");
    const auto grown = obl::DiffSections(DigestSections(kSnapshot), DigestSections(added));
    OBL_REQUIRE(grown.size() == 1);
    OBL_CHECK(grown[0].section == WorldSection::Guests);
    OBL_CHECK_EQ(grown[0].actual.fields, 3u);

    // A renamed field keeps every digest.
    std::string renamed = kSnapshot;
    renamed.replace(at, 6, "wallet");
    OBL_CHECK(obl::DiffSections(DigestSections(kSnapshot), DigestSections(renamed)).empty());
}

OBL_TEST_CASE("world_sections.digest_is_fnv1a_of_the_lines")
{
    const SectionDigests digests = DigestSections("OBL-world-v3\nRNG\tclass\t0\t524e47\n");
    // FNV-1a 32 of "0\t524e47\n": type and bytes, without the section and field names (schema 3).
    uint32_t hash = 2166136261u;
    for (unsigned char c : std::string("0\t524e47\n")) {
        hash ^= c;
        hash *= 16777619u;
    }
    OBL_CHECK_EQ(digests[static_cast<size_t>(WorldSection::Rng)].hash, hash);
    OBL_CHECK_EQ(digests[static_cast<size_t>(WorldSection::World)].hash, SectionDigest::kEmptyHash);
}

OBL_TEST_CASE("world_sections.text_round_trips_and_rejects_bad_tokens")
{
    const SectionDigests digests = DigestSections(kSnapshot);
    const std::string text = obl::FormatSectionDigests(digests);
    OBL_CHECK(text.starts_with("world="));
    OBL_CHECK(text.find("rng=") != std::string::npos);
    OBL_CHECK(text.find("other=") != std::string::npos);
    const auto read = obl::ParseSectionDigests(text);
    OBL_REQUIRE(read.has_value());
    OBL_CHECK(*read == digests);
    OBL_CHECK(obl::FormatSectionDigests(SectionDigests{}).empty());

    SectionDigests one{};
    one[static_cast<size_t>(WorldSection::Guests)] = {0x0000abcd, 7};
    OBL_CHECK_EQ(obl::FormatSectionDigests(one), std::string("guests=0000abcd/7"));
    OBL_CHECK(obl::ParseSectionDigests("guests=0000abcd/7") == std::optional<SectionDigests>{one});

    std::string error;
    OBL_CHECK(!obl::ParseSectionDigests("tourists=0000abcd/7", &error));
    OBL_CHECK_EQ(error, std::string("unrecognized section 'tourists'"));
    OBL_CHECK(!obl::ParseSectionDigests("guests=0000abcd/7 guests=0000abcd/7", &error));
    OBL_CHECK_EQ(error, std::string("section 'guests' repeated"));
    for (const char* bad : {"guests", "guests=abcd/7", "guests=0000abcd/", "guests=0000abcd/0", "guests=0000abcd/x", "guests=0000abcg/7"})
        OBL_CHECK(!obl::ParseSectionDigests(bad));
}

OBL_TEST_CASE("world_sections.names")
{
    for (size_t i = 0; i < obl::kWorldSectionCount; ++i) {
        const auto section = static_cast<WorldSection>(i);
        OBL_CHECK(obl::SectionByName(obl::SectionName(section)) == std::optional<WorldSection>{section});
    }
    OBL_CHECK(!obl::SectionByName("Guests").has_value());
}
