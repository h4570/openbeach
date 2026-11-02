#include "game/weather/weather_state.h"
#include "assertions.h"
#include "core/code_page.h"
#include "game/ui/text_table.h"
#include "game/game/settings_store.h"

#include "breakdowns.h"
#include "building_fixture.h"
#include "game/ui/image.h"
#include "game/objects/script_program_run.h"
#include "game/objects/script_program_load.h"
#include "game/ui/sound_api.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/world/world_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/world/world_script_load.h"
#include "music.h"
#include "samples.h"
#include "platform/intro_movie.h"
#include "gui_control.h"
#include "game/ui/map_view_blocks.h"
#include "game/ui/map_view_access.h"
#include "game/world/world_construct_pis.h"
#include "platform/test_tools/presenter.h"
#include "core/log.h"
#include "game/ui/map_blocks.h"
#include "test_util.h"
#include "world_hash.h"
#include "world_state.h"
#include "determinism.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include "game/ui/map_view.h"

namespace obl::test {
namespace {

std::mutex g_reportMutex;
std::map<std::string, double> g_reportValues;
std::atomic<int> g_passed, g_failed;
unsigned long long g_frames;

bool IsKeyChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-';
}

// Runs under the log mutex for every message: keep it cheap, never log.
void OnLogMessage(const char* message)
{
    constexpr char kPrefix[] = "autotest: ";
    if (std::strncmp(message, kPrefix, sizeof(kPrefix) - 1) != 0) return;
    const char* p = message + sizeof(kPrefix) - 1;
    if (std::strncmp(p, "PASS", 4) == 0 && (p[4] == ' ' || p[4] == 0))
        ++g_passed;
    else if (std::strncmp(p, "FAILED", 6) == 0 && (p[6] == ' ' || p[6] == 0))
        ++g_failed;

    std::lock_guard lock(g_reportMutex);
    while (*p) {
        const char* keyStart = p;
        while (IsKeyChar(*p)) ++p;
        if (p > keyStart && *p == '=') {
            char* end;
            double value = std::strtod(p + 1, &end);
            // "x=1/2/3" style fields are not numbers; only a clean number ends at a separator.
            if (end != p + 1 && (*end == 0 || *end == ' ' || *end == ',' || *end == ')' || *end == ';' || *end == ']'))
                g_reportValues[std::string(keyStart, p)] = value;
            p = end > p + 1 ? end : p + 1;
        } else if (p == keyStart) {
            ++p;
        }
    }
}

using Probe = std::function<bool(double&)>;

// A probe reading one value of a game object that does not exist outside its state (World, renderer...).
template<class Object, class Read> Probe ObjectProbe(Object* (*get)(), Read read)
{
    return [get, read](double& out) {
        Object* object = get();
        if (!object) return false;
        out = static_cast<double>(read(*object));
        return true;
    };
}

// `Group` is the World state group that declares the field (world_state.h).
template<class Group, class T> Probe WorldField(T Group::* field)
{
    return ObjectProbe(&game::GetWorld, [field](const layout::World& world) { return world.*field; });
}

// The mapped background blocks of MapView (step 12 of the `MapView_*` plan): the block table flags and the contents of every mapped view,
// as one FNV-1a hash (a 32-bit value is exact as a double, but `expect` prints six digits: `map-hash-lo`/`-hi` are its 16-bit halves, exact
// in the log). The block lifecycle and every writer of the blocks leave their effect only in the mapped file.
enum class MapBlocksValue { Count, Hash, HashLow, HashHigh };

bool MapBlocksProbe(double& out, MapBlocksValue value)
{
    ui::MapView* view = ui::GetMapView();
    if (!view || !view->m_blocksMapped || !view->m_blocks || view->m_blockSize <= 0) return false;
    const int count = view->m_blocksPerRow * view->m_blocksPerColumn;
    const size_t bytes = static_cast<size_t>(view->m_blockSize) * static_cast<size_t>(view->m_blockSize) * map_blocks::kBytesPerPixel;
    uint32_t hash = map_blocks::kFnvOffset;
    int mapped = 0;
    for (int i = 0; i < count; ++i) {
        const ui::MapBlock& block = view->m_blocks[i];
        if (!block.m_loaded || !block.m_pixels) continue;
        ++mapped;
        hash = map_blocks::Fnv1a(hash, &i, sizeof(i));
        if (value != MapBlocksValue::Count) hash = map_blocks::Fnv1a(hash, block.m_pixels, bytes);
    }
    switch (value) {
    case MapBlocksValue::Count: out = mapped; break;
    case MapBlocksValue::Hash: out = hash; break;
    case MapBlocksValue::HashLow: out = hash & 0xffffu; break;
    case MapBlocksValue::HashHigh: out = hash >> 16; break;
    }
    return true;
}

const std::map<std::string, Probe>& Probes()
{
    using layout::World;
    static const std::map<std::string, Probe> probes = {
            {"hour", WorldField(&World::m_gameDayTime)},
            {"day", WorldField(&World::m_gameDay)},
            {"game-seconds", WorldField(&World::m_gameTime)},
            {"speed", WorldField(&World::m_gameSpeed)},
            {"speed-level", WorldField(&World::m_gameSpeedLevel)},
            {"paused", WorldField(&World::m_paused)},
            {"forced-paused", WorldField(&World::m_forcedPaused)},
            {"weather-manual", ObjectProbe(&game::GetWeather, [](const weather::Weather& weather) { return weather.m_manualLighting; })},
            {"weather-manual-col", ObjectProbe(&game::GetWeather, [](const weather::Weather& weather) { return weather.m_manualColor; })},
            {"power-demand-all", WorldField(&World::m_powerUse)},
            {"power-capacity", WorldField(&World::m_maxPower)},
            {"power-output", WorldField(&World::m_currentPower)},
            {"power-demand", WorldField(&World::m_powerDemand)},
            {"cash", ObjectProbe(&game::GetWorld, [](const World& world) { return world.m_player->m_cash; })},
            // The day's money counters the place modes and the dozer write.
            {"income-today", WorldField(&World::m_incomeToday)},
            {"demolish-income-today", WorldField(&World::m_demolishIncomeToday)},
            {"costs-today", WorldField(&World::m_costsToday)},
            {"scenery-costs-today", WorldField(&World::m_sceneryCostToday)},
            {"buildings", ObjectProbe(&game::GetObjectManager, [](const object::Manager& m) { return m.m_buildings.size(); })},
            {"guests", ObjectProbe(&game::GetObjectManager, [](const object::Manager& m) { return m.m_guests.size(); })},
            // Guest arrival/departure sums of the world (both genders): the natural departure path (state 0x29 -> world::OnObjectDeleting).
            {"guests-arrived-total",
                    ObjectProbe(&game::GetWorld, [](const World& w) { return w.m_totalGuestArrive[0] + w.m_totalGuestArrive[1]; })},
            {"guests-departed-total",
                    ObjectProbe(&game::GetWorld, [](const World& w) { return w.m_totalGuestDepart[0] + w.m_totalGuestDepart[1]; })},
            {"guests-departed-today",
                    ObjectProbe(&game::GetWorld, [](const World& w) { return w.m_todayGuestDepart[0] + w.m_todayGuestDepart[1]; })},
            {"guests-unhappy-departed-total",
                    ObjectProbe(&game::GetWorld,
                            [](const World& w) { return w.m_totalGuestUnhappyDepart[0] + w.m_totalGuestUnhappyDepart[1]; })},
            {"render-width", ObjectProbe(&game::GetRenderer, [](const ui::Renderer& r) { return r.m_width; })},
            {"zoom-level", ObjectProbe(&game::GetRenderer, [](const ui::Renderer& r) { return r.m_zoomLevel; })},
            // GUI button actions (docs/architecture/data-formats/converter/gui-runtime.md): the Sound object's music volume and 3D maximum distance, and the volume saved in [Settings].
            {"sound-music-volume",
                    [](double& out) {
                        const auto* sound = audio::CurrentSound();
                        if (!sound) return false;
                        out = sound->musicVolume;
                        return true;
                    }},
            {"sound-max-distance",
                    [](double& out) {
                        const auto* sound = audio::CurrentSound();
                        if (!sound) return false;
                        out = sound->maxDistance;
                        return true;
                    }},
            {"settings-language",
                    [](double& out) {
                        const game::Game* game = game::GetGame();
                        if (!game || !game->m_settings) return false;
                        out = game->m_settings->m_language;
                        return true;
                    }},
            {"settings-shown-language",
                    [](double& out) {
                        const game::Game* game = game::GetGame();
                        if (!game || !game->m_settings) return false;
                        out = game->m_settings->m_shownLanguage;
                        return true;
                    }},
            {"settings-music-volume",
                    [](double& out) {
                        const game::Game* game = game::GetGame();
                        if (!game || !game->m_settings) return false;
                        out = game->m_settings->m_musicVolume;
                        return true;
                    }},
            {"settings-effects-volume",
                    [](double& out) {
                        const game::Game* game = game::GetGame();
                        if (!game || !game->m_settings) return false;
                        out = game->m_settings->m_effectsVolume;
                        return true;
                    }},
            {"sound-effects-scale",
                    [](double& out) {
                        const auto* sound = audio::CurrentSound();
                        if (!sound) return false;
                        out = sound->effectsScale;
                        return true;
                    }},
            // [Settings] video flags the in-game options window resets (ResetVideoOptions): NudgeScroll, SeaEffects, LightEffects, Keyline.
            {"settings-video-flags",
                    [](double& out) {
                        const game::Game* game = game::GetGame();
                        if (!game || !game->m_settings) return false;
                        const game::Settings& s = *game->m_settings;
                        out = s.m_nudgeScroll | s.m_seaEffects << 1 | s.m_lightEffects << 2 | s.m_keyline << 3;
                        return true;
                    }},
            {"camera-x", ObjectProbe(&ui::GetMapView, [](const ui::MapView& v) { return v.m_cameraX; })},
            {"camera-y", ObjectProbe(&ui::GetMapView, [](const ui::MapView& v) { return v.m_cameraY; })},
            {"camera-fx", ObjectProbe(&ui::GetMapView, [](const ui::MapView& v) { return v.m_cameraPositionX; })},
            {"camera-fy", ObjectProbe(&ui::GetMapView, [](const ui::MapView& v) { return v.m_cameraPositionY; })},
            {"camera-locked", ObjectProbe(&ui::GetMapView, [](const ui::MapView& v) { return v.m_mapLocked; })},
            {"camera-moving", ObjectProbe(&ui::GetMapView, [](const ui::MapView& v) { return v.m_movingMap; })},
            // Guests that have not been sent to a building yet (m_CurID = -1, what the guest constructor leaves through game/guest.cpp).
            {"guests-without-place",
                    [](double& out) {
                        const object::Manager* objects = game::GetObjectManager();
                        if (!objects) return false;
                        int guests = 0;
                        for (uint32_t i = 0; i < objects->m_guests.size(); ++i)
                            guests += static_cast<const object::Guest*>(objects->m_guests[i])->m_CurID == -1;
                        out = guests;
                        return true;
                    }},
            {"guests-drawn", ObjectProbe(&ui::GetMapView, [](const ui::MapView& v) { return v.m_guestObjectsDrawn; })},
            // MapView::GetVisibleTileRange through its patched entry: the share of sea tiles under the view (0..1).
            {"sea-share",
                    [](double& out) {
                        ui::MapView* view = ui::GetMapView();
                        if (!view || !game::GetGrid() || !game::GetRenderer()) return false;
                        out = view->GetVisibleTileRange();
                        return true;
                    }},
            {"map-hash", [](double& out) { return MapBlocksProbe(out, MapBlocksValue::Hash); }},
            {"map-hash-lo", [](double& out) { return MapBlocksProbe(out, MapBlocksValue::HashLow); }},
            {"map-hash-hi", [](double& out) { return MapBlocksProbe(out, MapBlocksValue::HashHigh); }},
            {"map-blocks", [](double& out) { return MapBlocksProbe(out, MapBlocksValue::Count); }},
            // MapView::GetRebuildQueueSize through its patched entry: the blocks still waiting in the rebuild queue (`queue-blocks`).
            {"rebuild-queue",
                    [](double& out) {
                        ui::MapView* view = ui::GetMapView();
                        if (!view || !view->m_blocksMapped) return false;
                        out = view->GetRebuildQueueSize();
                        return true;
                    }},
            {"render-height", ObjectProbe(&game::GetRenderer, [](const ui::Renderer& r) { return r.m_height; })},
            {"state",
                    [](double& out) {
                        out = GameStateIndex();
                        return true;
                    }},
            {"scenario-objective0", ObjectProbe(&game::GetWorld, [](const World& world) { return world.m_objectiveStatus[0]; })},
            {"scenario-objective1", ObjectProbe(&game::GetWorld, [](const World& world) { return world.m_objectiveStatus[1]; })},
            {"scenario-objective2", ObjectProbe(&game::GetWorld, [](const World& world) { return world.m_objectiveStatus[2]; })},
            {"build-tree-missing", ObjectProbe(&game::GetWorld, [](const World& world) { return world.m_buildingsLeftToBuild; })},
            {"world-script-state",
                    ObjectProbe(&game::GetWorld,
                            [](const World& world) {
                                const auto* script = static_cast<const world_script::WorldScript*>(world.m_worldScript);
                                return script ? static_cast<double>(script->m_currentState) : -1.0;
                            })},
            {"gui-pending", ObjectProbe(&game::GetGame, [](const game::Game& g) { return g.m_guiReloadPending; })},
            {"sandbox", ObjectProbe(&game::GetGame, [](const game::Game& g) { return g.m_sandbox; })},
            {"frame-dt",
                    [](double& out) {
                        const game::GameClock* clock = game::GetGameClock();
                        if (!clock) return false;
                        out = clock->m_deltaTime;
                        return true;
                    }},
            {"frames",
                    [](double& out) {
                        out = static_cast<double>(g_frames);
                        return true;
                    }},
            {"world-hash-checkpoints",
                    [](double& out) {
                        out = GetWorldHashStats().checkpoints;
                        return true;
                    }},
            {"rand-calls",
                    [](double& out) {
                        out = static_cast<double>(GameRandTape().Calls());
                        return true;
                    }},
            {"world-hash-deferred",
                    [](double& out) {
                        out = GetWorldHashStats().deferred;
                        return true;
                    }},
            {"world-hash-pending",
                    [](double& out) {
                        out = GetWorldHashStats().pending;
                        return true;
                    }},
            {"intro-movie-complete",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().complete;
                        return true;
                    }},
            {"intro-movie-skipped",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().skipped;
                        return true;
                    }},
            {"intro-movie-player",
                    [](double& out) {
                        out = static_cast<int>(platform::GetIntroMovieStats().player);
                        return true;
                    }},
            {"intro-movie-frames",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().frames;
                        return true;
                    }},
            {"intro-movie-seconds",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().seconds;
                        return true;
                    }},
            {"intro-movie-duration",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().duration;
                        return true;
                    }},
            {"intro-movie-audio",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().audio;
                        return true;
                    }},
            {"intro-movie-audio-clock",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().audioClock;
                        return true;
                    }},
            {"intro-movie-audio-seconds",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().audioSeconds;
                        return true;
                    }},
            {"intro-movie-audio-open",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().audioOpen;
                        return true;
                    }},
            {"intro-movie-destructed",
                    [](double& out) {
                        out = platform::GetIntroMovieStats().destructCalls;
                        return true;
                    }},
            // The loading-bar fraction ConstructWorld passed to BuildTempMap against world_construct_pis::LoadFraction of the size
            // the scenario script load returned (the .ble size): 0 = identical bits, 1 = different; fails while no world has started yet.
            {"world-load-fade-error",
                    [](double& out) {
                        float fade = 0;
                        bool loadingSave = false;
                        uint32_t size = 0;
                        if (!ui::GetStartupFade(fade, loadingSave) || !world::GetLastWorldScriptLoadSize(size)) return false;
                        const float expected = world_construct_pis::LoadFraction(static_cast<int>(size), loadingSave);
                        out = std::memcmp(&fade, &expected, sizeof(fade)) != 0;
                        return true;
                    }},
            {"world-script-bytes",
                    [](double& out) {
                        uint32_t size = 0;
                        out = world::GetLastWorldScriptLoadSize(size) ? size : -1.0;
                        return true;
                    }},
    };
    return probes;
}

bool ReadReport(const std::string& key, double& out)
{
    std::lock_guard lock(g_reportMutex);
    auto it = g_reportValues.find(key);
    if (it == g_reportValues.end()) return false;
    out = it->second;
    return true;
}

bool Number(const std::vector<std::string>& args, size_t i, double& out)
{
    if (i >= args.size()) return false;
    char* end;
    out = std::strtod(args[i].c_str(), &end);
    return end != args[i].c_str() && *end == 0;
}

void Fail(const std::string& name, const char* what)
{
    OBL_LOG("autotest: FAILED %s (%s)", name.c_str(), what);
}

void ReportValue(const std::string& name, bool ok, double expected, double actual)
{
    if (ok)
        OBL_LOG("autotest: PASS %s (%g)", name.c_str(), actual);
    else
        OBL_LOG("autotest: FAILED %s (oczekiwano %g, jest %g)", name.c_str(), expected, actual);
}

void ReportRange(const std::string& name, bool ok, double min, double max, double actual)
{
    if (ok)
        OBL_LOG("autotest: PASS %s (%g)", name.c_str(), actual);
    else
        OBL_LOG("autotest: FAILED %s (oczekiwano [%g, %g], jest %g)", name.c_str(), min, max, actual);
}

} // namespace

void RegisterAssertions()
{
    log::SetTap(&OnLogMessage);
}

void CountFrame()
{
    ++g_frames;
}

unsigned long long Frames()
{
    return g_frames;
}

void ResetAssertions()
{
    {
        std::lock_guard<std::mutex> lock(g_reportMutex);
        g_reportValues.clear();
    }
    g_passed = 0;
    g_failed = 0;
    g_frames = 0;
}

int PassCount()
{
    return g_passed;
}

int FailureCount()
{
    return g_failed;
}

bool ReadValue(const std::string& name, double& out)
{
    auto probe = Probes().find(name);
    if (probe != Probes().end()) return probe->second(out);
    if (ReadBreakdownProbe(name, out) || ReadBuildingUpdateProbe(name, out) || ReadWorldStateProbe(name, out) ||
            ReadPresenterProbe(name, out) || ReadMusicProbe(name, out) || ReadSamplesProbe(name, out) || ReadGuiProbe(name, out))
        return true;
    return ReadReport(name, out);
}

// expect <name> <value> [tolerance]        live probe or latest report value == value (+- tolerance)
// expect-range <name> <min> <max>          value within [min, max]
// expect-last <key> <value> [tolerance]    same, but only report lines (`key=number`) are looked at
// expect-last-range <key> <min> <max>
// expect-same <keyA> <keyB> [tolerance]    two report values agree (e.g. world totals vs recomputed totals)
bool ExecuteAssertionCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "expect-text") {
        if (args.size() < 2 || !ui::text_table::Current()) {
            Fail(name, "requires a key, a UTF-8 value and a loaded text table");
            return true;
        }
        const auto* actual = reinterpret_cast<const char16_t*>(ui::text_table::Lookup(args[0].c_str()));
        std::string expected = args[1];
        for (size_t i = 2; i < args.size(); ++i) expected += " " + args[i];
        const bool ok = std::u16string_view(actual) == code_page::Utf8ToUtf16(expected);
        ReportValue("text-" + args[0], ok, 1, ok ? 1 : 0);
        return true;
    }
    bool last = name == "expect-last" || name == "expect-last-range";
    bool range = name == "expect-range" || name == "expect-last-range";
    if (name != "expect" && name != "expect-last" && name != "expect-range" && name != "expect-last-range" && name != "expect-same")
        return false;

    if (args.size() < 2) {
        Fail(name, "missing arguments");
        return true;
    }
    double actual;
    if (name == "expect-same") {
        double other, tolerance = 0;
        Number(args, 2, tolerance);
        std::string label = args[0] + " == " + args[1];
        if (!ReadReport(args[0], actual) || !ReadReport(args[1], other)) {
            Fail(label, "missing value");
            return true;
        }
        ReportValue(label, std::fabs(actual - other) <= tolerance, other, actual);
        return true;
    }
    double a, b = 0;
    if (!Number(args, 1, a) || (range && !Number(args, 2, b))) {
        Fail(args[0], "invalid number");
        return true;
    }
    if (!(last ? ReadReport(args[0], actual) : ReadValue(args[0], actual))) {
        Fail(args[0], "missing value");
        return true;
    }
    if (range) {
        ReportRange(args[0], actual >= a && actual <= b, a, b, actual);
    } else {
        double tolerance = 0;
        Number(args, 2, tolerance);
        ReportValue(args[0], std::fabs(actual - a) <= tolerance, a, actual);
    }
    return true;
}

} // namespace obl::test
