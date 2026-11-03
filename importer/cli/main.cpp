// obl-convert: converts the user's copy of Beach Life into our data formats (docs/architecture/data-formats/converter/index.md).
// Sprites, palette modifiers, images, fonts, texts, GUI layouts, scripts, sounds, GUI tables, the catalog and old saves are converted
// with maps and scenario metadata; `pack` writes the pack manifest.

#include "importer/src/readers/building_list.h"
#include "importer/src/source/cabinet.h"
#include "importer/src/source/cd_source.h"
#include "importer/src/writers/catalog_writer.h"
#include "importer/src/readers/exe_tables.h"
#include "importer/src/writers/gui_layout_convert.h"
#include "importer/src/readers/gui_tables.h"
#include "importer/src/writers/guest_set_convert.h"
#include "importer/src/writers/image_convert.h"
#include "importer/src/writers/map_convert.h"
#include "importer/src/writers/scenario_convert.h"
#include "importer/src/writers/pack_manifest.h"
#include "importer/src/writers/font_convert.h"
#include "importer/src/writers/game_parameters_convert.h"
#include "importer/src/writers/gui_writer.h"
#include "importer/src/writers/media_convert.h"
#include "importer/src/writers/resource_convert.h"
#include "importer/src/writers/text_convert.h"
#include "importer/src/source/pe_file.h"
#include "importer/src/readers/save/save_body.h"
#include "importer/src/readers/save/save_stream.h"
#include "importer/src/readers/save/save_writer.h"
#include "formats/schemas.h"
#include "importer/src/writers/script_convert.h"
#include "importer/src/source/source.h"
#include "openbeach/importer/import.h"
#include "openbeach/importer/pack_sink.h"
#include "importer/src/writers/sound_convert.h"
#include "importer/src/writers/sound_script_convert.h"
#include "importer/src/writers/sprite_convert.h"

#include "core/json_schema.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

// A CD's large files are spooled beside the output directory, never inside it: a pack's manifest lists every file of the
// pack directory, so a spool file still open there would end up in pack.json.
class SpoolBeside {
public:
    explicit SpoolBeside(const std::filesystem::path& output)
        : m_directory(Beside(output)), m_spool(obl::convert::OpenDirectorySpool(m_directory))
    {}
    ~SpoolBeside()
    {
        m_spool.reset();
        std::error_code ec;
        std::filesystem::remove(m_directory, ec); // only when empty: another import may share it
    }
    SpoolBeside(const SpoolBeside&) = delete;
    SpoolBeside& operator=(const SpoolBeside&) = delete;
    obl::convert::Spool& operator*() const { return *m_spool; }
    obl::convert::Spool* get() const { return m_spool.get(); }

private:
    static std::filesystem::path Beside(const std::filesystem::path& output)
    {
        // "packs/base/" names the same directory as "packs/base".
        auto directory = std::filesystem::absolute(output).lexically_normal();
        if (!directory.has_filename()) directory = directory.parent_path();
        return directory.parent_path() / (directory.filename().string() + ".spool");
    }

    std::filesystem::path m_directory;
    std::unique_ptr<obl::convert::Spool> m_spool;
};

int Usage()
{
    std::fprintf(stderr,
            "usage: obl-convert footprints <installation>\n"
            "  lists the footprint grids of BeachLife.exe found by content pattern and BuildingList keys (key, address, size)\n"
            "       obl-convert gui-tables <BeachLife.exe> [output directory]\n"
            "  lists the tables, or writes validated gui/object_browser.json and gui/credits.json into the output directory\n"
            "  (the converted pack: <game dir>/packs/base)\n"
            "       obl-convert identify <installation directory or CD root>\n"
            "  names the EXE build and data revision with the files' hashes (pack.json keeps only an import reference)\n"
            "       obl-convert cabinet list <data1.hdr>\n"
            "       obl-convert cabinet verify <data1.hdr>\n"
            "       obl-convert cabinet extract <data1.hdr> <file group> <output directory>\n"
            "  lists, checks (size and MD5 of every stored file) or extracts the CD's InstallShield cabinets\n"
            "       obl-convert fonts <installation or CD root> <output directory> [--schemas <dir>]\n"
            "  writes Unicode font metrics and indexed PNG atlases with validated JSON\n"
            "       obl-convert images <installation or CD root> <output directory> [--schemas <dir>]\n"
            "  writes palette modifiers, screens, cursor atlas and haze PNGs with validated JSON\n"
            "       obl-convert texts <installation or CD root> <output directory> [--schemas <dir>]\n"
            "  writes text/<lang>.json for every installed text table with validated JSON\n"
            "       obl-convert sounds <installation or CD root> <output directory>\n"
            "  writes sounds/<name>.wav (a byte copy of every sample of blsnd.hdr and blsnd.spf) and sounds/sound_script.json\n"
            "       obl-convert guests <installation or CD root> <output directory> [--schemas <dir>]\n"
            "  writes guests/<set>.json for the 12 campaign and 3 sandbox guest files with validated JSON\n"
            "       obl-convert gui-layouts <installation or CD root> <output directory> [--schemas <dir>]\n"
            "  writes gui/<name>.gui for the installed GUI scripts and gui/layouts.json with validated JSON\n"
            "       obl-convert scenarios <installation or CD root> <output directory> [--schemas <dir>]\n"
            "  writes scenarios/<id>/scenario.json and scenarios/campaign.json; reports unused PIS entries\n"
            "       obl-convert maps <installation or CD root> <output directory> [--jobs <n>] [--schemas <dir>]\n"
            "  writes maps/ (terrain, heights, color, depth, water masks and the prop key lookup)\n"
            "       obl-convert scripts <installation or CD root> <output directory>\n"
            "  writes scripts/object/<name>.obs and scenarios/<id>/world.wsc, the decoded object and world scripts\n"
            "       obl-convert catalog <installation or CD root> <output directory> [--schemas <dir>]\n"
            "  writes catalog/ (index.json and one validated JSON per building, scenery, staff, vehicle, ...) from BuildingList.ble\n"
            "       obl-convert sprites <installation or CD root> <output directory> [--filter <text>] [--jobs <n>] [--schemas <dir>]\n"
            "  writes sprites/ (obl.sprite JSON, PNG palettes and sheets), every JSON checked against data/schemas/\n"
            "       obl-convert game-parameters <installation or CD root> <output directory> [--schemas <dir>]\n"
            "  writes rules/game_parameters.json from GameParameters.pis with validated JSON\n"
            "       obl-convert media <installation or CD root> <output directory>\n"
            "  writes music/<name>.mp3 (every soundtrack track) and video/intro.mpg as byte copies\n"
            "       obl-convert resources <installation or CD root> <output directory>\n"
            "  writes icon.ico and cursor_<id>.cur (the class icon and the seven cursors of BeachLife.exe's .rsrc)\n"
            "       obl-convert pack <installation directory> <pack directory> [--language <lang>] [--schemas <dir>]\n"
            "  writes <pack directory>/pack.json last: identity, import reference, default language (without --language: the\n"
            "  BeachLife.ini language when the pack has its table), hash and tier of every file\n"
            "       obl-convert import <installation or CD root> <pack directory> [--language <lang>] [--jobs <n>]\n"
            "  runs every pack command above, pack.json last (what the browser import runs)\n"
            "  --jobs <n>: worker threads for the sprite and map images (default: one per hardware thread; 1 converts serially);\n"
            "  the files, errors and progress are the same for every n\n"
            "       obl-convert save-info <save<N>.dat> [--objects]\n"
            "  checks an old save slot to its end and prints the header and record counts per type; --objects reads\n"
            "  every section and object by its emitter's field table and checks counts and object references\n"
            "       obl-convert save <save<N>.dat> <pack directory> <output.json> [--schemas <dir>]\n"
            "  writes a validated obl.save JSON with keys from <pack directory>/catalog/index.json and the identity and\n"
            "  language of <pack directory>/pack.json (run pack first); write the output outside the pack directory\n");
    return 2;
}

std::optional<obl::convert::PeFile> LoadExe(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "error: cannot read %s\n", path.string().c_str());
        return std::nullopt;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto exe = obl::convert::PeFile::Parse(std::move(bytes), error);
    if (!exe) std::fprintf(stderr, "error: %s: %s\n", path.string().c_str(), error.c_str());
    return exe;
}

// A positive thread count for --jobs; nullopt for anything else.
std::optional<int> ParseJobs(const std::string& text)
{
    if (text.empty() || text.size() > 4 || text.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
    const int jobs = std::stoi(text);
    if (jobs < 1) return std::nullopt;
    return jobs;
}

int DefaultJobs()
{
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

int Footprints(const std::filesystem::path& installation)
{
    const auto source = obl::convert::OpenDirectorySource(installation);
    std::string error;
    obl::convert::BuildingList list;
    if (!obl::convert::ReadBuildingList(*source, list, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    const auto path = obl::convert::FindFile(*source, "BeachLife.exe");
    if (!path) {
        std::fprintf(stderr, "error: BeachLife.exe not found in %s\n", installation.string().c_str());
        return 1;
    }
    auto bytes = source->ReadAll(*path);
    if (!bytes) {
        std::fprintf(stderr, "error: cannot read %s\n", source->Describe(*path).c_str());
        return 1;
    }
    auto exe = obl::convert::PeFile::Parse(std::move(*bytes), error);
    if (!exe) {
        std::fprintf(stderr, "error: %s: %s\n", source->Describe(*path).c_str(), error.c_str());
        return 1;
    }
    std::set<std::string> keys;
    for (const auto& building : list.buildings) keys.insert(building.key);
    std::vector<obl::convert::ExeFootprint> footprints;
    if (!obl::convert::FindFootprints(*exe, keys, footprints, error)) {
        std::fprintf(stderr, "error: %s: %s\n", source->Describe(*path).c_str(), error.c_str());
        return 1;
    }
    for (const auto& fp : footprints) std::printf("%-16s 0x%08x %dx%d\n", fp.building.c_str(), fp.address, fp.width, fp.height);
    std::printf("%zu footprints\n", footprints.size());
    return 0;
}

void PrintList(const char* name, const std::vector<obl::convert::GuiListEntry>& rows)
{
    std::printf("%s (%zu)\n", name, rows.size());
    for (const auto& row : rows) {
        if (row.Separator())
            std::printf("  ---\n");
        else
            std::printf("  %-28s %d\n", row.key.c_str(), row.value);
    }
}

int GuiTables(const std::filesystem::path& path, const std::filesystem::path& output = {})
{
    auto exe = LoadExe(path);
    if (!exe) return 1;
    std::string error;
    obl::convert::ExeGuiTables tables;
    if (!obl::convert::FindGuiTables(*exe, tables, error)) {
        std::fprintf(stderr, "error: %s: %s\n", path.string().c_str(), error.c_str());
        return 1;
    }
    if (!output.empty()) {
        const auto sink = obl::convert::OpenDirectorySink(output);
        if (!obl::convert::WriteGuiTables(tables, *sink, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::printf("GUI tables written to %s/gui\n", output.string().c_str());
        return 0;
    }
    PrintList("guest filters", tables.guestFilters);
    PrintList("building filters", tables.buildingFilters);
    PrintList("cell types", tables.cellTypes);
    PrintList("staff filters", tables.staffFilters);
    PrintList("hire types", tables.hireTypes);
    PrintList("events", tables.events);
    PrintList("column headings", tables.headings);
    std::printf("row buttons (%zu)\n", tables.rowButtons.size());
    for (const auto& name : tables.rowButtons) std::printf("  %s\n", name.c_str());
    std::printf("credits (%zu rows)\n", tables.credits.size());
    for (const auto& row : tables.credits)
        std::printf("  font %d line %d color %06x long %u kind %d\n", row.font, row.line, row.color, row.isLong, row.kind);
    return 0;
}

int Identify(const std::filesystem::path& root)
{
    obl::convert::Source source;
    std::string error;
    if (!obl::convert::IdentifySource(*obl::convert::OpenDirectorySource(root), source, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    std::printf("exe  %s\ndata %s\n", std::string(Name(source.exe)).c_str(), std::string(Name(source.data)).c_str());
    for (const auto& [path, hash] : source.sha256) std::printf("sha256 %s %s\n", hash.c_str(), path.c_str());
    return 0;
}

// Command-line paths are in the native narrow encoding; cabinet paths are UTF-8.
std::filesystem::path Utf8Path(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// A path from the cabinet header is used under the output directory only when it stays there.
bool SafeRelativePath(const std::string& path)
{
    const std::filesystem::path p = Utf8Path(path);
    if (p.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
    for (const auto& part : p)
        if (part == ".." || part == ".") return false;
    return true;
}

int CabinetCommand(const std::string& command, const std::vector<std::string>& args)
{
    std::string error;
    auto cabinet = obl::convert::Cabinet::Open(std::filesystem::path(args[0]), error);
    if (!cabinet) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    if (command == "list") {
        for (const auto& file : cabinet->Files())
            std::printf("%-30s %10llu %s%s\n", file.group.c_str(), static_cast<unsigned long long>(file.size), file.path.c_str(),
                    file.External() ? "  (next to the cabinet)" : "");
        std::printf("%zu files\n", cabinet->Files().size());
        return 0;
    }
    if (command == "verify") {
        int failed = 0, checked = 0;
        for (const auto& file : cabinet->Files()) {
            if (file.External()) continue;
            ++checked;
            if (!cabinet->Extract(file, [](std::span<const uint8_t>) { return true; }, error)) {
                std::fprintf(stderr, "error: %s\n", error.c_str());
                ++failed;
            }
        }
        std::printf("%d files checked, %d failed\n", checked, failed);
        return failed ? 1 : 0;
    }
    // extract <group> <out>
    const std::filesystem::path out = std::filesystem::path(args[2]);
    int extracted = 0;
    for (const auto& file : cabinet->Files()) {
        if (file.group != args[1] || file.External()) continue;
        if (!SafeRelativePath(file.path)) {
            std::fprintf(stderr, "error: unsafe path in the cabinet: %s\n", file.path.c_str());
            return 1;
        }
        const std::filesystem::path target = out / Utf8Path(file.path);
        std::filesystem::create_directories(target.parent_path());
        std::ofstream stream(target, std::ios::binary);
        const bool ok = stream &&
                cabinet->Extract(
                        file,
                        [&](std::span<const uint8_t> data) {
                            stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
                            return stream.good();
                        },
                        error);
        if (!ok) {
            stream.close();
            std::filesystem::remove(target);
            std::fprintf(stderr, "error: %s\n", error.empty() ? ("cannot write " + target.string()).c_str() : error.c_str());
            return 1;
        }
        ++extracted;
    }
    if (extracted == 0) {
        std::fprintf(stderr, "error: no stored files in group \"%s\" (see cabinet list)\n", args[1].c_str());
        return 1;
    }
    std::printf("%d files extracted\n", extracted);
    return 0;
}

int Assets(const std::vector<std::string>& args)
{
    std::string filter, error;
    std::optional<std::filesystem::path> schemaDir;
    int jobs = DefaultJobs();
    for (size_t i = 3; i < args.size(); i += 2) {
        if (i + 1 >= args.size()) return Usage();
        if (args[i] == "--filter" && args[0] == "sprites")
            filter = args[i + 1];
        else if (args[i] == "--jobs" && (args[0] == "sprites" || args[0] == "maps")) {
            const auto parsed = ParseJobs(args[i + 1]);
            if (!parsed) return Usage();
            jobs = *parsed;
        } else if (args[i] == "--schemas")
            schemaDir = std::filesystem::path(args[i + 1]);
        else
            return Usage();
    }
    obl::json_schema::Registry schemas;
    const bool loaded =
            schemaDir ? obl::formats::LoadSchemaDirectory(*schemaDir, schemas, error) : obl::formats::LoadEmbeddedSchemas(schemas, error);
    if (!loaded) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    const auto directory = obl::convert::OpenDirectorySource(std::filesystem::path(args[1]));
    // A CD root reads as the tree it would install; its large files are spooled beside the output directory meanwhile.
    const SpoolBeside spool{std::filesystem::path(args[2])};
    std::unique_ptr<obl::convert::ByteSource> cd;
    if (obl::convert::IsCdRoot(*directory) && !(cd = obl::convert::OpenCdSource(*directory, *spool, error))) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    const obl::convert::ByteSource* installation = cd ? cd.get() : directory.get();
    const auto output = obl::convert::OpenDirectorySink(std::filesystem::path(args[2]));
    if (args[0] == "scenarios") {
        int files = 0;
        std::vector<std::string> report;
        if (!obl::convert::ConvertScenarios(*installation, *output, schemas, files, error, &report)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        for (const auto& line : report) std::printf("%s\n", line.c_str());
        std::printf("%d scenario files written\n", files);
        return 0;
    }
    if (args[0] == "maps") {
        if (!filter.empty()) return Usage();
        int files = 0;
        bool upToDate = false;
        if (!obl::convert::ConvertMaps(*installation, *output, schemas, files, error, {}, &upToDate, jobs)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        if (upToDate) {
            std::printf("maps up to date (maps/conversion.stamp)\n");
            return 0;
        }
        std::printf("%d map files written\n", files);
        return 0;
    }
    if (args[0] == "catalog") {
        if (!filter.empty()) return Usage();
        int files = 0;
        if (!obl::convert::ConvertCatalog(*installation, *output, schemas, files, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::printf("%d catalog files written\n", files);
        return 0;
    }
    if (args[0] == "sounds") {
        if (!filter.empty()) return Usage();
        int files = 0, scripts = 0;
        if (!obl::convert::ConvertSamples(*installation, *output, files, error) ||
                !obl::convert::ConvertSoundScript(*installation, *output, schemas, scripts, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::printf("%d sound samples and %d sound script written\n", files, scripts);
        return 0;
    }
    if (args[0] == "guests") {
        if (!filter.empty()) return Usage();
        int files = 0;
        if (!obl::convert::ConvertGuestSets(*installation, *output, schemas, files, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::printf("%d guest sets written\n", files);
        return 0;
    }
    if (args[0] == "scripts") {
        if (!filter.empty()) return Usage();
        int files = 0;
        if (!obl::convert::ConvertScripts(*installation, *output, files, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::printf("%d script files written\n", files);
        return 0;
    }
    if (args[0] == "images" || args[0] == "fonts" || args[0] == "texts" || args[0] == "gui-layouts") {
        if (!filter.empty()) return Usage();
        int files = 0;
        if (!(args[0] == "fonts"                       ? obl::convert::ConvertFonts(*installation, *output, schemas, files, error)
                            : args[0] == "texts"       ? obl::convert::ConvertTexts(*installation, *output, schemas, files, error)
                            : args[0] == "gui-layouts" ? obl::convert::ConvertGuiLayouts(*installation, *output, schemas, files, error)
                                                       : obl::convert::ConvertImages(*installation, *output, schemas, files, error))) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        const char* label = args[0] == "fonts" ? "font" : args[0] == "texts" ? "text" : args[0] == "gui-layouts" ? "GUI layout" : "image";
        std::printf("%d %s asset files written\n", files, label);
        return 0;
    }
    if (args[0] == "media") {
        if (!filter.empty() || schemaDir) return Usage();
        int files = 0;
        if (!obl::convert::ConvertMedia(*installation, *output, files, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::printf("%d music and video files written\n", files);
        return 0;
    }
    if (args[0] == "resources") {
        if (!filter.empty() || schemaDir) return Usage();
        int files = 0;
        if (!obl::convert::ConvertResources(*installation, *output, files, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::printf("%d icon and cursor files written\n", files);
        return 0;
    }
    if (args[0] == "game-parameters") {
        if (!filter.empty()) return Usage();
        if (!obl::convert::ConvertGameParameters(*installation, *output, schemas, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::printf("rules/game_parameters.json written\n");
        return 0;
    }
    obl::convert::SpriteConversionStats stats;
    stats.jobs = jobs;
    if (!obl::convert::ConvertSprites(*installation, *output, schemas, filter, stats, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    if (stats.upToDate) {
        std::printf("sprites up to date (sprites/conversion.stamp)\n");
        return 0;
    }
    std::printf(
            "%d sprites, %d animations, %d unique frames, %d files written\n", stats.sprites, stats.animations, stats.frames, stats.files);
    return 0;
}

int SaveInfo(const std::filesystem::path& path, bool objects)
{
    std::vector<uint8_t> bytes;
    std::string error;
    std::string report;
    if (!obl::convert::ReadBoundedFile(path.string(), obl::convert::kMaxOldSaveBytes, bytes, error) ||
            !(objects ? obl::convert::DescribeOldSaveObjects(bytes, report, error) : obl::convert::DescribeOldSave(bytes, report, error))) {
        std::fprintf(stderr, "error: %s: %s\n", path.string().c_str(), error.c_str());
        return 1;
    }
    std::fputs(report.c_str(), stdout);
    return 0;
}

int Save(const std::vector<std::string>& args)
{
    std::optional<std::filesystem::path> schemaDir;
    if (args.size() == 6 && args[4] == "--schemas")
        schemaDir = std::filesystem::path(args[5]);
    else if (args.size() != 4)
        return Usage();
    obl::json_schema::Registry schemas;
    std::string error;
    const bool loaded =
            schemaDir ? obl::formats::LoadSchemaDirectory(*schemaDir, schemas, error) : obl::formats::LoadEmbeddedSchemas(schemas, error);
    if (!loaded || !obl::convert::ConvertOldSave(args[1], args[2], args[3], schemas, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s written\n", args[3].c_str());
    return 0;
}

int Pack(const std::vector<std::string>& args)
{
    std::optional<std::string> language;
    std::optional<std::filesystem::path> schemaDir;
    for (size_t i = 3; i < args.size(); i += 2) {
        if (i + 1 >= args.size()) return Usage();
        if (args[i] == "--language")
            language = args[i + 1];
        else if (args[i] == "--schemas")
            schemaDir = std::filesystem::path(args[i + 1]);
        else
            return Usage();
    }
    // A fallback changes pack identity and save text decoding. Require an explicit choice instead.
    const auto installation = obl::convert::OpenDirectorySource(std::filesystem::path(args[1]));
    if (!language) {
        std::string installed = obl::convert::InstalledLanguage(*installation);
        if (installed.empty()) {
            std::fprintf(stderr, "error: BeachLife.ini has no valid Language.Language; set it or pass --language <lang>\n");
            return 1;
        }
        language = std::move(installed);
    }
    obl::json_schema::Registry schemas;
    obl::convert::Source source;
    std::string error;
    int files = 0;
    const bool loaded =
            schemaDir ? obl::formats::LoadSchemaDirectory(*schemaDir, schemas, error) : obl::formats::LoadEmbeddedSchemas(schemas, error);
    std::error_code ec;
    if (!std::filesystem::is_directory(std::filesystem::path(args[2]), ec)) {
        std::fprintf(stderr, "error: %s: not a directory\n", args[2].c_str());
        return 1;
    }
    const auto pack = obl::convert::OpenDirectorySink(std::filesystem::path(args[2]));
    if (!loaded || !obl::convert::IdentifySource(*installation, source, error) ||
            !obl::convert::WritePackManifest(*pack, source, language, schemas, files, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    std::printf("pack.json written, %d files\n", files);
    return 0;
}

int Import(const std::vector<std::string>& args)
{
    obl::convert::ImportOptions options;
    options.jobs = DefaultJobs();
    for (size_t i = 3; i < args.size(); i += 2) {
        if (i + 1 >= args.size()) return Usage();
        if (args[i] == "--language")
            options.language = args[i + 1];
        else if (args[i] == "--jobs") {
            const auto parsed = ParseJobs(args[i + 1]);
            if (!parsed) return Usage();
            options.jobs = *parsed;
        } else
            return Usage();
    }
    const SpoolBeside spool{std::filesystem::path(args[2])};
    options.spool = spool.get();
    options.progress = [](std::string_view step) { std::printf("%.*s\n", static_cast<int>(step.size()), step.data()); };
    obl::json_schema::Registry schemas;
    std::string error;
    const auto installation = obl::convert::OpenDirectorySource(std::filesystem::path(args[1]));
    const auto pack = obl::convert::OpenDirectorySink(std::filesystem::path(args[2]));
    if (!obl::formats::LoadEmbeddedSchemas(schemas, error) || !obl::convert::ImportPack(*installation, *pack, schemas, options, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    std::printf("pack written to %s\n", args[2].c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() >= 3 &&
            (args[0] == "sprites" || args[0] == "images" || args[0] == "fonts" || args[0] == "texts" || args[0] == "gui-layouts" ||
                    args[0] == "sounds" || args[0] == "catalog" || args[0] == "maps" || args[0] == "scenarios" ||
                    args[0] == "game-parameters" || args[0] == "media" || args[0] == "resources" || args[0] == "guests" ||
                    args[0] == "scripts")) {
        return Assets(args);
    }
    if (args.size() == 2 && args[0] == "footprints") return Footprints(args[1]);
    if (args.size() == 2 && args[0] == "gui-tables") return GuiTables(args[1]);
    if (args.size() == 3 && args[0] == "gui-tables") return GuiTables(args[1], args[2]);
    if (args.size() == 2 && args[0] == "identify") return Identify(args[1]);
    if (args.size() == 3 && args[0] == "cabinet" && (args[1] == "list" || args[1] == "verify")) return CabinetCommand(args[1], {args[2]});
    if (args.size() == 5 && args[0] == "cabinet" && args[1] == "extract") return CabinetCommand(args[1], {args[2], args[3], args[4]});
    if (args.size() >= 3 && args[0] == "pack") return Pack(args);
    if (args.size() >= 3 && args[0] == "import") return Import(args);
    if (args.size() >= 4 && args[0] == "save") return Save(args);
    if (args.size() == 2 && args[0] == "save-info") return SaveInfo(args[1], false);
    if (args.size() == 3 && args[0] == "save-info" && args[2] == "--objects") return SaveInfo(args[1], true);
    return Usage();
}
