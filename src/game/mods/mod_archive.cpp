#include "game/mods/mod_archive.h"

#include "core/byte_reader.h"
#include "core/json_schema.h"
#include "core/log.h"
#include "core/pack_path.h"
#include "formats/data_patch.h"
#include "formats/mod_pack.h"
#include "formats/schemas.h"
#include "formats/asset_recipe.h"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <utility>

namespace obl::mods {

namespace {

using Bytes = std::span<const uint8_t>;
namespace fs = std::filesystem;

constexpr uint32_t kEndSignature = 0x06054b50;
constexpr uint32_t kCentralSignature = 0x02014b50;
constexpr uint32_t kLocalSignature = 0x04034b50;
constexpr uint32_t kDescriptorSignature = 0x08074b50;
constexpr size_t kEndSize = 22;
constexpr size_t kCentralSize = 46;
constexpr size_t kLocalSize = 30;
constexpr uint16_t kDescriptor = 8;
constexpr uint8_t kDirectory = 0x10;

struct Entry {
    std::string_view name;
    uint32_t offset;
    uint32_t crc;
    uint32_t compressed;
    uint32_t expanded;
    uint16_t flags;
    uint16_t method;
    bool directory;
};

bool Contains(Bytes bytes, size_t offset, size_t size)
{
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::string_view Name(Bytes bytes, size_t offset, size_t size)
{
    return {std::bit_cast<const char*>(bytes.data() + offset), size};
}

std::optional<size_t> FindCentralEnd(Bytes zip)
{
    if (zip.size() < kEndSize) return std::nullopt;
    const size_t last = zip.size() - kEndSize;
    const size_t first = last - std::min<size_t>(last, 65535);
    for (size_t at = last + 1; at-- > first;) {
        if (obl::Read<uint32_t>(zip, at) == kEndSignature && obl::Read<uint16_t>(zip, at + 20) == last - at) return at;
    }
    return std::nullopt;
}

bool CheckExtra(Bytes extra)
{
    for (size_t at = 0; at < extra.size();) {
        if (!Contains(extra, at, 4)) return false;
        const auto tag = obl::Read<uint16_t>(extra, at);
        const auto length = obl::Read<uint16_t>(extra, at + 2);
        at += 4;
        // PKWARE/ASi Unix extras may carry link targets. ZIP64 is outside this bounded format.
        if (tag == 0x0001 || tag == 0x000d || tag == 0x756e || !Contains(extra, at, length)) return false;
        at += length;
    }
    return true;
}

std::optional<Entry> ReadCentralEntry(Bytes central, size_t& at)
{
    if (!Contains(central, at, kCentralSize) || obl::Read<uint32_t>(central, at) != kCentralSignature) return std::nullopt;
    const auto nameLength = obl::Read<uint16_t>(central, at + 28);
    const auto extraLength = obl::Read<uint16_t>(central, at + 30);
    const auto commentLength = obl::Read<uint16_t>(central, at + 32);
    const size_t size = kCentralSize + nameLength + extraLength + commentLength;
    if (!nameLength || !Contains(central, at, size) || obl::Read<uint16_t>(central, at + 34) != 0) return std::nullopt;
    if (!CheckExtra(central.subspan(at + kCentralSize + nameLength, extraLength))) return std::nullopt;
    const auto name = Name(central, at + kCentralSize, nameLength);
    const auto attributes = obl::Read<uint32_t>(central, at + 38);
    const auto type = (attributes >> 16) & 0170000;
    const bool directory = name.ends_with('/');
    // Unix file types and the DOS directory bit must describe ordinary files or directories.
    if ((type && type != (directory ? 0040000u : 0100000u)) || (!directory && (attributes & kDirectory)) || (attributes & 0x400))
        return std::nullopt;
    Entry entry{name, obl::Read<uint32_t>(central, at + 42), obl::Read<uint32_t>(central, at + 16), obl::Read<uint32_t>(central, at + 20),
            obl::Read<uint32_t>(central, at + 24), obl::Read<uint16_t>(central, at + 8), obl::Read<uint16_t>(central, at + 10), directory};
    at += size;
    if ((entry.flags & ~0x080e) || (entry.method != 0 && entry.method != 8) || (directory && entry.expanded != 0)) return std::nullopt;
    return entry;
}

bool MatchesDescriptor(Bytes bytes, size_t at, const Entry& entry)
{
    return Contains(bytes, at, 12) && obl::Read<uint32_t>(bytes, at) == entry.crc &&
            obl::Read<uint32_t>(bytes, at + 4) == entry.compressed && obl::Read<uint32_t>(bytes, at + 8) == entry.expanded;
}

std::optional<Bytes> ReadPayload(Bytes locals, const Entry& entry, size_t& next)
{
    const size_t at = entry.offset;
    if (at != next || !Contains(locals, at, kLocalSize) || obl::Read<uint32_t>(locals, at) != kLocalSignature ||
            obl::Read<uint16_t>(locals, at + 6) != entry.flags || obl::Read<uint16_t>(locals, at + 8) != entry.method)
        return std::nullopt;
    const auto nameLength = obl::Read<uint16_t>(locals, at + 26);
    const auto extraLength = obl::Read<uint16_t>(locals, at + 28);
    const size_t start = at + kLocalSize + nameLength + extraLength;
    if (!Contains(locals, start, entry.compressed) || Name(locals, at + kLocalSize, nameLength) != entry.name) return std::nullopt;
    if (!CheckExtra(locals.subspan(at + kLocalSize + nameLength, extraLength))) return std::nullopt;
    next = start + entry.compressed;
    if (entry.flags & kDescriptor) {
        if (Contains(locals, next, 16) && obl::Read<uint32_t>(locals, next) == kDescriptorSignature &&
                MatchesDescriptor(locals, next + 4, entry))
            next += 16;
        else if (MatchesDescriptor(locals, next, entry))
            next += 12;
        else
            return std::nullopt;
    } else if (!MatchesDescriptor(locals, at + 14, entry)) {
        return std::nullopt;
    }
    return locals.subspan(start, entry.compressed);
}

bool Decode(Bytes payload, const Entry& entry, std::string& out)
{
    if (entry.method == 0) {
        if (entry.compressed != entry.expanded) return false;
        out.assign(Name(payload, 0, payload.size()));
    } else {
        // The spare byte lets inflate finish a stream with zero output and detects output beyond the declared limit.
        out.resize(entry.expanded + 1);
        z_stream stream{};
        stream.next_in = const_cast<uint8_t*>(payload.data());
        stream.avail_in = entry.compressed;
        stream.next_out = std::bit_cast<uint8_t*>(out.data());
        stream.avail_out = entry.expanded + 1;
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result != Z_STREAM_END || stream.total_out != entry.expanded || stream.total_in != entry.compressed) return false;
        out.resize(entry.expanded);
    }
    return crc32(0, std::bit_cast<const uint8_t*>(out.data()), entry.expanded) == entry.crc;
}

std::string Lower(std::string_view name)
{
    std::string lower(name);
    for (char& c : lower)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return lower;
}

bool IsPortableName(std::string_view name)
{
    if (name.empty() || name.back() == '.' || name.front() == ' ' || name.back() == ' ') return false;
    for (unsigned char c : name)
        if (c < 32 || c >= 127 || std::string_view(R"(<>:"\|?*)").find(static_cast<char>(c)) != std::string_view::npos) return false;
    const std::string stem = Lower(name.substr(0, name.find('.')));
    const auto device = std::string_view(stem).substr(0, stem.find_last_not_of(' ') + 1);
    return device != "con" && device != "prn" && device != "aux" && device != "nul" &&
            !(device.size() == 4 && (device.starts_with("com") || device.starts_with("lpt")) && device.back() >= '0' &&
                    device.back() <= '9');
}

bool IsNativeFile(const std::string& name)
{
    const auto extension = fs::path(name).extension().string();
    return extension == ".exe" || extension == ".dll" || extension == ".so" || extension == ".dylib" || extension == ".com" ||
            extension == ".msi";
}

struct PathEntry {
    std::string spelling;
    bool directory;
    bool explicitEntry;
};

bool CheckPath(std::string_view name, bool directory, std::string& root, std::map<std::string, PathEntry>& paths)
{
    if (name.empty() || name.size() > 240 || std::count(name.begin(), name.end(), '/') >= 16) return false;
    const size_t slash = name.find('/');
    const auto id = name.substr(0, slash);
    if (!pack_path::PackDirectory(id) || (!directory && slash == std::string_view::npos)) return false;
    if (root.empty()) root = id;
    if (id != root) return false;

    size_t start = 0;
    for (;;) {
        const size_t end = name.find('/', start);
        if (!IsPortableName(name.substr(start, end == std::string_view::npos ? end : end - start))) return false;
        const bool leaf = end == std::string_view::npos;
        const auto prefix = name.substr(0, end);
        std::string lower = Lower(prefix);
        if (leaf && !directory && IsNativeFile(lower)) return false;
        const auto [it, inserted] = paths.try_emplace(std::move(lower), PathEntry{std::string(prefix), !leaf || directory, leaf});
        if (!inserted) {
            auto& existing = it->second;
            if (existing.spelling != prefix || !existing.directory || (leaf && (!directory || existing.explicitEntry))) return false;
            if (leaf) existing.explicitEntry = true;
        }
        if (leaf) return true;
        start = end + 1;
    }
}

// The mods root and target must be plain directories. Do not follow an installed link when replacing or removing it.
fs::file_status Status(const fs::path& path, std::error_code& ec)
{
    const auto status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory) ec.clear();
    return status;
}

bool CheckDirectory(const fs::path& path, std::string& error)
{
    std::error_code ec;
    const auto status = Status(path, ec);
    if (!ec && (status.type() == fs::file_type::not_found || fs::is_directory(status))) return true;
    error = "cannot use directory " + path.generic_string();
    return false;
}

bool CheckIdCollision(const fs::path& destination, std::string& error)
{
    std::error_code ec;
    const auto root = Status(destination.parent_path(), ec);
    if (!ec && root.type() == fs::file_type::not_found) return true;
    const std::string id = destination.filename().string();
    for (fs::directory_iterator it(destination.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().u8string();
        const std::string_view bytes(std::bit_cast<const char*>(name.data()), name.size());
        if (bytes != id && Lower(bytes) == id) {
            error = "a folder with the same id already exists with different capitalization";
            return false;
        }
    }
    if (ec) error = "cannot check installed mods: " + ec.message();
    return !ec;
}

// Hold a per-mod lock and keep staging on the same filesystem as the destination. Until accepted, restore the previous
// directory if publishing the archive or its cache fails.
class DirectoryChange {
public:
    explicit DirectoryChange(fs::path destination) : m_destination(std::move(destination)) {}

    bool Prepare(std::string& error)
    {
        std::error_code ec;
        fs::create_directory(m_destination.parent_path(), ec);
        const auto stage = m_destination.parent_path() / (".mod-change-" + m_destination.filename().string());
        if (!ec && fs::create_directory(stage, ec)) {
            m_stage = stage;
            return true;
        }
        error = "cannot stage changes to " + m_destination.generic_string();
        return false;
    }

    ~DirectoryChange()
    {
        if (m_stage.empty()) return;
        std::error_code ec;
        if (!m_accepted && m_installed) fs::rename(m_destination, Incoming(), ec);
        if (!m_accepted && m_saved && !ec) fs::rename(m_stage / "previous", m_destination, ec);
        if (ec) {
            OBL_LOG("mod archive: could not restore %s; files remain in %s", m_destination.string().c_str(), m_stage.string().c_str());
            return;
        }
        fs::remove_all(m_stage, ec);
    }

    fs::path Incoming() const { return m_stage / "incoming"; }

    bool Publish(bool hadDirectory, bool hasIncoming, std::string& error)
    {
        if (!CheckDirectory(m_destination, error)) return false;
        std::error_code ec;
        const bool exists = fs::is_directory(Status(m_destination, ec));
        if (ec || exists != hadDirectory) {
            error = "directory changed while preparing " + m_destination.generic_string();
            return false;
        }
        if (exists) {
            fs::rename(m_destination, m_stage / "previous", ec);
            m_saved = !ec;
        }
        if (!ec && hasIncoming) {
            fs::rename(Incoming(), m_destination, ec);
            m_installed = !ec;
        }
        if (!ec) return true;
        error = "cannot update " + m_destination.generic_string() + ": " + ec.message();
        return false;
    }

    void Accept() { m_accepted = true; }

private:
    fs::path m_destination;
    fs::path m_stage;
    bool m_saved = false;
    bool m_installed = false;
    bool m_accepted = false;
};

bool WriteFile(const fs::path& path, std::string_view bytes, std::string& error)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "cannot create the mod's folders: " + ec.message();
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (out) return true;
    error = "cannot write " + path.filename().string();
    return false;
}

// Recipe outputs are generated before publishing anything. Apply the package's expanded size budget to them as well.
class RecipeCache final : public convert::PackSink {
public:
    bool Write(std::string_view path, Bytes bytes, std::string& error) override
    {
        const std::string name = "cache/" + std::string(path);
        if (!CheckPath(name, false, m_root, m_paths)) {
            error = "unsafe or conflicting recipe output path";
            return false;
        }
        if (bytes.size() > kMaxModExpandedBytes - m_bytes) {
            error = "recipe outputs exceed 128 MiB";
            return false;
        }
        m_bytes += bytes.size();
        return m_files.Write(path, bytes, error);
    }

    std::optional<std::vector<uint8_t>> Read(std::string_view path) const override { return m_files.Read(path); }
    bool Remove(std::string_view path) override { return m_files.Remove(path); }
    std::vector<std::string> Files() const override { return m_files.Files(); }
    const auto& Contents() const { return m_files.files(); }

private:
    size_t m_bytes = 0;
    std::string m_root;
    std::map<std::string, PathEntry> m_paths;
    convert::MemorySink m_files;
};

bool CheckModDirectories(const fs::path& destination, std::string& error)
{
    return CheckDirectory(destination.parent_path(), error) && CheckIdCollision(destination, error) && CheckDirectory(destination, error);
}

} // namespace

bool ModArchive::Read(std::span<const uint8_t> zip, std::string_view packId, std::string& error)
{
    m_id.clear();
    m_packId.clear();
    m_files.clear();
    const auto fail = [&](std::string reason) {
        error = std::move(reason);
        return false;
    };
    if (zip.size() > kMaxModZipBytes) return fail("ZIP exceeds the 64 MiB archive limit");
    const auto end = FindCentralEnd(zip);
    if (!end) return fail("not a ZIP archive");
    const auto count = obl::Read<uint16_t>(zip, *end + 10);
    const size_t length = obl::Read<uint32_t>(zip, *end + 12);
    const size_t offset = obl::Read<uint32_t>(zip, *end + 16);
    if (obl::Read<uint32_t>(zip, *end + 4) != 0 || obl::Read<uint16_t>(zip, *end + 8) != count ||
            !Contains(zip.first(*end), offset, length) || offset + length != *end)
        return fail("invalid or split ZIP directory");
    if (count > kMaxModZipEntries) return fail("ZIP exceeds the entry limit");

    const Bytes central = zip.subspan(offset, length);
    size_t at = 0;
    size_t expanded = 0;
    std::string id;
    std::map<std::string, PathEntry> paths;
    std::vector<Entry> entries;
    for (size_t i = 0; i < count; ++i) {
        const auto entry = ReadCentralEntry(central, at);
        if (!entry) return fail("unsupported ZIP entry (only regular files and directories, stored or deflated)");
        const auto name = entry->directory ? entry->name.substr(0, entry->name.size() - 1) : entry->name;
        if (!CheckPath(name, entry->directory, id, paths)) return fail("unsafe, duplicate or misplaced ZIP path");
        if (entry->expanded > kMaxModFileBytes || entry->expanded > kMaxModExpandedBytes - expanded)
            return fail("ZIP exceeds the expanded size limit");
        expanded += entry->expanded;
        entries.push_back(*entry);
    }
    if (at != central.size()) return fail("invalid ZIP directory length");

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.offset < b.offset; });
    const Bytes locals = zip.first(offset);
    size_t next = 0;
    bool hasScripts = false;
    std::vector<File> files;
    for (const Entry& entry : entries) {
        const auto payload = ReadPayload(locals, entry, next);
        std::string bytes;
        if (!payload || !Decode(*payload, entry, bytes)) return fail("ZIP contains corrupt file data");
        if (entry.directory) continue;
        std::string name(entry.name.substr(id.size() + 1));
        constexpr std::string_view kScriptsPrefix = "scripts/presentation/";
        hasScripts |= name.starts_with(kScriptsPrefix) && name.find('/', kScriptsPrefix.size()) == std::string::npos &&
                fs::path(name).extension() == ".lua";
        files.push_back({std::move(name), std::move(bytes)});
    }
    if (next != locals.size()) return fail("ZIP contains unlisted file data");

    const auto manifest = std::find_if(files.begin(), files.end(), [](const File& f) { return f.name == "mod.json"; });
    if (manifest == files.end()) return fail("ZIP must contain one mod folder with a mod.json");
    if (manifest->bytes.size() > 256 * 1024) return fail("mod.json exceeds 256 KiB");
    mod_pack::Manifest parsed;
    if (!mod_pack::ParseManifest(manifest->bytes, parsed, error)) return false;
    if (parsed.id != id) return fail("mod.json id does not match the directory name");
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.name < b.name; });
    std::string_view refused;
    if (parsed.scope == mod_pack::Scope::Presentation) {
        for (const auto& file : files) {
            const std::string format = file.name.starts_with("gui/") && file.name.ends_with(".patch.json")
                    ? data_patch::PatchFormatText(file.bytes)
                    : std::string();
            if (!mod_pack::PresentationMayShip(file.name, format, file.bytes)) {
                refused = file.name;
                break;
            }
        }
    }
    if (!mod_pack::CheckManifestContent(parsed, packId, hasScripts, refused, error)) return false;
    m_id = std::move(id);
    m_packId = packId;
    m_files = std::move(files);
    return true;
}

ModArchive::Result ModArchive::Write(const fs::path& gameDir, bool replace, std::string& error) const
{
    const auto destination = pack_path::ModDirectory(gameDir, m_id);
    if (!destination || m_files.empty()) {
        error = "no validated mod archive";
        return Result::Failed;
    }
    if (!CheckModDirectories(*destination, error)) return Result::Failed;
    std::error_code ec;
    const bool exists = fs::is_directory(Status(*destination, ec));
    if (ec) {
        error = "cannot check installed mod: " + ec.message();
        return Result::Failed;
    }
    if (exists && !replace) return Result::AlreadyInstalled;
    if (replace && !exists) {
        error = "the mod to replace is no longer installed";
        return Result::Failed;
    }

    const auto cacheDir = *pack_path::ModCacheDirectory(gameDir, m_id);
    if (!CheckModDirectories(cacheDir, error)) return Result::Failed;
    convert::MemorySink recipes;
    size_t recipeCount = 0;
    for (const File& file : m_files) {
        if (!mod_pack::IsRecipePath(file.name)) continue;
        if (++recipeCount > 64 || file.bytes.size() > 256 * 1024) {
            error = "ZIP exceeds the recipe count or size limit";
            return Result::Failed;
        }
        if (!recipes.WriteText(file.name, file.bytes, error)) return Result::Failed;
    }
    RecipeCache cache;
    convert::ModRecipesResult report;
    if (recipeCount) {
        const auto pack = convert::OpenDirectorySink(gameDir / *pack_path::PackDirectory(m_packId));
        json_schema::Registry schemas;
        if (!formats::LoadEmbeddedSchemas(schemas, error) || !convert::BuildModRecipes(*pack, recipes, cache, schemas, report, error))
            return Result::Failed;
    }
    const bool hadCache = fs::is_directory(Status(cacheDir, ec));
    if (ec) {
        error = "cannot check the mod cache: " + ec.message();
        return Result::Failed;
    }
    // An orphan cache from a removed mod must never be applied to a fresh install of the same id.
    const bool hasCache = !cache.Contents().empty();
    DirectoryChange mod(*destination);
    DirectoryChange recipeOutputs(cacheDir);
    if (!mod.Prepare(error) || ((hadCache || hasCache) && !recipeOutputs.Prepare(error))) return Result::Failed;
    for (const File& file : m_files)
        if (!WriteFile(mod.Incoming() / file.name, file.bytes, error)) return Result::Failed;
    for (const auto& [name, bytes] : cache.Contents())
        if (!WriteFile(recipeOutputs.Incoming() / name, Name(bytes, 0, bytes.size()), error)) return Result::Failed;
    if ((hadCache || hasCache) && !recipeOutputs.Publish(hadCache, hasCache, error)) return Result::Failed;
    if (!mod.Publish(exists, true, error)) return Result::Failed;
    mod.Accept();
    recipeOutputs.Accept();
    for (const auto& disabled : report.disabled) OBL_LOG("mod %s: %s", m_id.c_str(), disabled.message.c_str());
    return Result::Installed;
}

bool RemoveMod(const fs::path& gameDir, std::string_view id, std::string& error)
{
    const auto destination = pack_path::ModDirectory(gameDir, id);
    if (!destination || !IsPortableName(id)) {
        error = "invalid mod id";
        return false;
    }
    const auto cacheDir = *pack_path::ModCacheDirectory(gameDir, id);
    if (!CheckModDirectories(*destination, error) || !CheckModDirectories(cacheDir, error)) return false;
    std::error_code ec;
    const bool hadCache = fs::is_directory(Status(cacheDir, ec));
    if (ec) {
        error = "cannot check the mod cache: " + ec.message();
        return false;
    }
    DirectoryChange mod(*destination);
    DirectoryChange cache(cacheDir);
    if (!mod.Prepare(error) || (hadCache && !cache.Prepare(error))) return false;
    if (hadCache && !cache.Publish(true, false, error)) return false;
    if (!mod.Publish(true, false, error)) return false;
    mod.Accept();
    cache.Accept();
    return true;
}

} // namespace obl::mods
