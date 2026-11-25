#include "game/game/start_settings.h"

#include "core/char16.h"
#include "core/code_page.h"
#include "core/ini_file.h"
#include "core/pack_path.h"
#include "game/game/settings_ini.h"
#include "game/tables/text_load.h"
#include "game/ui/display_geometry.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace obl::game::start_settings {

const char kDefaultFile[] = "; OpenBeach settings (read at start-up)\r\n"
                            "\r\n"
                            "[Display]\r\n"
                            "; 1 = window 800x600 (movable, alt-tab friendly), 0 = borderless fullscreen on the monitor\r\n"
                            "; Alt+Enter switches between the two and stores the choice here\r\n"
                            "Windowed=1\r\n"
                            "; 1 = in gameplay keep the mouse in the active game window, so the screen edge scrolls the map;\r\n"
                            "; Alt+Tab or the options (Esc) let it go, 0 = the mouse leaves the window freely\r\n"
                            "ConfineMouse=1\r\n"
                            "; window size multiplier (1..4, decimals allowed), capped at the monitor size\r\n"
                            "Scale=1\r\n"
                            "; frame rate cap, 0 = unlimited (render as fast as possible)\r\n"
                            "MaxFps=60\r\n"
                            "\r\n"
                            "[Intro]\r\n"
                            "; 1 = play the Eidos intro movie (in the game window when Windowed=1), 0 = skip it\r\n"
                            "PlayMovie=1\r\n"
                            "\r\n"
                            "[Launcher]\r\n"
                            "; 1 = show the settings window before the game starts, 0 = start the game at once\r\n"
                            "; (OpenBeach --settings shows the window once more)\r\n"
                            "ShowSettings=1\r\n";

namespace {

namespace fs = std::filesystem;

constexpr const char* kLanguageNames[] = {"English", "French", "Italian", "German", "Spanish", "Polish"};
static_assert(std::size(kLanguageNames) == text_load::kLanguageCount);

bool ReadBool(const fs::path& path, const char* section, const char* key, bool fallback)
{
    return ini::ReadInt(path, section, key, fallback ? 1 : 0) != 0;
}

// The requested language as the game reads it (settings::LoadMain): nothing counts without the main section's Version.
unsigned ReadLanguage(const fs::path& path)
{
    if (ini::ReadString(path, settings::kMainSection, "Version", settings::kMissing, settings::kMainBuffer) != settings::kMainVersion)
        return 0;
    const std::string text = ini::ReadString(path, "Language", "Language", settings::kMissing, settings::kMainBuffer);
    if (text == settings::kMissing) return 0;
    const int index = std::atoi(text.c_str());
    return index >= 0 && static_cast<unsigned>(index) < text_load::kLanguageCount ? static_cast<unsigned>(index) : 0;
}

double ReadScale(const fs::path& path)
{
    const std::u16string wide = code_page::Utf8ToUtf16(ini::ReadString(path, "Display", "Scale", "1", 64));
    const Text16 text(wide.begin(), wide.end());
    double scale = 1.0;
    return ui::ParseScale(text.c_str(), &scale) ? scale : 1.0;
}

std::string Number(double value)
{
    return settings_schema::NumberText(value);
}

using settings_schema::Field;
using settings_schema::Kind;

Field Toggle(const char* key, bool on)
{
    Field field;
    field.id = key;
    field.kind = Kind::Toggle;
    field.defaultValue = on ? "1" : "0";
    return field;
}

Field Choice(const char* key, std::vector<settings_schema::Option> options, const char* defaultValue, bool zeroLast = false)
{
    Field field;
    field.id = key;
    field.kind = Kind::Choice;
    field.options = std::move(options);
    field.defaultValue = defaultValue;
    field.zeroLast = zeroLast;
    return field;
}

struct EngineSetting {
    Field field;
    const char* section;
};

const std::vector<EngineSetting>& EngineSettings()
{
    static const std::vector<EngineSetting> settings = {
            {Toggle("Windowed", true), "Display"},
            // The sizes the window offers ([Display] Scale is 1..4; the game accepts decimals, so any other value stays selectable).
            {Choice("Scale", {{"1", "1x"}, {"1.5", "1.5x"}, {"2", "2x"}, {"2.5", "2.5x"}, {"3", "3x"}, {"3.5", "3.5x"}, {"4", "4x"}}, "1"),
                    "Display"},
            {Toggle("ConfineMouse", true), "Display"},
            {Choice("MaxFps",
                     {{"30", "30 fps"}, {"60", "60 fps"}, {"75", "75 fps"}, {"120", "120 fps"}, {"144", "144 fps"}, {"240", "240 fps"},
                             {"0", "unlimited"}},
                     "60", true),
                    "Display"},
            {Toggle("PlayMovie", true), "Intro"},
            {Toggle("ShowSettings", true), "Launcher"},
    };
    return settings;
}

} // namespace

const settings_schema::Field& EngineField(Setting setting)
{
    return EngineSettings()[static_cast<size_t>(setting)].field;
}

const char* EngineSection(Setting setting)
{
    return EngineSettings()[static_cast<size_t>(setting)].section;
}

std::string Get(const Values& v, Setting setting)
{
    switch (setting) {
    case Windowed: return v.windowed ? "1" : "0";
    case Scale: return Number(v.scale);
    case ConfineMouse: return v.confineMouse ? "1" : "0";
    case MaxFps: return std::to_string(v.maxFps);
    case PlayMovie: return v.playMovie ? "1" : "0";
    default: return v.showAtStart ? "1" : "0";
    }
}

void Set(Values& v, Setting setting, std::string_view canonical)
{
    const auto canonicalValue = settings_schema::Canonical(EngineField(setting), canonical);
    // A value of the file that the window does not offer (Scale=1.75) is not an option but still a value.
    const std::string text(canonicalValue ? *canonicalValue : std::string(canonical));
    const bool on = settings_schema::IsOn(text);
    switch (setting) {
    case Windowed: v.windowed = on; break;
    case Scale: v.scale = std::atof(text.c_str()); break;
    case ConfineMouse: v.confineMouse = on; break;
    case MaxFps: v.maxFps = std::atoi(text.c_str()); break;
    case PlayMovie: v.playMovie = on; break;
    default: v.showAtStart = on; break;
    }
}

bool CreateDefaultFile(const fs::path& path)
{
    std::error_code error;
    if (fs::exists(path, error) || error) return !error;
    std::ofstream out(path, std::ios::binary);
    return static_cast<bool>(out.write(kDefaultFile, sizeof(kDefaultFile) - 1));
}

Values Load(const fs::path& gameDir, const fs::path& pack)
{
    Values values;
    const fs::path file = gameDir / kFileName;
    values.windowed = ReadBool(file, "Display", "Windowed", values.windowed);
    values.confineMouse = ReadBool(file, "Display", "ConfineMouse", values.confineMouse);
    values.scale = ReadScale(file);
    values.maxFps = ini::ReadInt(file, "Display", "MaxFps", values.maxFps);
    values.playMovie = ReadBool(file, "Intro", "PlayMovie", values.playMovie);
    values.showAtStart = ReadBool(file, "Launcher", "ShowSettings", values.showAtStart);

    for (unsigned index = 0; index < text_load::kLanguageCount; ++index)
        if (fs::is_regular_file(pack_path::Resolve(pack, text_load::PackPath(text_load::LanguageCode(index)))))
            values.languages.push_back(index);
    // The game's own start-up choice, so the window names the language the player will see.
    unsigned language = ReadLanguage(gameDir / settings::kIniFileName);
    std::string notice, error;
    text_load::LanguageSelection selection;
    if (selection.Select(pack, language, notice, error, text_load::PackLanguage(pack))) values.language = language;
    return values;
}

bool Save(const fs::path& gameDir, const Values& before, const Values& after)
{
    bool written = true;
    const fs::path file = gameDir / kFileName;
    const auto put = [&](const fs::path& path, const char* section, const char* key, const std::string& value) {
        written &= ini::WriteString(path, section, key, value);
    };
    const auto flag = [](bool value) { return std::string(value ? "1" : "0"); };
    if (after.windowed != before.windowed || after.scale != before.scale || after.confineMouse != before.confineMouse ||
            after.maxFps != before.maxFps || after.playMovie != before.playMovie || after.showAtStart != before.showAtStart)
        written &= CreateDefaultFile(file);
    if (after.windowed != before.windowed) put(file, "Display", "Windowed", flag(after.windowed));
    if (after.confineMouse != before.confineMouse) put(file, "Display", "ConfineMouse", flag(after.confineMouse));
    if (after.scale != before.scale) put(file, "Display", "Scale", Number(after.scale));
    if (after.maxFps != before.maxFps) put(file, "Display", "MaxFps", std::to_string(after.maxFps));
    if (after.playMovie != before.playMovie) put(file, "Intro", "PlayMovie", flag(after.playMovie));
    if (after.showAtStart != before.showAtStart) put(file, "Launcher", "ShowSettings", flag(after.showAtStart));

    if (after.language != before.language) {
        const fs::path main = gameDir / settings::kIniFileName;
        if (ini::ReadString(main, settings::kMainSection, "Version", settings::kMissing, settings::kMainBuffer) != settings::kMainVersion)
            put(main, settings::kMainSection, "Version", settings::kMainVersion);
        put(main, "Language", "Language", std::to_string(after.language));
    }
    return written;
}

const char* LanguageName(unsigned index)
{
    return kLanguageNames[index < std::size(kLanguageNames) ? index : 0];
}

} // namespace obl::game::start_settings
