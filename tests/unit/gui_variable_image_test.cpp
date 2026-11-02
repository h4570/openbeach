// Sprite calls behind the GuiVariableImage keys (src/game/ui/gui_variable_image.h): which sprite, which frame or direction, where.

#include "unit_test.h"

#include "game/ui/gui_variable_image.h"

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace obl::gui_variable_image;

namespace {

class FakeEnv final : public Env {
public:
    int rating = 0;
    float totty = 0, beauty = 0, safety = 0;
    float use = 0, capacity = 0, output = 0, time = 0;
    uint32_t screenWidth = 800;
    float volume = 0;
    PowerBlink blink{false, 0.0f, true};
    const void* provider = nullptr;
    float guestMood = 0, staffMood = 0;
    int generatorOutput = 0;
    bool generatorFaulted = false;
    std::map<const void*, double> male, female; // intensity by building
    std::vector<const void*> buildings;
    bool hasEvents = true;
    float eventIntensity[4][2] = {};
    mutable uint32_t eventSlot = 99;
    int saveImage = -1, scenarioPicture = 0;
    std::map<std::string, bool> idle;
    std::map<std::string, SpriteMetrics> metrics;
    mutable std::vector<std::string> idleAsked;

    // Draw phase: the picked building / scenery, goals panel, window workers and the sandbox label.
    int selectedBuilding = 0, selectedScenery = 0;
    int buildingIcons[3] = {10, 11, 12}, sceneryIcons[3] = {20, 21, 22};
    float sceneryRating[3] = {0.0f, 0.5f, 0.0f};
    int commodities[3] = {0, 2, 1};
    float appeal[3][2][2] = {{{0, 0}, {0, 0}}, {{0.5f, 0.0f}, {0.25f, 0.0f}}, {{0.0f, 0.4f}, {0, 0}}}; // [building][slot][female]
    int providerIcon = 7;
    uint32_t goals = 0;
    uint32_t goalStatus[4] = {};
    int labelWidth = 60;
    int staffClass = 0xb;
    bool staffFemale = false;
    const char* guestSprite = "fat_bloke";
    bool sandbox = false;
    int language = 0;

    int SelectedBuilding() const override { return selectedBuilding; }
    int SelectedScenery() const override { return selectedScenery; }
    int BuildingIcon(int building) const override { return buildingIcons[building]; }
    int SceneryIcon(int scenery) const override { return sceneryIcons[scenery]; }
    float SceneryRating(int scenery) const override { return sceneryRating[scenery]; }
    int BuildingCommodityCount(int building) const override { return commodities[building]; }
    float BuildingAppeal(int building, int slot, bool female) const override { return appeal[building][slot][female ? 1 : 0]; }
    int ProviderIcon(const void*) const override { return providerIcon; }
    uint32_t ObjectiveCount() const override { return goals; }
    uint32_t ObjectiveStatus(uint32_t index) const override { return goalStatus[index]; }
    int ProgressLabelWidth() const override { return labelWidth; }
    int StaffClass(const void*) const override { return staffClass; }
    bool StaffFemale(const void*) const override { return staffFemale; }
    const char* GuestSprite(const void*) const override { return guestSprite; }
    bool SandboxMode() const override { return sandbox; }
    int Language() const override { return language; }

    int ResortRating() const override { return rating; }
    float AverageTotty() const override { return totty; }
    float AverageBeauty() const override { return beauty; }
    float AverageSafety() const override { return safety; }
    float PowerUse() const override { return use; }
    float PowerCapacity() const override { return capacity; }
    float PowerOutput() const override { return output; }
    float GameTime() const override { return time; }
    uint32_t ScreenWidth() const override { return screenWidth; }
    float MusicVolume() const override { return volume; }
    const char* SpriteName(NameSlot slot) const override
    {
        switch (slot) {
        case NameSlot::Mp3Pips: return "UI3_mp3_pips";
        case NameSlot::PowerWarnings: return "UI3_pp_warnings";
        case NameSlot::BpPreview: return "UI3_bp_preview";
        case NameSlot::BpThumb: return "UI3_bp_thumb";
        case NameSlot::BpGenderMale: return "UI3_bp_gender_male";
        case NameSlot::BpGenderFemale: return "UI3_bp_gender_female";
        case NameSlot::BiSideiconMale: return "UI3_bi_sideicon_male";
        case NameSlot::BiSideiconFemale: return "UI3_bi_sideicon_female";
        case NameSlot::ObjChecks: return "UI3_obj_checks";
        case NameSlot::StaffCleaner: return "ui3_st_ph_cleaner";
        case NameSlot::StaffBuilder: return "ui3_st_ph_builder";
        case NameSlot::StaffHandyman: return "ui3_st_ph_handy";
        case NameSlot::StaffSecurity: return "ui3_st_ph_security";
        case NameSlot::StaffRepMale: return "ui3_st_ph_rep_m";
        case NameSlot::StaffRepFemale: return "ui3_st_ph_rep_f";
        case NameSlot::StaffLifeguardMale: return "ui3_st_ph_lifeg_m";
        case NameSlot::StaffLifeguardFemale: return "ui3_st_ph_lifeg_f";
        }
        return "";
    }
    PowerBlink GetPowerBlink() const override { return blink; }
    void SetPowerBlink(const PowerBlink& value) const override { const_cast<FakeEnv*>(this)->blink = value; }
    const void* Provider() const override { return provider; }
    float GuestMood(const void*) const override { return guestMood; }
    float StaffMood(const void*) const override { return staffMood; }
    int GeneratorOutput(const void*) const override { return generatorOutput; }
    bool GeneratorFaulted(const void*) const override { return generatorFaulted; }
    double AverageMaleIntensity(const void* building) const override { return male.at(building); }
    double AverageFemaleIntensity(const void* building) const override { return female.at(building); }
    uint32_t BuildingCount() const override { return static_cast<uint32_t>(buildings.size()); }
    const void* BuildingAt(uint32_t index) const override { return buildings[index]; }
    bool EventIntensity(const void*, uint32_t slot, bool isFemale, float& intensity) const override
    {
        eventSlot = slot;
        if (!hasEvents) return false;
        intensity = eventIntensity[slot][isFemale ? 1 : 0];
        return true;
    }
    int SelectedSaveImage() const override { return saveImage; }
    int ScenarioPicture() const override { return scenarioPicture; }
    bool HasIdleBuilding(const char* name) const override
    {
        idleAsked.push_back(name);
        const auto found = idle.find(name);
        return found != idle.end() && found->second;
    }
    SpriteMetrics Metrics(const char* name) const override
    {
        const auto found = metrics.find(name);
        return found != metrics.end() ? found->second : SpriteMetrics{3, 20, 10};
    }
};

struct Run {
    Result result;
    RecordingCanvas canvas;
    Run(const char* key, const FakeEnv& env, const void* object = nullptr, int x0 = 100, int y0 = 50, int x1 = 140, int y1 = 90)
        : canvas(env)
    {
        result = DrawOverlay({key, x0, y0, x1, y1, object}, env, canvas);
    }
};

// The frame of the one draw a run made (copied out, so a temporary run may be passed).
template<class R> int FrameOf(const R& run)
{
    return run.canvas.calls.at(1).frame;
}

// The direction each of the five stars is drawn with.
template<class R> std::vector<int> Directions(const R& run)
{
    std::vector<int> directions;
    for (const Call& call : run.canvas.calls)
        if (call.kind == Call::DrawClipped) directions.push_back(call.direction);
    return directions;
}

} // namespace

OBL_TEST_CASE("gui_variable_image.ftol_wraps_like_the_game")
{
    OBL_CHECK_EQ(FloatToIntWrap(3.9), 3);
    OBL_CHECK_EQ(FloatToIntWrap(-3.9), -3);
    OBL_CHECK_EQ(FloatToIntWrap(3e9), -1294967296);
    OBL_CHECK_EQ(FloatToIntWrap(std::numeric_limits<double>::quiet_NaN()), 0);
    OBL_CHECK_EQ(FloatToIntWrap(1e30), 0);
    OBL_CHECK_EQ(Atoi("  -12x"), 0u - 12u);
    OBL_CHECK_EQ(Atoi("7"), 7u);
    OBL_CHECK_EQ(Atoi("abc"), 0u);
}

OBL_TEST_CASE("gui_variable_image.resort_rating_stars_use_half_and_full_thresholds")
{
    FakeEnv env;
    env.rating = 35; // 10 half, 20 full, 30 half, 40 full ...
    Run run("STAR_RATING_UI3", env);
    OBL_CHECK(run.result == Result::Handled);
    OBL_CHECK_EQ(run.canvas.calls.size(), 6u);
    OBL_CHECK(run.canvas.calls[0].kind == Call::Create && run.canvas.calls[0].name == "UI3_BAR_STARS" &&
            run.canvas.calls[0].create == CreateCall::Bank);
    OBL_CHECK((Directions(run) == std::vector<int>{2, 1, 0, 0, 0}));
    OBL_CHECK_EQ(run.canvas.released, 1);
    // Each star sits one sprite width further right on the widget's top edge, on the farthest layer.
    for (int star = 0; star < 5; ++star) {
        const Call& call = run.canvas.calls[1 + star];
        OBL_CHECK_EQ(call.x, 100 + 20 * star);
        OBL_CHECK_EQ(call.y, 50);
        OBL_CHECK_EQ(call.depth, 0u);
        OBL_CHECK_EQ(call.time, kClippedTime);
        OBL_CHECK_EQ(call.animType, 0);
    }
    for (int rating : {0, 9, 10, 19, 20, 99, 100}) {
        env.rating = rating;
        const Run edge("STAR_RATING_UI3", env);
        std::vector<int> expected;
        for (int star = 0; star < 5; ++star) expected.push_back(rating >= 20 + 20 * star ? 2 : rating >= 10 + 20 * star ? 1 : 0);
        OBL_CHECK(Directions(edge) == expected);
    }
    env.rating = 50;
    OBL_CHECK(Run("STAR_RATING_UI2", env).canvas.calls[0].name == "UI2_BAR_STARS");
    OBL_CHECK(Run("STAR_RATING_SMALL", env).canvas.calls[0].name == "UI3_STARS");
}

OBL_TEST_CASE("gui_variable_image.directions_fall_back_to_the_last_one")
{
    FakeEnv env;
    env.rating = 100;
    env.metrics["UI3_BAR_STARS"] = {2, 20, 10};
    OBL_CHECK((Directions(Run("STAR_RATING_UI3", env)) == std::vector<int>{1, 1, 1, 1, 1}));
    env.metrics["UI3_BAR_STARS"] = {1, 20, 10};
    OBL_CHECK((Directions(Run("STAR_RATING_UI3", env)) == std::vector<int>{0, 0, 0, 0, 0}));
    env.metrics["UI3_BAR_STARS"] = {0, 20, 10}; // no directions at all: the setter's fallback is -1
    OBL_CHECK((Directions(Run("STAR_RATING_UI3", env)) == std::vector<int>{-1, -1, -1, -1, -1}));
}

OBL_TEST_CASE("gui_variable_image.guest_averages_scale_to_percent_and_wrap")
{
    FakeEnv env;
    env.totty = 0.5f; // 50 -> two full stars and a half
    OBL_CHECK((Directions(Run("TOTTY_STARS", env)) == std::vector<int>{2, 2, 1, 0, 0}));
    env.beauty = 0.1f;
    OBL_CHECK((Directions(Run("BEAUTY_STARS", env)) == std::vector<int>{1, 0, 0, 0, 0}));
    env.safety = -0.5f; // a negative count is a huge unsigned one: every star full
    OBL_CHECK((Directions(Run("SAFETY_STARS", env)) == std::vector<int>{2, 2, 2, 2, 2}));
    OBL_CHECK(Run("SAFETY_STARS", env).canvas.calls[0].name == "UI3_STARS");
}

OBL_TEST_CASE("gui_variable_image.power_bars_are_drawn_whole")
{
    FakeEnv env;
    Run ui2("POWERBAR", env);
    OBL_CHECK_EQ(ui2.canvas.calls.size(), 2u);
    OBL_CHECK(ui2.canvas.calls[0].name == "UI2_POWERBAR" && ui2.canvas.calls[1].kind == Call::DrawClipped);
    OBL_CHECK_EQ(ui2.canvas.calls[1].direction, 0);
    OBL_CHECK(Run("POWERBAR3", env).canvas.calls[0].name == "UI3_POWERBAR");
}

OBL_TEST_CASE("gui_variable_image.power_output_fills_the_bar_by_the_share_of_capacity")
{
    FakeEnv env;
    env.metrics["UI3_POWERBAR"] = {3, 80, 12};
    env.metrics["UI2_POWERBAR"] = {2, 60, 9};
    env.capacity = 100;
    env.output = 25;
    Run wide("POWER_OUTPUT", env);
    OBL_REQUIRE(wide.canvas.calls.size() == 2u);
    OBL_CHECK(wide.canvas.calls[0].name == "UI3_POWERBAR");
    const Call& draw = wide.canvas.calls[1];
    OBL_CHECK(draw.kind == Call::Draw);
    OBL_CHECK_EQ(draw.direction, 2);
    OBL_CHECK_EQ(draw.x, 100);
    OBL_CHECK_EQ(draw.y, 50);
    OBL_CHECK_EQ(draw.left, 0);
    OBL_CHECK_EQ(draw.top, 0);
    OBL_CHECK_EQ(draw.right, 20); // 80 * 25%
    OBL_CHECK_EQ(draw.bottom, 12);
    OBL_CHECK_EQ(wide.canvas.released, 1);
    env.screenWidth = 640;
    Run narrow("POWER_OUTPUT", env);
    OBL_CHECK(narrow.canvas.calls[0].name == "UI2_POWERBAR");
    OBL_CHECK_EQ(narrow.canvas.calls[1].direction, 1); // two directions: the last one
    OBL_CHECK_EQ(narrow.canvas.calls[1].right, 15);
    OBL_CHECK_EQ(narrow.canvas.calls[1].bottom, 9);
    // No capacity, nothing at all (a NaN capacity too).
    env.capacity = 0;
    OBL_CHECK(Run("POWER_OUTPUT", env).canvas.calls.empty());
    env.capacity = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(Run("POWER_OUTPUT", env).canvas.calls.empty());
    env.capacity = -5;
    OBL_CHECK(Run("POWER_OUTPUT", env).canvas.calls.empty());
}

OBL_TEST_CASE("gui_variable_image.power_use_flashes_while_the_load_exceeds_the_output")
{
    FakeEnv env;
    env.metrics["UI3_POWERBAR"] = {3, 80, 12};
    env.capacity = 100;
    env.output = 50;
    env.use = 25; // half of the output: the bar ends at 40 of the 80 pixels of the output share
    env.time = 10;
    env.blink = {true, 99.0f, false};
    Run calm("POWER_USE", env);
    OBL_REQUIRE(calm.canvas.calls.size() == 2u);
    OBL_CHECK_EQ(calm.canvas.calls[1].direction, 1);
    OBL_CHECK_EQ(calm.canvas.calls[1].right, 20); // 80 * 50% = 40 px of output, half of it
    OBL_CHECK_EQ(calm.canvas.calls[1].bottom, 12);
    OBL_CHECK(!env.blink.active && env.blink.visible); // no overload: shown, blink reset

    // Overload: the first frame starts the blink hidden, then the phase flips each 0.75 s of game time.
    env.use = 150;
    Run first("POWER_USE", env);
    OBL_CHECK(env.blink.active && !env.blink.visible);
    OBL_CHECK_EQ(env.blink.nextToggle, 10.75f);
    OBL_CHECK_EQ(first.canvas.calls.size(), 1u); // created and released, not drawn
    OBL_CHECK_EQ(first.canvas.released, 1);
    env.time = 10.5f;
    Run waiting("POWER_USE", env);
    OBL_CHECK(!env.blink.visible && waiting.canvas.calls.size() == 1u);
    env.time = 11;
    Run flipped("POWER_USE", env);
    OBL_CHECK(env.blink.visible);
    OBL_CHECK_EQ(env.blink.nextToggle, 11.75f);
    OBL_REQUIRE(flipped.canvas.calls.size() == 2u);
    OBL_CHECK_EQ(flipped.canvas.calls[1].right, 80); // load 300% of the output: clamped to the sprite width
    env.time = 12;
    Run hidden("POWER_USE", env);
    OBL_CHECK(!env.blink.visible && hidden.canvas.calls.size() == 1u);
    // No capacity: nothing is created and the blink is left alone.
    env.capacity = 0;
    const PowerBlink kept = env.blink;
    OBL_CHECK(Run("POWER_USE", env).canvas.calls.empty());
    OBL_CHECK(env.blink.active == kept.active && env.blink.visible == kept.visible);
}

OBL_TEST_CASE("gui_variable_image.smileys_map_mood_to_frames_and_skip_the_fifth")
{
    FakeEnv env;
    const int object = 0;
    const struct {
        const char* key;
        const char* sprite;
        bool staff;
    } kinds[] = {{"GUEST_SMILEY", "UI3_gp_smile", false}, {"STAFF_SMILEY", "UI3_gp_smile", true},
            {"BROWSER_SMILEY", "UI3_ob_listsmiles", false}, {"STAFF_BROWSER_SMILEY", "UI3_ob_listsmiles", true}};
    for (const auto& kind : kinds) {
        static const std::pair<float, int> kMoods[] = {{0.0f, 0}, {0.19f, 0}, {0.2f, 1}, {0.79f, 3}, {0.8f, 4}, {1.0f, 4}, {1.2f, 5},
                {std::numeric_limits<float>::quiet_NaN(), 0}};
        for (const auto& [mood, frame] : kMoods) {
            (kind.staff ? env.staffMood : env.guestMood) = mood;
            Run run(kind.key, env, &object);
            OBL_REQUIRE(run.canvas.calls.size() == 2u);
            OBL_CHECK(run.canvas.calls[0].name == kind.sprite);
            OBL_CHECK_EQ(run.canvas.calls[1].frame, frame);
            OBL_CHECK_EQ(run.canvas.calls[1].animType, 3);
            OBL_CHECK_EQ(run.canvas.calls[1].x, 100);
            OBL_CHECK_EQ(run.canvas.calls[1].y, 50);
            OBL_CHECK_EQ(run.canvas.calls[1].depth, 0u);
        }
    }
    env.guestMood = 0.4f;
    const int window = 1;
    env.provider = &window;
    OBL_CHECK_EQ(FrameOf(Run("GUEST_SMILEY", env)), 2);
    env.provider = nullptr;
    const Run none("GUEST_SMILEY", env);
    OBL_CHECK(none.result == Result::Undefined);
    OBL_CHECK(none.canvas.calls.empty());
}

OBL_TEST_CASE("gui_variable_image.generator_status_is_off_working_or_faulted")
{
    FakeEnv env;
    const int object = 0;
    env.generatorOutput = 0;
    Run off("POWERGEN_STATUS", env, &object);
    OBL_REQUIRE(off.canvas.calls.size() == 2u);
    OBL_CHECK(off.canvas.calls[0].name == "UI3_pp_warnings" && off.canvas.calls[0].create == CreateCall::Sprite);
    OBL_CHECK_EQ(off.canvas.calls[1].frame, 0);
    OBL_CHECK_EQ(off.canvas.calls[1].animType, 3);
    env.generatorFaulted = true;
    OBL_CHECK_EQ(FrameOf(Run("POWERGEN_STATUS", env, &object)), 0); // a generator making no power is simply off
    env.generatorOutput = 40;
    OBL_CHECK_EQ(FrameOf(Run("POWERGEN_STATUS", env, &object)), 2);
    env.generatorFaulted = false;
    OBL_CHECK_EQ(FrameOf(Run("POWERGEN_STATUS", env, &object)), 1);
    OBL_CHECK(Run("POWERGEN_STATUS", env).result == Result::Undefined);
}

OBL_TEST_CASE("gui_variable_image.music_volume_pips_fill_from_the_bottom")
{
    FakeEnv env;
    env.volume = 0.34f; // ceil(34) / 10 = 3 lit
    Run run("MP3_VOL_PIPS", env, nullptr, 10, 20, 30, 200);
    OBL_REQUIRE(run.canvas.calls.size() == 11u);
    OBL_CHECK(run.canvas.calls[0].name == "UI3_mp3_pips" && run.canvas.calls[0].create == CreateCall::Sprite);
    for (int pip = 0; pip < 10; ++pip) {
        const Call& call = run.canvas.calls[1 + pip];
        OBL_CHECK_EQ(call.x, 10);
        OBL_CHECK_EQ(call.y, 200 - 12 - 12 * pip);
        OBL_CHECK_EQ(call.frame, pip < 3 ? 1 : 0);
        OBL_CHECK_EQ(call.animType, 3);
        OBL_CHECK_EQ(call.depth, 0u);
    }
    static const std::pair<float, int> kVolumes[] = {
            {0.0f, 0}, {0.001f, 0}, {0.1f, 1}, {0.11f, 1}, {0.5f, 5}, {1.0f, 10}, {std::numeric_limits<float>::quiet_NaN(), 0}};
    for (const auto& [volume, lit] : kVolumes) {
        env.volume = volume;
        Run pips("MP3_VOL_PIPS", env);
        int litCount = 0;
        for (const Call& call : pips.canvas.calls) litCount += call.kind == Call::DrawClipped && call.frame == 1;
        OBL_CHECK_EQ(litCount, lit);
        OBL_CHECK_EQ(pips.canvas.calls.size(), 11u);
    }
    env.volume = -0.25f; // ceil(-25) / 10 truncates to -2: no lit pip, twelve dark ones
    Run below("MP3_VOL_PIPS", env);
    OBL_CHECK_EQ(below.canvas.calls.size(), 13u);
}

OBL_TEST_CASE("gui_variable_image.island_intensity_averages_the_buildings_with_appeal")
{
    FakeEnv env;
    const int a = 0, b = 1, c = 2;
    env.buildings = {&a, &b, &c};
    env.male = {{&a, 0.5}, {&b, 0.0}, {&c, 0.25}};
    env.female = {{&a, 0.0}, {&b, 0.0}, {&c, 0.0}};
    // Male: (50 + 25) / 2 = 37.5 -> 37: three full-ish stars (37 >= 30 half of the second, 40 not reached).
    OBL_CHECK((Directions(Run("ISLAND_MALE_INTENSITY", env)) == std::vector<int>{2, 1, 0, 0, 0}));
    OBL_CHECK(Run("ISLAND_MALE_INTENSITY", env).canvas.calls[0].name == "UI3_STARS");
    // Nobody appeals to women: the average stays 0 and no star is lit.
    OBL_CHECK((Directions(Run("ISLAND_FEMALE_INTENSITY", env)) == std::vector<int>{0, 0, 0, 0, 0}));
    env.buildings.clear();
    OBL_CHECK(Run("ISLAND_MALE_INTENSITY", env).canvas.calls.size() == 6u);
}

OBL_TEST_CASE("gui_variable_image.building_appeal_stars_use_the_strict_thresholds")
{
    FakeEnv env;
    const int window = 0;
    env.provider = &window;
    env.male = {{&window, 0.1}};
    env.female = {{&window, 0.5}};
    Run male("BI_MALE_APPEAL_STARS", env);
    OBL_CHECK(male.canvas.calls[0].name == "UI3_bi_stars");
    // 10 is not above the full-star threshold of 10, but it is above the half one of 0.
    OBL_CHECK((Directions(male) == std::vector<int>{1, 0, 0, 0, 0}));
    OBL_CHECK((Directions(Run("BI_FEMALE_APPEAL_STARS", env)) == std::vector<int>{2, 2, 1, 0, 0}));
    env.male = {{&window, 0.0}};
    OBL_CHECK(Run("BI_MALE_APPEAL_STARS", env).canvas.calls.empty()); // no appeal, no stars
    env.provider = nullptr;
    OBL_CHECK(Run("BI_MALE_APPEAL_STARS", env).result == Result::Undefined);
}

OBL_TEST_CASE("gui_variable_image.event_appeal_reads_the_numbered_show")
{
    FakeEnv env;
    const int window = 0;
    env.provider = &window;
    env.eventIntensity[1][0] = 0.35f;
    env.eventIntensity[2][1] = 0.9f;
    Run male("EVENT_MALE_APPEAL_1", env);
    OBL_CHECK_EQ(env.eventSlot, 1u);
    OBL_CHECK(male.canvas.calls[0].name == "UI3_STARS");
    OBL_CHECK((Directions(male) == std::vector<int>{2, 2, 0, 0, 0})); // 34 on the strict scale: full below 34 are 10 and 30
    // The number follows the prefix of 20 characters for the female key.
    Run female("EVENT_FEMALE_APPEAL_2", env);
    OBL_CHECK_EQ(env.eventSlot, 2u);
    OBL_CHECK((Directions(female) == std::vector<int>{2, 2, 2, 2, 1}));
    // A key that only looks like the prefix is not an event key, and an empty tail is slot 0.
    OBL_CHECK(Run("EVENT_MALE_APPEA", env).result == Result::Unhandled);
    Run empty("EVENT_MALE_APPEAL_", env);
    OBL_CHECK_EQ(env.eventSlot, 0u);
    OBL_CHECK(empty.result == Result::Handled);
    env.hasEvents = false;
    OBL_CHECK(Run("EVENT_MALE_APPEAL_0", env).result == Result::Undefined);
    env.hasEvents = true;
    env.provider = nullptr;
    OBL_CHECK(Run("EVENT_FEMALE_APPEAL_0", env).result == Result::Undefined);
}

OBL_TEST_CASE("gui_variable_image.event_slot_past_the_shows_is_undefined")
{
    struct Slot {
        float m_intensity[2];
    };
    struct Events {
        Slot m_slots[3];
    } events{{{0.1f, 0.2f}, {0.3f, 0.4f}, {0.5f, 0.6f}}};
    float intensity = -1.0f;
    OBL_CHECK(EventSlotIntensity(&events, 2, true, intensity));
    OBL_CHECK_EQ(intensity, 0.6f);
    OBL_CHECK(EventSlotIntensity(&events, 0, false, intensity));
    OBL_CHECK_EQ(intensity, 0.1f);
    // Past the three shows, a number that wrapped, and a building without shows: undefined, the output untouched.
    intensity = -1.0f;
    OBL_CHECK(!EventSlotIntensity(&events, 3, false, intensity));
    OBL_CHECK(!EventSlotIntensity(&events, 0xffffffffu, true, intensity));
    OBL_CHECK(!EventSlotIntensity(static_cast<const Events*>(nullptr), 0, false, intensity));
    OBL_CHECK_EQ(intensity, -1.0f);
}

OBL_TEST_CASE("gui_variable_image.scenario_pictures_and_built_ticks")
{
    FakeEnv env;
    env.saveImage = -1;
    OBL_CHECK(Run("SCENARIO_PIC_LOAD", env).canvas.calls.empty());
    env.saveImage = 2;
    Run load("SCENARIO_PIC_LOAD", env);
    OBL_REQUIRE(load.canvas.calls.size() == 2u);
    OBL_CHECK(load.canvas.calls[0].name == "ui3_fe_is_pics_small");
    OBL_CHECK_EQ(load.canvas.calls[1].frame, 2);
    OBL_CHECK_EQ(load.canvas.calls[1].animType, 3);
    OBL_CHECK_EQ(load.canvas.calls[1].depth, 0xffffu);
    env.scenarioPicture = 5;
    OBL_CHECK_EQ(FrameOf(Run("SCENARIO_PIC_SAVE", env)), 5);

    static const std::pair<const char*, const char*> kBuilt[] = {
            {"WATERCLN_BUILT", "WATERCLN"}, {"STORE_BUILT", "GENSTORE"}, {"CLUB2_BUILT", "CLUB2"}};
    for (const auto& [key, building] : kBuilt) {
        env.idleAsked.clear();
        OBL_CHECK(Run(key, env).canvas.calls.empty()); // not built, or still under construction
        OBL_CHECK(env.idleAsked == std::vector<std::string>{building});
        env.idle[building] = true;
        Run built(key, env);
        OBL_REQUIRE(built.canvas.calls.size() == 2u);
        OBL_CHECK(built.canvas.calls[0].name == "ui3_progress_i_tick");
        OBL_CHECK_EQ(built.canvas.calls[1].animType, 0);
        OBL_CHECK_EQ(built.canvas.calls[1].depth, 0xffffu);
        OBL_CHECK_EQ(built.canvas.released, 1);
    }
}

namespace {

struct DrawRun {
    Result result;
    RecordingCanvas canvas;
    DrawRun(const char* key, const FakeEnv& env, const void* object = nullptr, int x0 = 100, int y0 = 50, int x1 = 140, int y1 = 90)
        : canvas(env)
    {
        result = DrawWidget({key, x0, y0, x1, y1, object}, env, canvas);
    }
};

} // namespace

OBL_TEST_CASE("gui_variable_image.panel_filler_tiles_the_strip_against_the_right_edge")
{
    FakeEnv env;
    DrawRun run("UI3_PANEL_REPEAT", env, nullptr, 10, 5, 100, 25);
    OBL_REQUIRE(run.canvas.calls.size() >= 3u);
    OBL_CHECK(run.canvas.calls[0].name == "UI3_Bar_Filler" && run.canvas.calls[0].create == CreateCall::Bank);
    // x = 10, 42 (< 68), then the closing tile at 100 - 32.
    OBL_CHECK_EQ(run.canvas.calls.size(), 4u);
    OBL_CHECK_EQ(run.canvas.calls[1].x, 10);
    OBL_CHECK_EQ(run.canvas.calls[2].x, 42);
    OBL_CHECK_EQ(run.canvas.calls[3].x, 68);
    OBL_CHECK_EQ(run.canvas.calls[3].y, 5);
    OBL_CHECK_EQ(run.canvas.calls[3].depth, 0xffffu);
    OBL_CHECK_EQ(run.canvas.released, 1);
    DrawRun narrow("UI3_PANEL_REPEAT", env, nullptr, 10, 5, 30, 25); // shorter than a tile: only the closing one
    OBL_CHECK_EQ(narrow.canvas.calls.size(), 2u);
    OBL_CHECK_EQ(narrow.canvas.calls[1].x, -2);
    DrawRun empty("UI3_PANEL_REPEAT", env, nullptr, 10, 5, 10, 25);
    OBL_CHECK(empty.canvas.calls.empty() && empty.result == Result::Handled);
}

OBL_TEST_CASE("gui_variable_image.build_panel_pictures_follow_the_picked_building_and_scenery")
{
    FakeEnv env;
    env.selectedBuilding = 2;
    env.selectedScenery = 1;
    DrawRun large("BUILD_LARGE_PREVIEW", env);
    OBL_REQUIRE(large.canvas.calls.size() == 2u);
    OBL_CHECK(large.canvas.calls[0].name == "UI3_bp_preview" && large.canvas.calls[0].create == CreateCall::Sprite);
    OBL_CHECK_EQ(large.canvas.calls[1].frame, 12);
    OBL_CHECK_EQ(large.canvas.calls[1].animType, 3);
    OBL_CHECK_EQ(large.canvas.calls[1].depth, 0xffffu);
    OBL_CHECK_EQ(FrameOf(DrawRun("BUILD_SCENERY_PREVIEW", env)), 21);
    const int window = 0;
    env.provider = &window;
    DrawRun thumb("BUILD_THUMB", env);
    OBL_CHECK(thumb.canvas.calls[0].name == "UI3_bp_thumb");
    OBL_CHECK_EQ(thumb.canvas.calls[1].frame, 7);
    env.provider = nullptr;
    OBL_CHECK(DrawRun("BUILD_THUMB", env).result == Result::Undefined);
}

OBL_TEST_CASE("gui_variable_image.build_intensity_and_appeal_icons_use_the_average_over_slots")
{
    FakeEnv env;
    env.selectedBuilding = 1; // male (0.5 + 0.25) / 2 = 0.375, female none
    DrawRun male("BUILD_ID_MALE_INTENSITY", env);
    OBL_CHECK(male.canvas.calls[0].name == "UI3_STARS");
    OBL_CHECK((Directions(male) == std::vector<int>{2, 2, 0, 0, 0})); // 37 on the strict scale
    OBL_CHECK(DrawRun("BUILD_ID_FEMALE_INTENSITY", env).canvas.calls.empty());
    OBL_CHECK_EQ(DrawRun("MALE_APPEAL_ICON", env).canvas.calls.size(), 2u);
    OBL_CHECK(DrawRun("MALE_APPEAL_ICON", env).canvas.calls[0].name == "UI3_bp_gender_male");
    OBL_CHECK(DrawRun("FEMALE_APPEAL_ICON", env).canvas.calls.empty());
    env.selectedBuilding = 2; // female 0.4 on one slot
    OBL_CHECK(DrawRun("FEMALE_APPEAL_ICON", env).canvas.calls[0].name == "UI3_bp_gender_female");
    OBL_CHECK(DrawRun("MALE_APPEAL_ICON", env).canvas.calls.empty());
    env.selectedBuilding = 0; // no slots: the average is -1
    OBL_CHECK(DrawRun("BUILD_ID_MALE_INTENSITY", env).canvas.calls.empty());
    OBL_CHECK(DrawRun("MALE_APPEAL_ICON", env).canvas.calls.empty());
    env.selectedScenery = 1;
    DrawRun rating("SP_ITEM_RATING", env); // 50 with the non-strict scale
    OBL_CHECK((Directions(rating) == std::vector<int>{2, 2, 1, 0, 0}));
    env.selectedScenery = 0;
    OBL_CHECK(DrawRun("SP_ITEM_RATING", env).canvas.calls.empty());
}

OBL_TEST_CASE("gui_variable_image.window_building_side_icons")
{
    FakeEnv env;
    const int window = 0;
    env.provider = &window;
    env.male = {{&window, 0.2}};
    env.female = {{&window, 0.0}};
    DrawRun male("BI_MALE_APPEAL_ICON", env);
    OBL_CHECK(male.canvas.calls[0].name == "UI3_bi_sideicon_male" && male.canvas.calls[0].create == CreateCall::Sprite);
    OBL_CHECK_EQ(male.canvas.calls[1].depth, 0xffffu);
    OBL_CHECK(DrawRun("BI_FEMALE_APPEAL_ICON", env).canvas.calls.empty());
    env.female = {{&window, 0.1}};
    OBL_CHECK(DrawRun("BI_FEMALE_APPEAL_ICON", env).canvas.calls[0].name == "UI3_bi_sideicon_female");
    env.provider = nullptr;
    OBL_CHECK(DrawRun("BI_MALE_APPEAL_ICON", env).result == Result::Undefined);
}

OBL_TEST_CASE("gui_variable_image.goal_icons_sit_after_the_progress_caption")
{
    FakeEnv env;
    env.goals = 3;
    env.goalStatus[0] = 0;
    env.goalStatus[1] = 1;
    env.goalStatus[2] = 2;
    env.goalStatus[3] = 9;
    for (int goal = 0; goal < 3; ++goal) {
        DrawRun run(("OBJ_ICON_" + std::to_string(goal)).c_str(), env);
        OBL_REQUIRE(run.canvas.calls.size() == 2u);
        OBL_CHECK(run.canvas.calls[0].name == "UI3_obj_checks");
        OBL_CHECK_EQ(run.canvas.calls[1].frame, goal);
        OBL_CHECK_EQ(run.canvas.calls[1].animType, 3);
        OBL_CHECK_EQ(run.canvas.calls[1].x, 100 + 60 + 10);
    }
    env.goals = 4;
    OBL_CHECK_EQ(FrameOf(DrawRun("OBJ_ICON_3", env)), 0); // a status beyond 2 leaves the first frame
    OBL_CHECK(DrawRun("OBJ_ICON_4", env).canvas.calls.empty()); // not a goal of this scenario
    OBL_CHECK(DrawRun("OBJ_ICON_-1", env).canvas.calls.empty()); // atoi gives 0xffffffff
}

OBL_TEST_CASE("gui_variable_image.photos_follow_the_window_object")
{
    FakeEnv env;
    const int window = 0;
    env.provider = &window;
    const struct {
        int objectClass;
        bool female;
        const char* sprite;
    } staff[] = {{7, false, "ui3_st_ph_lifeg_m"}, {7, true, "ui3_st_ph_lifeg_f"}, {8, true, "ui3_st_ph_handy"},
            {9, false, "ui3_st_ph_rep_m"}, {9, true, "ui3_st_ph_rep_f"}, {0xa, false, "ui3_st_ph_builder"},
            {0xb, true, "ui3_st_ph_cleaner"}, {0xc, false, "ui3_st_ph_security"}};
    for (const auto& worker : staff) {
        env.staffClass = worker.objectClass;
        env.staffFemale = worker.female;
        DrawRun run("STAFF_PHOTO", env);
        OBL_REQUIRE(run.canvas.calls.size() == 2u);
        OBL_CHECK(run.canvas.calls[0].name == worker.sprite && run.canvas.calls[0].create == CreateCall::Sprite);
        OBL_CHECK_EQ(run.canvas.calls[1].animType, 0);
    }
    for (int objectClass : {0, 6, 0xd}) {
        env.staffClass = objectClass;
        OBL_CHECK(DrawRun("STAFF_PHOTO", env).result == Result::Undefined);
    }
    env.guestSprite = "Fit_Bloke";
    DrawRun guest("GUEST_PHOTO", env);
    OBL_CHECK(guest.canvas.calls[0].name == "ui3_gp_guests" && guest.canvas.calls[0].create == CreateCall::Bank);
    OBL_CHECK_EQ(guest.canvas.calls[1].frame, 1);
    int frames[] = {0, 1, 2, 3, 0};
    const char* names[] = {"fat_bloke", "fit_bloke", "GAIL", "shaz", "other"};
    for (int i = 0; i < 5; ++i) {
        env.guestSprite = names[i];
        OBL_CHECK_EQ(FrameOf(DrawRun("GUEST_PHOTO", env)), frames[i]);
    }
    env.provider = nullptr;
    OBL_CHECK(DrawRun("STAFF_PHOTO", env).result == Result::Undefined);
    OBL_CHECK(DrawRun("GUEST_PHOTO", env).result == Result::Undefined);
    env.scenarioPicture = 3;
    DrawRun large("SCENARIO_PIC_LARGE", env);
    OBL_CHECK(large.canvas.calls[0].name == "ui3_fe_is_pics_large");
    OBL_CHECK_EQ(large.canvas.calls[1].frame, 3);
}

OBL_TEST_CASE("gui_variable_image.sandbox_title_follows_the_language")
{
    FakeEnv env;
    OBL_CHECK(DrawRun("SANDBOX_TITLE", env).canvas.calls.empty()); // not a sandbox game
    env.sandbox = true;
    const char* labels[] = {"ui3_fe_sandboxlabel", "ui3_fe_sandboxlabel_f", "ui3_fe_sandboxlabel_i", "ui3_fe_sandboxlabel_g",
            "ui3_fe_sandboxlabel_s", "ui3_fe_sandboxlabel_p", "ui3_fe_sandboxlabel"};
    for (int language = 0; language < 7; ++language) {
        env.language = language;
        DrawRun run("SANDBOX_TITLE", env);
        OBL_REQUIRE(run.canvas.calls.size() == 2u);
        OBL_CHECK(run.canvas.calls[0].name == labels[language] && run.canvas.calls[0].create == CreateCall::Bank);
        OBL_CHECK_EQ(run.canvas.calls[1].animType, 0);
    }
    env.language = -3;
    OBL_CHECK(DrawRun("SANDBOX_TITLE", env).canvas.calls[0].name == "ui3_fe_sandboxlabel");
}

OBL_TEST_CASE("gui_variable_image.every_draw_key_is_handled_and_overlay_keys_are_not")
{
    FakeEnv env;
    const int object = 0;
    env.provider = &object;
    env.male = {{&object, 0.3}};
    env.female = {{&object, 0.3}};
    for (int i = 0; i < DrawKeyCount(); ++i) OBL_CHECK(DrawRun(DrawKey(i), env, &object).result != Result::Unhandled);
    OBL_CHECK(DrawRun("STAR_RATING_UI3", env).result == Result::Unhandled);
    OBL_CHECK(DrawRun("BUILD_THUMBX", env).result == Result::Unhandled);
}

OBL_TEST_CASE("gui_variable_image.unrecognized_keys_draw_nothing")
{
    FakeEnv env;
    for (const char* key : {"", "UNLISTED", "star_rating_ui3", "STAR_RATING", "POWER", "EVENT_"}) {
        const Run run(key, env);
        OBL_CHECK(run.result == Result::Unhandled);
        OBL_CHECK(run.canvas.calls.empty());
    }
}

OBL_TEST_CASE("gui_variable_image.every_listed_key_is_handled")
{
    FakeEnv env;
    const int object = 0;
    env.provider = &object;
    env.male = {{&object, 0.3}};
    env.female = {{&object, 0.3}};
    for (int i = 0; i < OverlayKeyCount(); ++i) {
        const Run run(OverlayKey(i), env, &object);
        OBL_CHECK(run.result != Result::Unhandled);
    }
}
