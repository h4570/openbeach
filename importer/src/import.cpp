#include "openbeach/importer/import.h"

#include "importer/src/readers/gui_tables.h"
#include "importer/src/source/cd_source.h"
#include "importer/src/source/pe_file.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/catalog_writer.h"
#include "importer/src/writers/font_convert.h"
#include "importer/src/writers/game_parameters_convert.h"
#include "importer/src/writers/guest_set_convert.h"
#include "importer/src/writers/gui_layout_convert.h"
#include "importer/src/writers/gui_writer.h"
#include "importer/src/writers/image_convert.h"
#include "importer/src/writers/map_convert.h"
#include "importer/src/writers/media_convert.h"
#include "importer/src/writers/pack_manifest.h"
#include "importer/src/writers/scenario_convert.h"
#include "importer/src/writers/script_convert.h"
#include "importer/src/writers/sound_convert.h"
#include "importer/src/writers/sound_script_convert.h"
#include "importer/src/writers/sprite_convert.h"
#include "importer/src/writers/text_convert.h"

namespace obl::convert {
namespace {

bool WriteExeGuiTables(const ByteSource& source, PackSink& pack, std::string& error)
{
    const auto path = FindFile(source, kExePath);
    if (!path) {
        error = std::string(kExePath) + " not found";
        return false;
    }
    auto bytes = source.ReadAll(*path);
    if (!bytes) {
        error = "cannot read " + source.Describe(*path);
        return false;
    }
    const auto exe = PeFile::Parse(std::move(*bytes), error);
    ExeGuiTables tables;
    if (!exe || !FindGuiTables(*exe, tables, error)) {
        error = source.Describe(*path) + ": " + error;
        return false;
    }
    return WriteGuiTables(tables, pack, error);
}

} // namespace

bool ImportPack(
        const ByteSource& picked, PackSink& pack, const json_schema::Registry& schemas, const ImportOptions& options, std::string& error)
{
    // A CD is read as the tree it would install.
    std::unique_ptr<ByteSource> cd;
    if (IsCdRoot(picked)) {
        if (!options.spool) {
            error = "a CD import needs a spool";
            return false;
        }
        cd = OpenCdSource(picked, *options.spool, error);
        if (!cd) return false;
    }
    const ByteSource& source = cd ? *cd : picked;
    // The language decides the pack's identity and how saves decode text, so the source or the caller names it. Only a caller
    // that asks for it (the browser, whose player chooses among the pack's tables afterwards) gets a pick among the copy's tables.
    const bool pick = !options.language && options.preferredLanguage;
    std::string language = options.language ? *options.language
            : pick                          ? ChooseLanguage(source, *options.preferredLanguage)
                                            : InstalledLanguage(source);
    const bool hasExe = FindFile(source, kExePath).has_value(); // a missing executable is the first step's message
    if (language.empty() && (hasExe || !pick)) {
        error = pick ? "your copy of the game has no text tables" : "BeachLife.ini has no valid Language.Language; choose the language";
        return false;
    }
    // Texts in a language the copy lacks would end in another language's table (or none); say so before anything is written.
    if (hasExe && !HasTextTable(source, language)) {
        error = "your copy of the game has no texts in the language \"" + language + "\"; choose another language";
        return false;
    }
    if (options.chosenLanguage) *options.chosenLanguage = language;
    int files = 0;
    SpriteConversionStats sprites;
    Source identity;
    // The counting callback of a step, or none when the caller does not count.
    const auto countOf = [&](std::string_view step) -> std::function<void(int, int)> {
        if (!options.count) return {};
        return [&options, step](int done, int total) { options.count(step, done, total); };
    };
    if (options.count) sprites.report = [&](int done, int total) { options.count("sprites", done, total); };
    // a fixed order; pack.json last, since it hashes every file before it.
    const std::pair<std::string_view, std::function<bool()>> steps[] = {
            {"gui-tables", [&] { return WriteExeGuiTables(source, pack, error); }},
            {"game-parameters", [&] { return ConvertGameParameters(source, pack, schemas, error); }},
            {"scripts", [&] { return ConvertScripts(source, pack, files, error); }},
            {"maps", [&] { return ConvertMaps(source, pack, schemas, files, error, countOf("maps")); }},
            {"gui-layouts", [&] { return ConvertGuiLayouts(source, pack, schemas, files, error); }},
            {"sounds",
                    [&] { return ConvertSamples(source, pack, files, error) && ConvertSoundScript(source, pack, schemas, files, error); }},
            {"images", [&] { return ConvertImages(source, pack, schemas, files, error); }},
            {"media", [&] { return ConvertMedia(source, pack, files, error); }},
            {"sprites", [&] { return ConvertSprites(source, pack, schemas, {}, sprites, error); }},
            {"fonts", [&] { return ConvertFonts(source, pack, schemas, files, error); }},
            {"catalog", [&] { return ConvertCatalog(source, pack, schemas, files, error); }},
            {"guests", [&] { return ConvertGuestSets(source, pack, schemas, files, error); }},
            {"texts", [&] { return ConvertTexts(source, pack, schemas, files, error); }},
            {"scenarios", [&] { return ConvertScenarios(source, pack, schemas, files, error); }},
            {"pack",
                    [&] {
                        const auto known = options.knownExes.empty() ? SupportedExes() : options.knownExes;
                        return IdentifySource(source, identity, error, known) &&
                                WritePackManifest(pack, identity, language, schemas, files, error, countOf("pack"));
                    }},
    };
    for (const auto& [name, run] : steps) {
        if (options.progress) options.progress(name);
        if (!run()) {
            error = std::string(name) + ": " + error;
            return false;
        }
    }
    return true;
}

} // namespace obl::convert
