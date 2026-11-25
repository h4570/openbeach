#include "unit_test.h"

#include "core/sha256.h"
#include "game/mods/ed25519.h"
#include "game/mods/mod_catalog.h"
#include "temp_dir.h"

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

using namespace obl;

namespace {

template<size_t N> std::array<uint8_t, N> FromHex(std::string_view hex)
{
    std::array<uint8_t, N> out{};
    for (size_t i = 0; i < N; ++i) {
        unsigned v = 0;
        std::from_chars(hex.data() + i * 2, hex.data() + i * 2 + 2, v, 16);
        out[i] = static_cast<uint8_t>(v);
    }
    return out;
}

std::vector<uint8_t> BytesFromHex(std::string_view hex)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned v = 0;
        std::from_chars(hex.data() + i, hex.data() + i + 2, v, 16);
        out.push_back(static_cast<uint8_t>(v));
    }
    return out;
}

constexpr ed25519::Seed kSeedA = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44,
        0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
constexpr ed25519::Seed kSeedB = {0x4c, 0xcd, 0x08, 0x9b, 0x28, 0xff, 0x96, 0xda, 0x9d, 0xb6, 0xc3, 0x46, 0xec, 0x11, 0x4e, 0x0f, 0x5b,
        0x8a, 0x31, 0x9f, 0x35, 0xab, 0xa6, 0x24, 0xda, 0x8c, 0xf6, 0xed, 0x4f, 0xb8, 0xa6, 0xfb};

std::string MakeSignedCatalog(const nlohmann::json& doc, const ed25519::Seed& seed = kSeedA)
{
    std::string signedText;
    std::string error;
    OBL_REQUIRE(mods::SignCatalogJson(doc.dump(), seed, signedText, error));
    return signedText;
}

nlohmann::json SampleEntry(std::string id = "fps_counter", std::string url = "fps_counter-1.0.0.zip")
{
    return nlohmann::json{
            {"id", std::move(id)},
            {"title", "FPS counter"},
            {"mod_version", "1.0.0"},
            {"packs", {"base", "l0"}},
            {"scope", "presentation"},
            {"api", 1},
            {"engine_min", "1.10.0"},
            {"engine_max", "2.0.0"},
            {"url", std::move(url)},
            {"sha256", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
            {"content_hash", "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"},
            {"size", 4210},
            {"license", "MIT"},
    };
}

nlohmann::json SampleCatalogJson()
{
    return nlohmann::json{
            {"format", "obl.mod_catalog"},
            {"version", 1},
            {"title", "Community Mods"},
            {"mods", nlohmann::json::array({SampleEntry()})},
    };
}

} // namespace

OBL_TEST_CASE("mod_catalog.ed25519_rfc8032_vectors")
{
    // RFC 8032 Section 7.1 Test 1 (empty message)
    {
        const auto pk = ed25519::PublicKeyFromSeed(kSeedA);
        const auto expectedPk = FromHex<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
        OBL_CHECK(pk == expectedPk);

        const auto sig = ed25519::Sign(kSeedA, {});
        const auto expectedSig = FromHex<64>("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                                             "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
        OBL_CHECK(sig == expectedSig);
        OBL_CHECK(ed25519::Verify(pk, {}, sig));

        auto badSig = sig;
        badSig[0] ^= 1;
        OBL_CHECK(!ed25519::Verify(pk, {}, badSig));
        const uint8_t extra = 0;
        OBL_CHECK(!ed25519::Verify(pk, {&extra, 1}, sig));
    }

    // RFC 8032 Section 7.1 Test 2 (1-byte message 0x72)
    {
        const auto pk = ed25519::PublicKeyFromSeed(kSeedB);
        const auto expectedPk = FromHex<32>("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
        OBL_CHECK(pk == expectedPk);

        const uint8_t msg[1] = {0x72};
        const auto sig = ed25519::Sign(kSeedB, msg);
        const auto expectedSig = FromHex<64>("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                                             "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
        OBL_CHECK(sig == expectedSig);
        OBL_CHECK(ed25519::Verify(pk, msg, sig));
    }

    // RFC 8032 Section 7.1 Test 3 (2-byte message 0xaf82)
    {
        const auto seed = FromHex<32>("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7");
        const auto pk = ed25519::PublicKeyFromSeed(seed);
        const auto expectedPk = FromHex<32>("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025");
        OBL_CHECK(pk == expectedPk);

        const auto msg = BytesFromHex("af82");
        const auto sig = ed25519::Sign(seed, msg);
        const auto expectedSig = FromHex<64>("6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
                                             "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a");
        OBL_CHECK(sig == expectedSig);
        OBL_CHECK(ed25519::Verify(pk, msg, sig));
    }
}

OBL_TEST_CASE("mod_catalog.parses_valid_signed_catalog_and_resolves_urls")
{
    const std::string text = MakeSignedCatalog(SampleCatalogJson());
    mods::Catalog parsed;
    std::string error;
    OBL_REQUIRE(mods::ParseCatalog(text, "https://example.org/mods/catalog.json", false, parsed, error));
    OBL_CHECK(parsed.title == "Community Mods");
    OBL_REQUIRE(parsed.mods.size() == 1);
    OBL_CHECK(parsed.mods[0].id == "fps_counter");
    OBL_CHECK(parsed.mods[0].url == "https://example.org/mods/fps_counter-1.0.0.zip");
    OBL_CHECK(parsed.mods[0].scope == mod_pack::Scope::Presentation);
    OBL_CHECK(parsed.mods[0].api == 1);

    // Local file:// catalog resolves relative URLs against its directory.
    mods::Catalog localParsed;
    OBL_REQUIRE(mods::ParseCatalog(text, "file:///tmp/cat/catalog.json", true, localParsed, error));
    OBL_CHECK(localParsed.mods[0].url == "file:///tmp/cat/fps_counter-1.0.0.zip");
}

OBL_TEST_CASE("mod_catalog.refuses_invalid_format_fields_and_urls")
{
    mods::Catalog out;
    std::string error;

    // Non-HTTPS catalog URL rejected when allowLocal is false.
    const std::string validText = MakeSignedCatalog(SampleCatalogJson());
    OBL_CHECK(!mods::ParseCatalog(validText, "http://example.org/catalog.json", false, out, error));
    OBL_CHECK(!mods::ParseCatalog(validText, "file:///tmp/catalog.json", false, out, error));

    // Wrong format.
    {
        auto doc = SampleCatalogJson();
        doc["format"] = "obl.other";
        OBL_CHECK(!mods::ParseCatalog(MakeSignedCatalog(doc), "https://example.org/catalog.json", false, out, error));
    }
    // Non-HTTPS mod URL in HTTPS catalog.
    {
        auto doc = SampleCatalogJson();
        doc["mods"][0]["url"] = "http://example.org/mod.zip";
        OBL_CHECK(!mods::ParseCatalog(MakeSignedCatalog(doc), "https://example.org/catalog.json", false, out, error));
    }
    // Path traversal in local catalog mod URL.
    {
        auto doc = SampleCatalogJson();
        doc["mods"][0]["url"] = "../escape.zip";
        OBL_CHECK(!mods::ParseCatalog(MakeSignedCatalog(doc), "file:///tmp/cat/catalog.json", true, out, error));
    }
    // Duplicate mod ids.
    {
        auto doc = SampleCatalogJson();
        doc["mods"].push_back(SampleEntry("fps_counter", "fps2.zip"));
        OBL_CHECK(!mods::ParseCatalog(MakeSignedCatalog(doc), "https://example.org/catalog.json", false, out, error));
    }
    // Invalid engine range (min > max).
    {
        auto doc = SampleCatalogJson();
        doc["mods"][0]["engine_min"] = "2.0.0";
        doc["mods"][0]["engine_max"] = "1.0.0";
        OBL_CHECK(!mods::ParseCatalog(MakeSignedCatalog(doc), "https://example.org/catalog.json", false, out, error));
    }
}

OBL_TEST_CASE("mod_catalog.refuses_tampered_signature")
{
    auto doc = nlohmann::json::parse(MakeSignedCatalog(SampleCatalogJson()));
    doc["title"] = "Tampered Catalog";
    mods::Catalog out;
    std::string error;
    OBL_CHECK(!mods::ParseCatalog(doc.dump(), "https://example.org/catalog.json", false, out, error));
    OBL_CHECK(error.find("signature") != std::string::npos);
}

OBL_TEST_CASE("mod_catalog.pins_key_and_warns_on_key_change")
{
    test::TempPath dir("mod-catalog-store");
    std::filesystem::create_directory(dir.path);

    const std::string url = "https://example.org/catalog.json";
    mods::Catalog catA;
    std::string error;
    OBL_REQUIRE(mods::ParseCatalog(MakeSignedCatalog(SampleCatalogJson(), kSeedA), url, false, catA, error));

    mods::CatalogStore store;
    OBL_REQUIRE(store.AddOrUpdate(url, catA, false, false, error) == mods::CatalogStore::UpdateStatus::Added);
    OBL_REQUIRE(store.Save(dir.path, error));

    // Reload from disk and verify pinned key persists.
    mods::CatalogStore reloaded;
    OBL_REQUIRE(reloaded.Load(dir.path, error));
    OBL_REQUIRE(reloaded.Catalogs().size() == 1);
    OBL_CHECK(reloaded.Catalogs()[0].pinnedPublicKeyHex == catA.publicKeyHex);

    // Refresh with same key succeeds.
    OBL_CHECK(reloaded.AddOrUpdate(url, catA, false, false, error) == mods::CatalogStore::UpdateStatus::Updated);

    // Refresh with a different valid key warns and keeps the old key until trusted.
    auto docB = SampleCatalogJson();
    docB["title"] = "New Owner Catalog";
    mods::Catalog catB;
    OBL_REQUIRE(mods::ParseCatalog(MakeSignedCatalog(docB, kSeedB), url, false, catB, error));
    OBL_CHECK(reloaded.AddOrUpdate(url, catB, false, false, error) == mods::CatalogStore::UpdateStatus::KeyChanged);
    OBL_CHECK(reloaded.Find(url)->pinnedPublicKeyHex == catA.publicKeyHex);
    OBL_CHECK(reloaded.Find(url)->title == "Community Mods");

    // Explicit trustNewKey updates the pinned key.
    OBL_CHECK(reloaded.AddOrUpdate(url, catB, false, true, error) == mods::CatalogStore::UpdateStatus::Updated);
    OBL_CHECK(reloaded.Find(url)->pinnedPublicKeyHex == catB.publicKeyHex);
    OBL_CHECK(reloaded.Find(url)->title == "New Owner Catalog");
}

OBL_TEST_CASE("mod_catalog.verifies_download_size_and_sha256")
{
    const std::vector<uint8_t> payload = {'P', 'K', 3, 4, 1, 2, 3};
    mods::CatalogEntry entry;
    entry.size = payload.size();
    entry.sha256 = sha256::Hex(payload);

    std::string error;
    OBL_CHECK(mods::VerifyCatalogDownload(payload, entry, error));

    auto tampered = payload;
    tampered[0] ^= 1;
    OBL_CHECK(!mods::VerifyCatalogDownload(tampered, entry, error));
    OBL_CHECK(error.find("SHA-256") != std::string::npos);

    auto shorter = payload;
    shorter.pop_back();
    OBL_CHECK(!mods::VerifyCatalogDownload(shorter, entry, error));
    OBL_CHECK(error.find("size") != std::string::npos);
}

OBL_TEST_CASE("mod_catalog.filters_by_engine_version_pack_and_api")
{
    mods::SemVer engine{1, 13, 0};
    mods::CatalogEntry ok;
    ok.id = "ok";
    ok.packs = {"l0"};
    ok.api = 1;
    ok.engineMin = mods::SemVer{1, 12, 0};
    ok.engineMax = mods::SemVer{1, 13, 0};

    mods::CatalogEntry tooNew = ok;
    tooNew.id = "too_new";
    tooNew.engineMin = mods::SemVer{1, 14, 0};

    mods::CatalogEntry tooOld = ok;
    tooOld.id = "too_old";
    tooOld.engineMax = mods::SemVer{1, 12, 9};

    mods::CatalogEntry wrongPack = ok;
    wrongPack.id = "wrong_pack";
    wrongPack.packs = {"base"};

    mods::CatalogEntry highApi = ok;
    highApi.id = "high_api";
    highApi.api = 2;

    const std::vector<mods::CatalogEntry> all = {ok, tooNew, tooOld, wrongPack, highApi};
    const auto filtered = mods::FilterCompatibleMods(all, engine, "l0", 1);
    OBL_REQUIRE(filtered.size() == 1);
    OBL_CHECK(filtered[0].id == "ok");
}

OBL_TEST_CASE("mod_catalog.resolves_dependency_order_and_rejects_cycles_or_missing")
{
    mods::CatalogEntry base;
    base.id = "base_lib";
    mods::CatalogEntry mid;
    mid.id = "mid_lib";
    mid.requiresMods = {"base_lib"};
    mods::CatalogEntry app;
    app.id = "app_mod";
    app.requiresMods = {"mid_lib"};

    const std::vector<mods::CatalogEntry> available = {app, base, mid};
    std::vector<mods::CatalogEntry> order;
    std::string error;

    // Fresh install: base_lib -> mid_lib -> app_mod.
    OBL_REQUIRE(mods::ResolveInstallOrder("app_mod", available, {}, true, order, error));
    OBL_REQUIRE(order.size() == 3);
    OBL_CHECK(order[0].id == "base_lib");
    OBL_CHECK(order[1].id == "mid_lib");
    OBL_CHECK(order[2].id == "app_mod");

    // base_lib already installed: skipped.
    const std::vector<std::string> installed = {"base_lib"};
    OBL_REQUIRE(mods::ResolveInstallOrder("app_mod", available, installed, true, order, error));
    OBL_REQUIRE(order.size() == 2);
    OBL_CHECK(order[0].id == "mid_lib");
    OBL_CHECK(order[1].id == "app_mod");

    // Missing dependency fails.
    mods::CatalogEntry broken;
    broken.id = "broken";
    broken.requiresMods = {"nonexistent"};
    const std::vector<mods::CatalogEntry> withBroken = {broken};
    OBL_CHECK(!mods::ResolveInstallOrder("broken", withBroken, {}, true, order, error));
    OBL_CHECK(error.find("nonexistent") != std::string::npos);

    // Cycle fails.
    mods::CatalogEntry cycA;
    cycA.id = "cyc_a";
    cycA.requiresMods = {"cyc_b"};
    mods::CatalogEntry cycB;
    cycB.id = "cyc_b";
    cycB.requiresMods = {"cyc_a"};
    const std::vector<mods::CatalogEntry> withCycle = {cycA, cycB};
    OBL_CHECK(!mods::ResolveInstallOrder("cyc_a", withCycle, {}, true, order, error));
    OBL_CHECK(error.find("circular") != std::string::npos);
}

OBL_TEST_CASE("mod_catalog.finds_missing_save_mod_by_id_and_hash")
{
    mods::Catalog cat;
    std::string error;
    const std::string url = "https://example.org/catalog.json";
    OBL_REQUIRE(mods::ParseCatalog(MakeSignedCatalog(SampleCatalogJson()), url, false, cat, error));

    mods::CatalogStore store;
    OBL_REQUIRE(store.AddOrUpdate(url, cat, false, false, error) == mods::CatalogStore::UpdateStatus::Added);

    const auto found = store.FindModByIdAndHash("fps_counter", "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");
    OBL_REQUIRE(found.has_value());
    OBL_CHECK(found->catalogTitle == "Community Mods");
    OBL_CHECK(found->entry.id == "fps_counter");

    OBL_CHECK(!store.FindModByIdAndHash("fps_counter", "0000000000000000000000000000000000000000000000000000000000000000").has_value());
    OBL_CHECK(!store.FindModByIdAndHash("other", "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210").has_value());
}
