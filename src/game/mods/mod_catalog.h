#pragma once

#include "core/engine_version.h"
#include "formats/mod_pack.h"
#include "game/mods/ed25519.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Mod catalogs (docs/modding/catalogs.md): signed third-party indexes (`obl.mod_catalog`) the player adds by HTTPS URL.
// Pure logic: no network calls, no OS headers, no simulation state.
namespace obl::mods {

// The version helpers live in core/engine_version.h, which mod.json's own check (formats/mod_pack) shares.
using obl::CompareVersion;
using obl::InVersionRange;
using obl::kEngineVersion;
using obl::ParseVersion;
using obl::SemVer;

inline constexpr const char* kCatalogFormat = "obl.mod_catalog";
inline constexpr int kCatalogVersion = 1;
inline constexpr const char* kCatalogStoreFormat = "obl.mod_catalogs";
inline constexpr int kCatalogStoreVersion = 1;
inline constexpr const char* kCatalogStoreFile = "mod-catalogs.json";
inline constexpr const char* kOfficialCatalogUrl = "https://openbeach.app/mods/catalog.json";
inline constexpr const char* kOfficialCatalogTitle = "OpenBeach Official Mods";
// A third-party source listed after the official one. It is added once, so a player who removes it keeps it removed, and its
// key ships pinned: a catalog at that address signed with any other key asks the player before it is trusted.
inline constexpr const char* kCommunityCatalogUrl = "https://openbeach-larry.github.io/mods/catalog.json";
inline constexpr const char* kCommunityCatalogTitle = "Community Mods";
inline constexpr const char* kCommunityCatalogPublicKey = "06a123bc3a0f9683d8b54f927973fbd0c61c71f11770554af8be675f07f6ea3c";
inline constexpr size_t kMaxCatalogBytes = 4 * 1024 * 1024;
inline constexpr size_t kMaxCatalogEntries = 2048;

struct CatalogEntry {
    std::string id;
    std::string title;
    std::string description; // optional, as in mod.json
    std::string modVersion;
    std::vector<std::string> packs;
    mod_pack::Scope scope = mod_pack::Scope::World;
    int api = 0;
    std::optional<SemVer> engineMin;
    std::optional<SemVer> engineMax;
    std::vector<std::string> requiresMods;
    std::string url; // resolved HTTPS URL (or file:// for local test catalogs)
    std::string sha256; // 64 lowercase hex digits of the ZIP archive
    std::string contentHash; // optional 64 lowercase hex digits of ModContentHash (for save lookup)
    uint64_t size = 0; // exact byte length of the ZIP archive
    std::string license;
};

struct Catalog {
    std::string title;
    std::string publicKeyHex;
    ed25519::PublicKey publicKey{};
    std::vector<CatalogEntry> mods;
};

// Validates and parses a signed catalog.json document. `catalogUrl` is the address it was fetched from (used to resolve
// relative mod URLs and enforce HTTPS on remote catalogs). `allowLocal` permits file:// catalog URLs for local/test catalogs.
bool ParseCatalog(std::string_view text, std::string_view catalogUrl, bool allowLocal, Catalog& out, std::string& error);

// Signs an unsigned catalog JSON object (setting `public_key` and `signature`) using `seed`.
bool SignCatalogJson(std::string_view unsignedJson, const ed25519::Seed& seed, std::string& signedOut, std::string& error);

// Checks whether a catalog entry can run on the given engine version, pack and presentation script API.
bool IsCatalogModCompatible(const CatalogEntry& entry, const SemVer& engineVersion, std::string_view packId, int presentationApi);

// Returns only the entries compatible with the given engine version, pack and presentation script API.
std::vector<CatalogEntry> FilterCompatibleMods(
        std::span<const CatalogEntry> mods, const SemVer& engineVersion, std::string_view packId, int presentationApi);

// Computes the install order for `targetId` (dependencies first, ending with `targetId`), skipping mods already in
// `installedIds` (unless `includeTargetIfInstalled` is true, e.g. for updates). Detects missing dependencies and cycles.
bool ResolveInstallOrder(std::string_view targetId, std::span<const CatalogEntry> available, std::span<const std::string> installedIds,
        bool includeTargetIfInstalled, std::vector<CatalogEntry>& orderOut, std::string& error);

// What the online list offers for a catalog entry: Install when the mod is not installed (`installedVersion` null), Update when the
// installed version is older or not a valid version, UpToDate when it is the same or newer (a newer local copy is never replaced).
enum class CatalogOffer { Install, Update, UpToDate };
CatalogOffer OfferFor(const CatalogEntry& entry, const std::string* installedVersion);

// Checks that downloaded archive bytes match the catalog entry's declared size and SHA-256 before passing to ModArchive.
bool VerifyCatalogDownload(std::span<const uint8_t> bytes, const CatalogEntry& entry, std::string& error);

// Validates a catalog URL entered by the user (`https://...`, or `file://...` when `allowLocal` is true).
bool IsValidCatalogUrl(std::string_view url, bool allowLocal, std::string& error);

struct StoredCatalog {
    std::string url;
    std::string title;
    std::string pinnedPublicKeyHex;
    std::vector<CatalogEntry> mods;
    bool allowLocal = false;
    bool enabled = true;
    bool official = false;
};

struct CatalogModMatch {
    std::string catalogTitle;
    std::string catalogUrl;
    CatalogEntry entry;
};

// Persistent store of online mod sources (`<game dir>/mod-catalogs.json`) with TOFU key pinning.
class CatalogStore {
public:
    enum class UpdateStatus { Added, Updated, KeyChanged, Failed };

    CatalogStore();

    bool Load(const std::filesystem::path& gameDir, std::string& error);
    bool Save(const std::filesystem::path& gameDir, std::string& error) const;

    // Adds a new catalog (pinning its key) or updates an existing one. If the catalog's public key changed since it was
    // pinned and `trustNewKey` is false, returns UpdateStatus::KeyChanged without replacing the pinned key or cached mods.
    UpdateStatus AddOrUpdate(std::string_view url, const Catalog& catalog, bool allowLocal, bool trustNewKey, std::string& error);

    bool Remove(std::string_view url);
    bool SetEnabled(std::string_view url, bool enabled);
    const StoredCatalog* Find(std::string_view url) const;
    const std::vector<StoredCatalog>& Catalogs() const { return m_catalogs; }

    // Looks up a missing save mod by id and content hash across all enabled online sources.
    std::optional<CatalogModMatch> FindModByIdAndHash(std::string_view id, std::string_view contentHash) const;

private:
    void EnsureOfficialCatalog();
    void EnsureDefaultCatalogs();

    std::vector<StoredCatalog> m_catalogs;
    // Default sources already offered to this player (`seeded_defaults`), so a removed one is not added back.
    std::vector<std::string> m_seededDefaults;
};

} // namespace obl::mods
