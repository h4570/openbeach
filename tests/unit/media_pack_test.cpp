#include "unit_test.h"
#include "game/ui/media_pack.h"
#include "game/game/settings_ini.h"
#include "temp_dir.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

using namespace obl;

namespace {

void WriteFile(const std::filesystem::path& file)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << "media";
}

} // namespace

OBL_TEST_CASE("media_pack.check_readable_names_missing_file")
{
    const obl::test::TempPath tmp("obl-media-pack");
    const std::string present = (tmp.path / "video" / "intro.mpg").string();
    const std::string missing = (tmp.path / "music" / "day1.mp3").string();
    WriteFile(present);

    std::string error;
    OBL_CHECK(media_pack::CheckReadable(present, error));
    OBL_CHECK(error.empty());

    // A missing file stops the game with its name and the step that writes it (the hint of the other pack loaders).
    OBL_CHECK(!media_pack::CheckReadable(missing, error));
    OBL_CHECK(error.find(missing) != std::string::npos);
    OBL_CHECK(error.find("cannot open") != std::string::npos);
    OBL_CHECK(error.find("Import the game data again") != std::string::npos);
}

OBL_TEST_CASE("media_pack.converted_track_opens_from_the_mod_layer_first")
{
    // The playlist and the night stream open the converted track through the layers: `mod/music/` shadows `packs/base/music/`.
    const obl::test::TempPath game("media-pack-layers");
    const auto pack = game.path / "packs" / "base";
    const auto mod = game.path / "mod";
    WriteFile(pack / "music" / "day1.mp3");
    WriteFile(pack / "music" / "day2.mp3");
    WriteFile(mod / "music" / "day2.mp3");

    // Only the pack has the file: the pack's directory, with the separator the playlist appends the name to.
    OBL_CHECK(media_pack::TrackDirectory(pack, media_pack::kTrackDirectory, "day1.mp3") == (pack / "music").generic_string() + "/");
    OBL_CHECK(media_pack::TrackPath(pack, media_pack::kTrackDirectory, "day1.mp3") == (pack / "music" / "day1.mp3").generic_string());
    // Both have the file: the mod layer's one.
    OBL_CHECK(media_pack::TrackPath(pack, media_pack::kTrackDirectory, "day2.mp3") == (mod / "music" / "day2.mp3").generic_string());
    // Neither has it: the pack's path, so the caller reports the converted file.
    OBL_CHECK(media_pack::TrackPath(pack, media_pack::kTrackDirectory, "night9.mp3") == (pack / "music" / "night9.mp3").generic_string());
    // Other directories (the user's own music folder) are not layered: directory + file as before.
    OBL_CHECK(media_pack::TrackDirectory(pack, "music\\", "day2.mp3") == "music\\");
    OBL_CHECK(media_pack::TrackPath(pack, "music\\", "day2.mp3") == "music\\day2.mp3");
}

OBL_TEST_CASE("media_pack.soundtrack_names_are_converted_content_keys")
{
    // obl-convert media names every track `music/<lowercase name>.mp3` (media_convert.cpp); the playlist must ask for those names.
    OBL_CHECK(std::string(media_pack::kTrackDirectory) == "packs/base/music/");
    for (const auto& track : settings::kSoundtrack) {
        const std::string file = track.file;
        std::string lower = file;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        OBL_CHECK(file == lower);
        OBL_CHECK(file.size() > 4 && file.substr(file.size() - 4) == ".mp3");
    }
}

OBL_TEST_CASE("media_pack.selected_pack_and_mod_supply_music_and_intro")
{
    const obl::test::TempPath game("media-selected-pack");
    const auto pack = game.path / "packs" / "l0";
    const auto base = game.path / "packs" / "base";
    const auto mod = game.path / "mod";
    WriteFile(base / "music" / "night1.mp3");
    WriteFile(base / "video" / "intro.mpg");
    WriteFile(pack / "music" / "night1.mp3");
    WriteFile(pack / "video" / "intro.mpg");
    OBL_CHECK(media_pack::TrackPath(pack, media_pack::kTrackDirectory, "night1.mp3") == (pack / "music/night1.mp3").generic_string());
    OBL_CHECK(media_pack::IntroPath(pack) == (pack / "video/intro.mpg").generic_string());
    WriteFile(mod / "music" / "night1.mp3");
    WriteFile(mod / "video" / "intro.mpg");
    OBL_CHECK(media_pack::TrackPath(pack, media_pack::kTrackDirectory, "night1.mp3") == (mod / "music/night1.mp3").generic_string());
    OBL_CHECK(media_pack::IntroPath(pack) == (mod / "video/intro.mpg").generic_string());
    std::filesystem::remove(pack / "video/intro.mpg");
    std::filesystem::remove(mod / "video/intro.mpg");
    OBL_CHECK(media_pack::IntroPath(pack) == (pack / "video/intro.mpg").generic_string());
}
