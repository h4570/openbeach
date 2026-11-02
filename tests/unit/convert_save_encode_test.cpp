#include "importer/src/readers/save/save_body.h"
#include "formats/save_document.h"
#include "importer/src/writers/catalog_writer.h"
#include "importer/src/readers/save/save_encode.h"
#include "importer/src/readers/save/save_writer.h"
#include "formats/schemas.h"
#include "old_save_builder.h"
#include "unit_test.h"
#include "core/content_key.h"

#include "core/json_schema.h"
#include "temp_dir.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using obl::convert::OldSaveBody;
using obl::test::Save;
} // namespace

OBL_TEST_CASE("convert.save_encode_reproduces_synthetic_saves")
{
    for (const Save& save : {Save::Make(2), Save::Make(1, {3, 4}, 4, "x", true, {2, 3, 4, 7})}) {
        OldSaveBody body;
        std::string error;
        OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
        OBL_CHECK(obl::convert::EncodeOldSave(body) == save.data);
    }
}

OBL_TEST_CASE("data.convert_save_encode_reproduces_local_saves")
{
    const fs::path saves = fs::path(OBL_GAME_DIR) / "SaveGames";
    if (!fs::is_directory(saves)) OBL_SKIP("no local saves");
    int checked = 0;
    for (const auto& entry : fs::directory_iterator(saves)) {
        if (entry.path().extension() != ".dat") continue;
        std::vector<uint8_t> bytes;
        OldSaveBody body;
        std::string error;
        const bool read = obl::convert::ReadBoundedFile(entry.path().string(), obl::convert::kMaxOldSaveBytes, bytes, error) &&
                obl::convert::ReadOldSaveBody(bytes, body, error);
        if (!read) std::fprintf(stderr, "%s: %s\n", entry.path().string().c_str(), error.c_str());
        OBL_REQUIRE(read);
        const std::vector<uint8_t> encoded = obl::convert::EncodeOldSave(body);
        if (encoded != bytes) {
            const auto diff = std::mismatch(encoded.begin(), encoded.end(), bytes.begin(), bytes.end());
            std::fprintf(stderr, "%s: encoded bytes differ at %zu (sizes %zu, %zu)\n", entry.path().string().c_str(),
                    static_cast<size_t>(diff.first - encoded.begin()), encoded.size(), bytes.size());
        }
        OBL_CHECK(encoded == bytes);
        ++checked;
    }
    if (!checked) OBL_SKIP("no local .dat saves");
}

namespace {
// Labels whose records differ between two bodies, section and object by section and object.
std::vector<std::string> BodyDifferences(const OldSaveBody& a, const OldSaveBody& b)
{
    std::vector<std::string> out;
    const auto blocks = [&](const std::vector<obl::convert::OldBlock>& x, const std::vector<obl::convert::OldBlock>& y, const char* what) {
        if (x.size() != y.size())
            out.push_back(std::string(what) + " count " + std::to_string(x.size()) + " vs " + std::to_string(y.size()));
        for (size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
            const auto& fx = x[i].fields;
            const auto& fy = y[i].fields;
            if (fx.size() != fy.size())
                out.push_back(
                        x[i].kind + " #" + std::to_string(i) + " fields " + std::to_string(fx.size()) + " vs " + std::to_string(fy.size()));
            for (size_t j = 0; j < std::min(fx.size(), fy.size()); ++j) {
                const auto& rx = fx[j].record;
                const auto& ry = fy[j].record;
                // Names are saved as their content keys: a spelling does not come back.
                const auto key = [](const auto& r) {
                    return obl::content_key::ContentKey({reinterpret_cast<const char*>(r.bytes.data()), r.bytes.size()});
                };
                if (fx[j].name == fy[j].name && (fx[j].name == "m_Name" || fx[j].name == "Object Name") && key(rx) == key(ry)) continue;
                // The converter clears these uninitialized words on purpose.
                if (fx[j].name == fy[j].name && fx[j].name == "m_GroupID" && ry.Bits() == 0) continue;
                if (fx[j].name == fy[j].name && (fx[j].name == "m_dwBuildingID" || fx[j].name == "m_OwnerBuildID") &&
                        ry.Bits() == 0xffffffffu)
                    continue;
                if (fx[j].name != fy[j].name || rx.tag != ry.tag || rx.count != ry.count ||
                        !std::equal(rx.bytes.begin(), rx.bytes.end(), ry.bytes.begin(), ry.bytes.end())) {
                    // The bytes from the first difference on.
                    size_t at = 0;
                    while (at < rx.bytes.size() && at < ry.bytes.size() && rx.bytes[at] == ry.bytes[at]) ++at;
                    std::string dump = x[i].kind + " #" + std::to_string(i) + " " + fx[j].name + " n" + std::to_string(rx.count) + "/" +
                            std::to_string(ry.count) + " @" + std::to_string(at) + " [";
                    char hex[4];
                    for (size_t k = at; k < rx.bytes.size() && k < at + 24; ++k) {
                        std::snprintf(hex, sizeof(hex), "%02x", rx.bytes[k]);
                        dump += hex;
                    }
                    dump += "] [";
                    for (size_t k = at; k < ry.bytes.size() && k < at + 24; ++k) {
                        std::snprintf(hex, sizeof(hex), "%02x", ry.bytes[k]);
                        dump += hex;
                    }
                    out.push_back(dump + "]");
                }
            }
        }
    };
    blocks(a.sections, b.sections, "sections");
    blocks(a.objects, b.objects, "objects");
    return out;
}
} // namespace

// Compare the complete file, names as content keys, after mapping through a serialized obl.save document.
OBL_TEST_CASE("data.convert_save_round_trip")
{
    const fs::path game(OBL_GAME_DIR);
    const fs::path saves = game / "SaveGames";
    if (!fs::is_directory(saves) || !fs::exists(game / "Data" / "Script" / "BuildingList.ble"))
        OBL_SKIP("no local saves or catalog source");
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadSchemaDirectory(fs::path(OBL_REPO_DIR) / "data" / "schemas", schemas, error));
    const obl::test::TempPath packTmp("obl_convert_save_round_trip");
    const auto& pack = packTmp.path;
    int written = 0;
    OBL_REQUIRE(obl::convert::ConvertCatalog(
            *obl::convert::OpenDirectorySource(game), *obl::convert::OpenDirectorySink(pack), schemas, written, error));
    std::ifstream in(pack / "catalog" / "index.json", std::ios::binary);
    const nlohmann::json index = nlohmann::json::parse(in);
    in.close();
    obl::convert::SaveOrigin origin;
    origin.file = "x.dat";
    origin.packHash = std::string(64, '0');
    origin.language = "pl"; // the local installation's language
    int checked = 0;
    for (const auto& entry : fs::directory_iterator(saves)) {
        if (entry.path().extension() != ".dat") continue;
        std::vector<uint8_t> bytes;
        OldSaveBody body, back;
        nlohmann::ordered_json document;
        OBL_REQUIRE(obl::convert::ReadBoundedFile(entry.path().string(), obl::convert::kMaxOldSaveBytes, bytes, error));
        OBL_REQUIRE(obl::convert::ReadOldSaveBody(bytes, body, error));
        OBL_REQUIRE(obl::convert::OldSaveDocument(body, index, origin, document, error));
        const nlohmann::json parsed = nlohmann::json::parse(document.dump()); // as the loader reads the file
        // The bridge's checks (no false refusal); data.convert_writes_local_saves_as_obl_save validates the schema.
        const auto checks = obl::formats::CheckOblSaveDocument(parsed, origin);
        for (const std::string& refusal : checks.refusals)
            std::fprintf(stderr, "%s: refused %s\n", entry.path().string().c_str(), refusal.c_str());
        OBL_CHECK(checks.refusals.empty());
        OBL_CHECK(checks.warnings.empty());
        const bool mapped = obl::convert::OldSaveBodyFromDocument(parsed, index, origin, back, error);
        if (!mapped) std::fprintf(stderr, "%s: %s\n", entry.path().string().c_str(), error.c_str());
        OBL_REQUIRE(mapped);
        const auto differences = BodyDifferences(body, back);
        for (size_t i = 0; i < std::min<size_t>(differences.size(), 25); ++i)
            std::fprintf(stderr, "%s: differs %s\n", entry.path().filename().string().c_str(), differences[i].c_str());
        OBL_CHECK(differences.empty());
        const auto encoded = obl::convert::EncodeOldSave(back);
        OldSaveBody again;
        OBL_REQUIRE(obl::convert::ReadOldSaveBody(encoded, again, error));
        OBL_CHECK(BodyDifferences(body, again).empty());
        ++checked;
    }
    if (!checked) OBL_SKIP("no local .dat saves");
}

OBL_TEST_CASE("convert.save_clears_uninitialized_object_words")
{
    const auto convert = [](const Save& save, const char* field, uint32_t value) {
        OldSaveBody body;
        std::string error;
        OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
        const uint8_t bytes[] = {uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
        for (auto& f : body.objects[0].fields)
            if (f.name == field || f.name == "m_GroupID") f.record.bytes = bytes;
        obl::convert::SaveOrigin origin;
        origin.file = "own.dat";
        origin.packHash = std::string(64, '0');
        nlohmann::ordered_json document;
        OBL_REQUIRE(obl::convert::OldSaveDocument(body, nlohmann::json::object(), origin, document, error));
        return document["objects"][0];
    };
    const Save shark = Save::Make(0, {}, 4, "x", false, {13});
    const Save scenery = Save::Make(1, {}, 4, "Palm 4s");
    for (const uint32_t stale : {0x7f00abcdu, 0xfffffffeu, 99999u}) { // -2 would otherwise reach the world receiver
        const auto moving = convert(shark, "m_dwBuildingID", stale);
        OBL_CHECK(moving["moving"]["building"] == -1);
        OBL_CHECK(moving["moving"]["group"] == 0);
        OBL_CHECK(convert(scenery, "m_OwnerBuildID", stale)["scenery"]["owner_building"] == -1);
    }
    const uint32_t own = convert(shark, "m_dwBuildingID", 0)["id"];
    OBL_CHECK(convert(shark, "m_dwBuildingID", own)["moving"]["building"] == own); // a live id stays
    const uint32_t prop = convert(scenery, "m_OwnerBuildID", 0)["id"];
    OBL_CHECK(convert(scenery, "m_OwnerBuildID", prop)["scenery"]["owner_building"] == prop);
}

OBL_TEST_CASE("convert.save_round_trip_keeps_height_and_byte_flags")
{
    const Save save = Save::Make(0, {}, 4, "x", false, {13}); // a shark has Mover state
    const nlohmann::json index = nlohmann::json::object();
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadSchemaDirectory(fs::path(OBL_REPO_DIR) / "data" / "schemas", schemas, error));
    for (const uint32_t height : {0x42190000u, 0x80000000u, 0x7fc01234u}) {
        for (const uint8_t flag : {uint8_t{0}, uint8_t{1}, uint8_t{0x4c}, uint8_t{0xa8}, uint8_t{255}}) {
            OldSaveBody body, back;
            OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
            const uint8_t z[] = {uint8_t(height), uint8_t(height >> 8), uint8_t(height >> 16), uint8_t(height >> 24)};
            for (auto& field : body.objects[0].fields) {
                if (field.name == "m_WorldXYZ.z") field.record.bytes = z;
                if (field.name == "m_bBuildingTraversal") field.record.bytes = std::span<const uint8_t>(&flag, 1);
            }
            obl::convert::SaveOrigin origin;
            origin.file = "own.dat";
            origin.packHash = std::string(64, '0');
            nlohmann::ordered_json document;
            OBL_REQUIRE(obl::convert::OldSaveDocument(body, index, origin, document, error));
            OBL_REQUIRE(obl::formats::ValidateDocument(schemas, "obl.save", document, error));
            OBL_CHECK(document["objects"][0]["moving"]["building_traversal"] == flag);
            const auto parsed = nlohmann::json::parse(document.dump());
            OBL_REQUIRE(obl::convert::OldSaveBodyFromDocument(parsed, index, origin, back, error));
            OBL_CHECK(back.objects[0].Find("m_WorldXYZ.z")->record.Bits() == height);
            OBL_CHECK(back.objects[0].Find("m_bBuildingTraversal")->record.Bits() == flag);
            OBL_CHECK(back.header.title.bytes.size() == body.header.title.bytes.size());
            for (const auto& bad : {nlohmann::json(256), nlohmann::json(-1), nlohmann::json(true), nlohmann::json("1")}) {
                auto invalid = document;
                invalid["objects"][0]["moving"]["building_traversal"] = bad;
                OBL_CHECK(!obl::formats::ValidateDocument(schemas, "obl.save", invalid, error));
                OBL_CHECK(!obl::convert::OldSaveBodyFromDocument(invalid, index, origin, back, error));
            }
        }
    }
}
