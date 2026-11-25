#include "game/mods/mod_catalog.h"

#include "core/pack_path.h"
#include "core/sha256.h"
#include "game/mods/mod_archive.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <fstream>
#include <map>
#include <set>
#include <utility>

namespace obl::mods {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr std::string_view kSigningPrefix = "obl.mod_catalog:v1\n";

bool IsLowerHex(std::string_view s, size_t expectedLen)
{
    if (s.size() != expectedLen) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

template<size_t N> bool HexToBytes(std::string_view hex, std::array<uint8_t, N>& out)
{
    if (!IsLowerHex(hex, N * 2)) return false;
    for (size_t i = 0; i < N; ++i) {
        unsigned value = 0;
        const auto res = std::from_chars(hex.data() + i * 2, hex.data() + i * 2 + 2, value, 16);
        if (res.ec != std::errc{} || res.ptr != hex.data() + i * 2 + 2) return false;
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

std::string BytesToHex(std::span<const uint8_t> bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0f]);
    }
    return out;
}

bool HasControlOrSpace(std::string_view s)
{
    for (unsigned char c : s) {
        if (c <= 0x20 || c >= 0x7f) return true;
    }
    return false;
}

bool IsHttpsUrl(std::string_view url)
{
    if (!url.starts_with("https://") || HasControlOrSpace(url)) return false;
    const std::string_view rest = url.substr(8);
    const size_t slash = rest.find('/');
    const std::string_view host = slash == std::string_view::npos ? rest : rest.substr(0, slash);
    return !host.empty() && host.find('@') == std::string_view::npos;
}

bool IsFileUrl(std::string_view url)
{
    return url.starts_with("file://") && url.size() > 7 && !HasControlOrSpace(url);
}

bool IsSafeRelativePath(std::string_view rel)
{
    if (rel.empty() || rel.front() == '/' || rel.find('\\') != std::string_view::npos || rel.find(':') != std::string_view::npos ||
            HasControlOrSpace(rel))
        return false;
    size_t start = 0;
    while (start <= rel.size()) {
        const size_t end = rel.find('/', start);
        const std::string_view part = rel.substr(start, end == std::string_view::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

bool ResolveModUrl(std::string_view catalogUrl, bool allowLocal, std::string_view rawUrl, std::string& resolved, std::string& error)
{
    if (rawUrl.empty()) {
        error = "mod url is empty";
        return false;
    }
    if (IsFileUrl(catalogUrl)) {
        if (!allowLocal) {
            error = "local catalog URLs are not allowed";
            return false;
        }
        if (rawUrl.find("://") != std::string_view::npos) {
            error = "local catalog mod url must be a relative path";
            return false;
        }
        if (!IsSafeRelativePath(rawUrl)) {
            error = "unsafe relative mod url in local catalog";
            return false;
        }
        const size_t slash = catalogUrl.rfind('/');
        if (slash == std::string_view::npos || slash < 7) {
            error = "invalid local catalog URL";
            return false;
        }
        resolved = std::string(catalogUrl.substr(0, slash + 1)) + std::string(rawUrl);
        return true;
    }

    // Remote HTTPS catalog: mod URL must be HTTPS or a safe relative path under the catalog directory.
    if (rawUrl.find("://") != std::string_view::npos) {
        if (!IsHttpsUrl(rawUrl)) {
            error = "mod url must use https://";
            return false;
        }
        resolved = std::string(rawUrl);
        return true;
    }
    if (!IsSafeRelativePath(rawUrl)) {
        error = "invalid relative mod url";
        return false;
    }
    const size_t query = catalogUrl.find_first_of("?#");
    const std::string_view clean = catalogUrl.substr(0, query);
    const size_t slash = clean.rfind('/');
    if (slash == std::string_view::npos || slash < 8) {
        resolved = std::string(clean) + "/" + std::string(rawUrl);
    } else {
        resolved = std::string(clean.substr(0, slash + 1)) + std::string(rawUrl);
    }
    return true;
}

std::vector<uint8_t> BuildSigningMessage(const json& docWithoutSig)
{
    const std::string canonical = docWithoutSig.dump();
    std::vector<uint8_t> msg;
    msg.reserve(kSigningPrefix.size() + canonical.size());
    msg.insert(msg.end(), kSigningPrefix.begin(), kSigningPrefix.end());
    msg.insert(msg.end(), canonical.begin(), canonical.end());
    return msg;
}

std::string FormatVersion(const SemVer& v)
{
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

bool ParseEntryJson(
        const json& obj, std::string_view catalogUrl, bool allowLocal, bool urlAlreadyResolved, CatalogEntry& out, std::string& error)
{
    if (!obj.is_object()) {
        error = "catalog mod entry must be a JSON object";
        return false;
    }
    const auto reqString = [&](const char* key, std::string& dst) -> bool {
        const auto it = obj.find(key);
        if (it == obj.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
            error = std::string("mod entry \"") + key + "\" is missing or empty";
            return false;
        }
        dst = it->get<std::string>();
        return true;
    };

    if (!reqString("id", out.id)) return false;
    if (!pack_path::PackDirectory(out.id)) {
        error = "invalid mod id \"" + out.id + "\"";
        return false;
    }
    if (!reqString("title", out.title) || !reqString("mod_version", out.modVersion) || !reqString("license", out.license)) return false;

    // pack / packs (same contract as mod.json)
    const auto packIt = obj.find("pack");
    const auto packsIt = obj.find("packs");
    if (packIt != obj.end() && packsIt != obj.end()) {
        error = "mod \"" + out.id + "\" cannot set both \"pack\" and \"packs\"";
        return false;
    }
    if (packIt != obj.end()) {
        if (!packIt->is_string() || !pack_path::PackDirectory(packIt->get_ref<const std::string&>())) {
            error = "mod \"" + out.id + "\" has invalid \"pack\"";
            return false;
        }
        out.packs.push_back(packIt->get<std::string>());
    } else if (packsIt != obj.end()) {
        if (!packsIt->is_array() || packsIt->empty()) {
            error = "mod \"" + out.id + "\" \"packs\" must be a non-empty array";
            return false;
        }
        for (const json& p : *packsIt) {
            if (!p.is_string() || !pack_path::PackDirectory(p.get_ref<const std::string&>())) {
                error = "mod \"" + out.id + "\" has invalid entry in \"packs\"";
                return false;
            }
            out.packs.push_back(p.get<std::string>());
        }
    } else {
        error = "mod \"" + out.id + "\" is missing \"pack\" or \"packs\"";
        return false;
    }

    if (const auto scopeIt = obj.find("scope"); scopeIt != obj.end()) {
        if (!scopeIt->is_string()) {
            error = "mod \"" + out.id + "\" \"scope\" must be a string";
            return false;
        }
        const std::string& s = scopeIt->get_ref<const std::string&>();
        if (s == "world")
            out.scope = mod_pack::Scope::World;
        else if (s == "presentation")
            out.scope = mod_pack::Scope::Presentation;
        else {
            error = "mod \"" + out.id + "\" has unknown scope \"" + s + "\"";
            return false;
        }
    }

    if (const auto apiIt = obj.find("api"); apiIt != obj.end()) {
        if (!apiIt->is_number_integer() || apiIt->get<int>() < 0) {
            error = "mod \"" + out.id + "\" \"api\" must be a non-negative integer";
            return false;
        }
        out.api = apiIt->get<int>();
    }

    if (const auto minIt = obj.find("engine_min"); minIt != obj.end()) {
        SemVer v;
        if (!minIt->is_string() || !ParseVersion(minIt->get_ref<const std::string&>(), v)) {
            error = "mod \"" + out.id + "\" has invalid \"engine_min\"";
            return false;
        }
        out.engineMin = v;
    }
    if (const auto maxIt = obj.find("engine_max"); maxIt != obj.end()) {
        SemVer v;
        if (!maxIt->is_string() || !ParseVersion(maxIt->get_ref<const std::string&>(), v)) {
            error = "mod \"" + out.id + "\" has invalid \"engine_max\"";
            return false;
        }
        out.engineMax = v;
    }
    if (out.engineMin && out.engineMax && CompareVersion(*out.engineMin, *out.engineMax) > 0) {
        error = "mod \"" + out.id + "\" has engine_min > engine_max";
        return false;
    }

    if (const auto reqIt = obj.find("requires"); reqIt != obj.end()) {
        if (!reqIt->is_array()) {
            error = "mod \"" + out.id + "\" \"requires\" must be an array";
            return false;
        }
        std::set<std::string> seen;
        for (const json& r : *reqIt) {
            if (!r.is_string() || !pack_path::PackDirectory(r.get_ref<const std::string&>())) {
                error = "mod \"" + out.id + "\" has invalid id in \"requires\"";
                return false;
            }
            const std::string& dep = r.get_ref<const std::string&>();
            if (dep == out.id) {
                error = "mod \"" + out.id + "\" cannot require itself";
                return false;
            }
            if (seen.insert(dep).second) out.requiresMods.push_back(dep);
        }
    }

    std::string rawUrl;
    if (!reqString("url", rawUrl)) return false;
    if (urlAlreadyResolved) {
        if (allowLocal ? (!IsFileUrl(rawUrl) && !IsHttpsUrl(rawUrl)) : !IsHttpsUrl(rawUrl)) {
            error = "mod \"" + out.id + "\" has invalid stored url";
            return false;
        }
        out.url = std::move(rawUrl);
    } else if (!ResolveModUrl(catalogUrl, allowLocal, rawUrl, out.url, error)) {
        return false;
    }

    if (!reqString("sha256", out.sha256) || !IsLowerHex(out.sha256, 64)) {
        error = "mod \"" + out.id + "\" \"sha256\" must be 64 lowercase hex characters";
        return false;
    }

    if (const auto chIt = obj.find("content_hash"); chIt != obj.end()) {
        if (!chIt->is_string() || !IsLowerHex(chIt->get_ref<const std::string&>(), 64)) {
            error = "mod \"" + out.id + "\" \"content_hash\" must be 64 lowercase hex characters";
            return false;
        }
        out.contentHash = chIt->get<std::string>();
    }

    const auto sizeIt = obj.find("size");
    if (sizeIt == obj.end() || !sizeIt->is_number_unsigned()) {
        error = "mod \"" + out.id + "\" \"size\" is missing or not a positive integer";
        return false;
    }
    out.size = sizeIt->get<uint64_t>();
    if (out.size == 0 || out.size > kMaxModZipBytes) {
        error = "mod \"" + out.id + "\" \"size\" must be between 1 and 64 MiB";
        return false;
    }

    return true;
}

json SerializeEntry(const CatalogEntry& e)
{
    json obj = json::object();
    obj["id"] = e.id;
    obj["title"] = e.title;
    obj["mod_version"] = e.modVersion;
    obj["packs"] = e.packs;
    obj["scope"] = e.scope == mod_pack::Scope::Presentation ? "presentation" : "world";
    if (e.api > 0) obj["api"] = e.api;
    if (e.engineMin) obj["engine_min"] = FormatVersion(*e.engineMin);
    if (e.engineMax) obj["engine_max"] = FormatVersion(*e.engineMax);
    if (!e.requiresMods.empty()) obj["requires"] = e.requiresMods;
    obj["url"] = e.url;
    obj["sha256"] = e.sha256;
    if (!e.contentHash.empty()) obj["content_hash"] = e.contentHash;
    obj["size"] = e.size;
    obj["license"] = e.license;
    return obj;
}

} // namespace

bool ParseVersion(std::string_view text, SemVer& out)
{
    int parts[3] = {0, 0, 0};
    size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const size_t dot = text.find('.', start);
        if ((i < 2 && dot == std::string_view::npos) || (i == 2 && dot != std::string_view::npos)) return false;
        const std::string_view seg = text.substr(start, dot == std::string_view::npos ? dot : dot - start);
        if (seg.empty() || (seg.size() > 1 && seg[0] == '0')) return false;
        int val = 0;
        const auto res = std::from_chars(seg.data(), seg.data() + seg.size(), val);
        if (res.ec != std::errc{} || res.ptr != seg.data() + seg.size() || val < 0) return false;
        parts[i] = val;
        start = dot + 1;
    }
    out = {parts[0], parts[1], parts[2]};
    return true;
}

int CompareVersion(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

bool InVersionRange(const SemVer& engine, const std::optional<SemVer>& minVersion, const std::optional<SemVer>& maxVersion)
{
    if (minVersion && CompareVersion(engine, *minVersion) < 0) return false;
    if (maxVersion && CompareVersion(engine, *maxVersion) > 0) return false;
    return true;
}

bool IsValidCatalogUrl(std::string_view url, bool allowLocal, std::string& error)
{
    if (IsHttpsUrl(url)) return true;
    if (allowLocal && IsFileUrl(url)) return true;
    error = "catalog address must start with https://";
    return false;
}

bool ParseCatalog(std::string_view text, std::string_view catalogUrl, bool allowLocal, Catalog& out, std::string& error)
{
    out = {};
    if (!IsValidCatalogUrl(catalogUrl, allowLocal, error)) return false;
    if (text.size() > kMaxCatalogBytes) {
        error = "catalog exceeds 4 MiB";
        return false;
    }
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        error = "catalog is not a valid JSON object";
        return false;
    }
    if (!doc.contains("format") || !doc["format"].is_string() || doc["format"].get_ref<const std::string&>() != kCatalogFormat) {
        error = "catalog \"format\" must be \"obl.mod_catalog\"";
        return false;
    }
    if (!doc.contains("version") || !doc["version"].is_number_integer() || doc["version"].get<int>() != kCatalogVersion) {
        error = "unsupported catalog \"version\"";
        return false;
    }
    if (!doc.contains("title") || !doc["title"].is_string() || doc["title"].get_ref<const std::string&>().empty()) {
        error = "catalog \"title\" is missing or empty";
        return false;
    }
    out.title = doc["title"].get<std::string>();

    if (!doc.contains("public_key") || !doc["public_key"].is_string()) {
        error = "catalog \"public_key\" is missing";
        return false;
    }
    out.publicKeyHex = doc["public_key"].get<std::string>();
    if (!HexToBytes(out.publicKeyHex, out.publicKey)) {
        error = "catalog \"public_key\" must be 64 lowercase hex characters";
        return false;
    }

    if (!doc.contains("signature") || !doc["signature"].is_string()) {
        error = "catalog \"signature\" is missing";
        return false;
    }
    ed25519::Signature signature{};
    if (!HexToBytes(doc["signature"].get_ref<const std::string&>(), signature)) {
        error = "catalog \"signature\" must be 128 lowercase hex characters";
        return false;
    }

    // Verify Ed25519 signature over canonical JSON without "signature".
    json unsignedDoc = doc;
    unsignedDoc.erase("signature");
    const std::vector<uint8_t> message = BuildSigningMessage(unsignedDoc);
    if (!ed25519::Verify(out.publicKey, message, signature)) {
        error = "catalog signature verification failed";
        return false;
    }

    if (!doc.contains("mods") || !doc["mods"].is_array()) {
        error = "catalog \"mods\" must be an array";
        return false;
    }
    if (doc["mods"].size() > kMaxCatalogEntries) {
        error = "catalog exceeds the maximum number of mods";
        return false;
    }

    std::set<std::string> seenIds;
    out.mods.reserve(doc["mods"].size());
    for (const json& item : doc["mods"]) {
        CatalogEntry entry;
        if (!ParseEntryJson(item, catalogUrl, allowLocal, false, entry, error)) return false;
        if (!seenIds.insert(entry.id).second) {
            error = "catalog contains duplicate mod id \"" + entry.id + "\"";
            return false;
        }
        out.mods.push_back(std::move(entry));
    }
    return true;
}

bool SignCatalogJson(std::string_view unsignedJson, const ed25519::Seed& seed, std::string& signedOut, std::string& error)
{
    json doc = json::parse(unsignedJson, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        error = "input is not a valid JSON object";
        return false;
    }
    doc.erase("signature");
    const ed25519::PublicKey pk = ed25519::PublicKeyFromSeed(seed);
    doc["public_key"] = BytesToHex(pk);
    const std::vector<uint8_t> message = BuildSigningMessage(doc);
    const ed25519::Signature sig = ed25519::Sign(seed, message);
    doc["signature"] = BytesToHex(sig);
    signedOut = doc.dump(2);
    return true;
}

bool IsCatalogModCompatible(const CatalogEntry& entry, const SemVer& engineVersion, std::string_view packId, int presentationApi)
{
    if (!InVersionRange(engineVersion, entry.engineMin, entry.engineMax)) return false;
    if (entry.api > presentationApi) return false;
    return std::find(entry.packs.begin(), entry.packs.end(), packId) != entry.packs.end();
}

std::vector<CatalogEntry> FilterCompatibleMods(
        std::span<const CatalogEntry> mods, const SemVer& engineVersion, std::string_view packId, int presentationApi)
{
    std::vector<CatalogEntry> out;
    for (const CatalogEntry& entry : mods) {
        if (IsCatalogModCompatible(entry, engineVersion, packId, presentationApi)) out.push_back(entry);
    }
    return out;
}

bool ResolveInstallOrder(std::string_view targetId, std::span<const CatalogEntry> available, std::span<const std::string> installedIds,
        bool includeTargetIfInstalled, std::vector<CatalogEntry>& orderOut, std::string& error)
{
    orderOut.clear();
    std::map<std::string_view, const CatalogEntry*> byId;
    for (const CatalogEntry& e : available) byId.try_emplace(e.id, &e);

    std::set<std::string_view> installed(installedIds.begin(), installedIds.end());
    enum class Mark { None, Visiting, Done };
    std::map<std::string_view, Mark> state;

    auto visit = [&](auto&& self, std::string_view id, bool isTarget) -> bool {
        if (!isTarget && installed.count(id)) return true;
        if (isTarget && !includeTargetIfInstalled && installed.count(id)) return true;
        const auto markIt = state.find(id);
        if (markIt != state.end()) {
            if (markIt->second == Mark::Done) return true;
            if (markIt->second == Mark::Visiting) {
                error = "circular mod dependency involving \"" + std::string(id) + "\"";
                return false;
            }
        }
        const auto entryIt = byId.find(id);
        if (entryIt == byId.end()) {
            error = "required mod \"" + std::string(id) + "\" is not available in any added catalog";
            return false;
        }
        state[id] = Mark::Visiting;
        for (const std::string& dep : entryIt->second->requiresMods) {
            if (!self(self, dep, false)) return false;
        }
        state[id] = Mark::Done;
        orderOut.push_back(*entryIt->second);
        return true;
    };

    return visit(visit, targetId, true);
}

bool VerifyCatalogDownload(std::span<const uint8_t> bytes, const CatalogEntry& entry, std::string& error)
{
    if (bytes.size() != entry.size) {
        error = "downloaded size (" + std::to_string(bytes.size()) + ") does not match catalog size (" + std::to_string(entry.size) + ")";
        return false;
    }
    const std::string actual = sha256::Hex(bytes);
    if (actual != entry.sha256) {
        error = "downloaded SHA-256 does not match catalog";
        return false;
    }
    return true;
}

bool CatalogStore::Load(const fs::path& gameDir, std::string& error)
{
    m_catalogs.clear();
    const fs::path path = gameDir / kCatalogStoreFile;
    std::error_code ec;
    if (!fs::exists(path, ec)) return true; // no catalogs added yet
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path.generic_string();
        return false;
    }
    const json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string()) != kCatalogStoreFormat ||
            doc.value("version", 0) != kCatalogStoreVersion || !doc.contains("catalogs") || !doc["catalogs"].is_array()) {
        error = "invalid " + std::string(kCatalogStoreFile);
        return false;
    }
    for (const json& item : doc["catalogs"]) {
        if (!item.is_object()) continue;
        StoredCatalog stored;
        stored.url = item.value("url", std::string());
        stored.title = item.value("title", std::string());
        stored.pinnedPublicKeyHex = item.value("public_key", std::string());
        stored.allowLocal = item.value("allow_local", false);
        std::string urlErr;
        if (!IsValidCatalogUrl(stored.url, stored.allowLocal, urlErr) || !IsLowerHex(stored.pinnedPublicKeyHex, 64)) continue;
        if (item.contains("mods") && item["mods"].is_array()) {
            for (const json& m : item["mods"]) {
                CatalogEntry entry;
                std::string entryErr;
                if (ParseEntryJson(m, stored.url, stored.allowLocal, true, entry, entryErr)) stored.mods.push_back(std::move(entry));
            }
        }
        m_catalogs.push_back(std::move(stored));
    }
    return true;
}

bool CatalogStore::Save(const fs::path& gameDir, std::string& error) const
{
    json doc = json::object();
    doc["format"] = kCatalogStoreFormat;
    doc["version"] = kCatalogStoreVersion;
    json arr = json::array();
    for (const StoredCatalog& c : m_catalogs) {
        json item = json::object();
        item["url"] = c.url;
        item["title"] = c.title;
        item["public_key"] = c.pinnedPublicKeyHex;
        if (c.allowLocal) item["allow_local"] = true;
        json mods = json::array();
        for (const CatalogEntry& m : c.mods) mods.push_back(SerializeEntry(m));
        item["mods"] = std::move(mods);
        arr.push_back(std::move(item));
    }
    doc["catalogs"] = std::move(arr);

    const fs::path path = gameDir / kCatalogStoreFile;
    std::ofstream out(path, std::ios::binary);
    out << doc.dump(2) << '\n';
    out.close();
    if (!out) {
        error = "cannot write " + path.generic_string();
        return false;
    }
    return true;
}

CatalogStore::UpdateStatus CatalogStore::AddOrUpdate(
        std::string_view url, const Catalog& catalog, bool allowLocal, bool trustNewKey, std::string& error)
{
    if (!IsValidCatalogUrl(url, allowLocal, error)) return UpdateStatus::Failed;
    for (StoredCatalog& existing : m_catalogs) {
        if (existing.url == url) {
            if (existing.pinnedPublicKeyHex != catalog.publicKeyHex && !trustNewKey) {
                error = "catalog signing key changed";
                return UpdateStatus::KeyChanged;
            }
            existing.title = catalog.title;
            existing.pinnedPublicKeyHex = catalog.publicKeyHex;
            existing.mods = catalog.mods;
            existing.allowLocal = allowLocal;
            return UpdateStatus::Updated;
        }
    }
    StoredCatalog added;
    added.url = std::string(url);
    added.title = catalog.title;
    added.pinnedPublicKeyHex = catalog.publicKeyHex;
    added.mods = catalog.mods;
    added.allowLocal = allowLocal;
    m_catalogs.push_back(std::move(added));
    return UpdateStatus::Added;
}

bool CatalogStore::Remove(std::string_view url)
{
    const auto it = std::find_if(m_catalogs.begin(), m_catalogs.end(), [&](const StoredCatalog& c) { return c.url == url; });
    if (it == m_catalogs.end()) return false;
    m_catalogs.erase(it);
    return true;
}

const StoredCatalog* CatalogStore::Find(std::string_view url) const
{
    const auto it = std::find_if(m_catalogs.begin(), m_catalogs.end(), [&](const StoredCatalog& c) { return c.url == url; });
    return it == m_catalogs.end() ? nullptr : &*it;
}

std::optional<CatalogModMatch> CatalogStore::FindModByIdAndHash(std::string_view id, std::string_view contentHash) const
{
    if (id.empty() || contentHash.empty()) return std::nullopt;
    for (const StoredCatalog& c : m_catalogs) {
        for (const CatalogEntry& e : c.mods) {
            if (e.id == id && !e.contentHash.empty() && e.contentHash == contentHash) {
                return CatalogModMatch{c.title, c.url, e};
            }
        }
    }
    return std::nullopt;
}

} // namespace obl::mods
