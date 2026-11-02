#include "core/char16.h"
#include "core/config.h"

#include "core/code_page.h"
#include "game/ui/display_geometry.h"
#include "core/ini_file.h"
#include "core/log.h"
#include "core/os.h"
#include "core/wide_path.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace obl::config {

namespace {

Settings g_settings;

constexpr char kDefaultIni[] = "; OpenBeach settings (read at start-up)\r\n"
                               "\r\n"
                               "[Display]\r\n"
                               "; 1 = window 800x600 (movable, alt-tab friendly), 0 = borderless fullscreen on the monitor\r\n"
                               "Windowed=1\r\n"
                               "; window size multiplier (1..4, decimals allowed), capped at the monitor size\r\n"
                               "Scale=1\r\n"
                               "; frame rate cap, 0 = unlimited (render as fast as possible)\r\n"
                               "MaxFps=60\r\n"
                               "\r\n"
                               "[Intro]\r\n"
                               "; 1 = play the Eidos intro movie (in the game window when Windowed=1), 0 = skip it\r\n"
                               "PlayMovie=1\r\n";

bool ReadBool(const std::filesystem::path& path, const char* section, const char* key, bool fallback)
{
    return ini::ReadInt(path, section, key, fallback ? 1 : 0) != 0;
}

} // namespace

void Load(const Text16& gameDirectory)
{
    const std::filesystem::path path = WidePath(gameDirectory) / "openbeach.ini";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) && !ec) {
        std::ofstream out(path, std::ios::binary);
        if (out.write(kDefaultIni, sizeof(kDefaultIni) - 1)) OBL_LOG("config: created default openbeach.ini");
    }

    g_settings.windowed = ReadBool(path, "Display", "Windowed", g_settings.windowed);
    g_settings.maxFps = ini::ReadInt(path, "Display", "MaxFps", g_settings.maxFps);
    g_settings.playIntroMovie = ReadBool(path, "Intro", "PlayMovie", g_settings.playIntroMovie);

    // Scripted test runs (OBL_SCRIPT) must be fast and start at the first frame: no intro movie, no frame cap. The
    // user's openbeach.ini stays untouched; OBL_MAXFPS / OBL_PLAYMOVIE (launcher --max-fps / --movie) override.
    if (os::HasEnv("OBL_SCRIPT")) {
        g_settings.maxFps = 0;
        g_settings.playIntroMovie = false;
        OBL_LOG("config: script mode, MaxFps=0 PlayMovie=0 unless overridden");
    }
    if (const auto replay = os::GetEnv("OBL_HEADLESS"); replay && !replay->empty()) {
        g_settings.headless = true;
        g_settings.headlessScript = os::HasEnv("OBL_SCRIPT");
        if (!g_settings.headlessScript) {
            const std::u16string wide = code_page::Utf8ToUtf16(*replay);
            g_settings.headlessReplay.assign(wide.begin(), wide.end());
        }
        g_settings.maxFps = 0;
        g_settings.playIntroMovie = false;
        OBL_LOG("config: headless mode (%s), MaxFps=0 PlayMovie=0", g_settings.headlessScript ? "script" : "replay");
    }
    std::string scaleText = ini::ReadString(path, "Display", "Scale", "1", 64);
    if (const auto scale = os::GetEnv("OBL_SCALE")) scaleText = scale->size() < 64 ? *scale : std::string();
    const std::u16string scaleWide = code_page::Utf8ToUtf16(scaleText);
    const Text16 scaleValue(scaleWide.begin(), scaleWide.end());
    g_settings.scale = 1.0;
    if (!ui::ParseScale(scaleValue.c_str(), &g_settings.scale)) OBL_LOG("config: invalid Scale, using 1");
    if (const auto value = os::GetEnv("OBL_WINDOWED")) g_settings.windowed = std::atoi(value->c_str()) != 0;
    if (const auto value = os::GetEnv("OBL_MAXFPS")) g_settings.maxFps = std::atoi(value->c_str());
    if (const auto value = os::GetEnv("OBL_PLAYMOVIE")) g_settings.playIntroMovie = std::atoi(value->c_str()) != 0;
    OBL_LOG("config: Windowed=%d Scale=%g MaxFps=%d PlayMovie=%d", g_settings.windowed, g_settings.scale, g_settings.maxFps,
            g_settings.playIntroMovie);
}

const Settings& Get()
{
    return g_settings;
}

} // namespace obl::config
