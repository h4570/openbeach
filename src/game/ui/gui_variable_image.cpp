#include "game/ui/gui_variable_image.h"

#include "core/ascii_case.h"

#include <cmath>
#include <cstring>
#include <iterator>

namespace obl::gui_variable_image {
namespace {

constexpr uint32_t kFarthest = 0; // depth of the bars and stars
constexpr uint32_t kUiLayer = 0xffff; // depth of the other pictures
constexpr float kHundred = 100.0f; // the float constant
constexpr float kBlinkPeriod = 0.75f; // the float constant, game seconds per blink phase
constexpr float kSmileySteps = 5.0f; // the float constant
constexpr int kPipHeight = 12; // distance between two volume pips

int ClampDirection(int wanted, const SpriteMetrics& metrics)
{
    return metrics.directions > wanted ? wanted : metrics.directions - 1;
}

// five stars side by side, each empty, half or full from `value` (0..100). The two differ only
// in the thresholds: the first compares `>=` from 10 and 20, the second `>` from 0 and 10 (so a bare 1 already shows a half).
void DrawStars(Canvas& canvas, const char* sprite, const Widget& widget, uint32_t value, bool strictThresholds)
{
    canvas.Create(sprite, CreateCall::Bank, false);
    const int step = canvas.Metrics().width;
    int x = widget.x0;
    uint32_t half = strictThresholds ? 0 : 10;
    uint32_t full = strictThresholds ? 10 : 20;
    for (int star = 0; star < 5; ++star) {
        int direction = ClampDirection(0, canvas.Metrics());
        if (strictThresholds ? half < value : half <= value) direction = ClampDirection(1, canvas.Metrics());
        if (strictThresholds ? full < value : full <= value) direction = ClampDirection(2, canvas.Metrics());
        canvas.SetDirection(direction);
        canvas.DrawClipped(x, widget.y0, kFarthest);
        x += step;
        full = half + 0x1e;
        half += 0x14;
    }
    canvas.Release();
}

// The full static bar of the HUD, one frame (direction 0).
void DrawBar(Canvas& canvas, const char* sprite, const Widget& widget)
{
    canvas.Create(sprite, CreateCall::Bank, true);
    canvas.SetDirection(ClampDirection(0, canvas.Metrics()));
    canvas.DrawClipped(widget.x0, widget.y0, kFarthest);
    canvas.Release();
}

const char* PowerBarSprite(const Env& env)
{
    return env.ScreenWidth() < 800 ? "UI2_POWERBAR" : "UI3_POWERBAR";
}

// POWER_OUTPUT: the bar filled to output / capacity.
void PowerOutput(const Widget& widget, const Env& env, Canvas& canvas)
{
    const float capacity = env.PowerCapacity();
    if (!(capacity > 0.0f)) return;
    const float output = env.PowerOutput();
    canvas.Create(PowerBarSprite(env), CreateCall::Bank, true);
    const SpriteMetrics metrics = canvas.Metrics();
    const float ratio = static_cast<float>(static_cast<double>(output) / static_cast<double>(capacity));
    const float filled = static_cast<float>(static_cast<double>(metrics.width) * static_cast<double>(ratio));
    canvas.SetDirection(metrics.directions > 2 ? 2 : metrics.directions - 1);
    canvas.Draw(widget.x0, widget.y0, 0, 0, FloatToIntWrap(static_cast<double>(filled)), metrics.height, kFarthest);
    canvas.Release();
}

// POWER_USE: the share of the bar the demand takes, flashing while it exceeds the output.
void PowerUse(const Widget& widget, const Env& env, Canvas& canvas)
{
    const float capacity = env.PowerCapacity();
    if (!(capacity > 0.0f)) return;
    canvas.Create(PowerBarSprite(env), CreateCall::Bank, true);
    const SpriteMetrics metrics = canvas.Metrics();
    const float width = static_cast<float>(metrics.width);
    const float output = env.PowerOutput();
    const float use = env.PowerUse();
    const float outputWidth = static_cast<float>(static_cast<double>(output) / static_cast<double>(capacity) * static_cast<double>(width));
    float useWidth = static_cast<float>(static_cast<double>(use) / static_cast<double>(output) * static_cast<double>(outputWidth));

    PowerBlink blink = env.GetPowerBlink();
    if (!(useWidth > outputWidth)) {
        blink.active = false;
        blink.visible = true;
    } else {
        const float now = env.GameTime();
        if (!blink.active) {
            blink.active = true;
            blink.visible = false;
            blink.nextToggle = static_cast<float>(static_cast<double>(kBlinkPeriod) + static_cast<double>(now));
        } else if (now > blink.nextToggle) {
            blink.nextToggle = static_cast<float>(static_cast<double>(kBlinkPeriod) + static_cast<double>(now));
            blink.visible = !blink.visible;
        }
    }
    env.SetPowerBlink(blink);

    canvas.SetDirection(metrics.directions > 1 ? 1 : metrics.directions - 1);
    if (blink.visible) {
        if (useWidth > width) useWidth = width;
        canvas.Draw(widget.x0, widget.y0, 0, 0, FloatToIntWrap(static_cast<double>(useWidth)), metrics.height, kFarthest);
    }
    canvas.Release();
}

// A face of a smiley strip: mood (0..1) * 5 picks the frame, and the fifth step is skipped (5 -> 4, 6 -> 5 ...).
void Smiley(const Widget& widget, Canvas& canvas, const char* sprite, float mood)
{
    canvas.Create(sprite, CreateCall::Bank, true);
    canvas.SetAnimType(3);
    int frame = FloatToIntWrap(static_cast<double>(mood) * static_cast<double>(kSmileySteps));
    if (frame >= 5) --frame;
    canvas.SetFrame(frame);
    canvas.DrawClipped(widget.x0, widget.y0, kFarthest);
    canvas.Release();
}

// POWERGEN_STATUS: frame 0 when the generator makes no power, 1 working, 2 while it is faulted.
void GeneratorStatus(const Widget& widget, const Env& env, Canvas& canvas, const void* building)
{
    canvas.Create(env.SpriteName(NameSlot::PowerWarnings), CreateCall::Sprite, true);
    canvas.SetAnimType(3);
    int frame = 1;
    if (env.GeneratorOutput(building) == 0)
        frame = 0;
    else if (env.GeneratorFaulted(building))
        frame = 2;
    canvas.SetFrame(frame);
    canvas.DrawClipped(widget.x0, widget.y0, kFarthest);
    canvas.Release();
}

// MP3_VOL_PIPS: ten pips from the bottom up, `ceil(volume * 100) / 10` of them lit (frame 1) and the rest dark (frame 0).
void VolumePips(const Widget& widget, const Env& env, Canvas& canvas)
{
    canvas.Create(env.SpriteName(NameSlot::Mp3Pips), CreateCall::Sprite, true);
    canvas.SetAnimType(3);
    int y = widget.y1 - kPipHeight;
    const float volume = env.MusicVolume();
    const int lit = volume == 0.0f || std::isnan(volume)
            ? 0
            : FloatToIntWrap(std::ceil(static_cast<double>(volume) * static_cast<double>(kHundred))) / 10;
    int dark = 10 - lit;
    canvas.SetFrame(1);
    for (int pip = lit; pip > 0; --pip) {
        canvas.DrawClipped(widget.x0, y, kFarthest);
        y -= kPipHeight;
    }
    canvas.SetFrame(0);
    for (; dark > 0; --dark) {
        canvas.DrawClipped(widget.x0, y, kFarthest);
        y -= kPipHeight;
    }
    canvas.Release();
}

// ISLAND_{MALE,FEMALE}_INTENSITY: mean percentage over the buildings with any appeal for that sex, shown as stars.
void IslandIntensity(const Widget& widget, const Env& env, Canvas& canvas, bool female)
{
    float sum = 0.0f;
    uint32_t counted = 0;
    const uint32_t buildings = env.BuildingCount();
    for (uint32_t i = 0; i < buildings; ++i) {
        const void* building = env.BuildingAt(i);
        const double intensity = female ? env.AverageFemaleIntensity(building) : env.AverageMaleIntensity(building);
        const double percent = intensity * static_cast<double>(kHundred);
        if (percent > 0.0) {
            sum = static_cast<float>(percent + static_cast<double>(sum));
            ++counted;
        }
    }
    if (counted != 0) sum = static_cast<float>(static_cast<double>(sum) / static_cast<double>(counted));
    DrawStars(canvas, "UI3_STARS", widget, static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(sum))), false);
}

// BI_{MALE,FEMALE}_APPEAL_STARS: the window's building, as stars when it appeals at all.
Result BuildingAppeal(const Widget& widget, const Env& env, Canvas& canvas, bool female)
{
    const void* building = env.Provider();
    if (!building) return Result::Undefined;
    const double intensity = female ? env.AverageFemaleIntensity(building) : env.AverageMaleIntensity(building);
    const double percent = intensity * static_cast<double>(kHundred);
    if (!(percent > 0.0)) return Result::Handled;
    DrawStars(canvas, "UI3_bi_stars", widget, static_cast<uint32_t>(FloatToIntWrap(percent)), true);
    return Result::Handled;
}

// EVENT_{MALE,FEMALE}_APPEAL_<n>: the intensity of the n-th show of the window's building.
Result EventAppeal(const Widget& widget, const Env& env, Canvas& canvas, const char* tail, bool female)
{
    const uint32_t slot = Atoi(tail);
    const void* provider = env.Provider();
    float intensity = 0.0f;
    if (!provider || !env.EventIntensity(provider, slot, female, intensity)) return Result::Undefined;
    DrawStars(canvas, "UI3_STARS", widget,
            static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(intensity) * static_cast<double>(kHundred))), true);
    return Result::Handled;
}

void ScenarioPicture(const Widget& widget, Canvas& canvas, int frame)
{
    canvas.Create("ui3_fe_is_pics_small", CreateCall::Bank, true);
    canvas.SetAnimType(3);
    canvas.SetFrame(frame);
    canvas.DrawClipped(widget.x0, widget.y0, kUiLayer);
    canvas.Release();
}

// WATERCLN_BUILT, STORE_BUILT, CLUB2_BUILT: a tick once such a building stands with no build or demolish job.
void BuiltTick(const Widget& widget, const Env& env, Canvas& canvas, const char* building)
{
    if (!env.HasIdleBuilding(building)) return;
    canvas.Create("ui3_progress_i_tick", CreateCall::Bank, true);
    canvas.DrawClipped(widget.x0, widget.y0, kUiLayer);
    canvas.Release();
}

constexpr const char* kKeys[] = {
        "STAR_RATING_UI3",
        "STAR_RATING_UI2",
        "STAR_RATING_SMALL",
        "TOTTY_STARS",
        "BEAUTY_STARS",
        "SAFETY_STARS",
        "POWERBAR",
        "POWERBAR3",
        "POWER_OUTPUT",
        "POWER_USE",
        "GUEST_SMILEY",
        "STAFF_SMILEY",
        "BROWSER_SMILEY",
        "STAFF_BROWSER_SMILEY",
        "POWERGEN_STATUS",
        "MP3_VOL_PIPS",
        "ISLAND_MALE_INTENSITY",
        "ISLAND_FEMALE_INTENSITY",
        "BI_MALE_APPEAL_STARS",
        "BI_FEMALE_APPEAL_STARS",
        "SCENARIO_PIC_LOAD",
        "SCENARIO_PIC_SAVE",
        "WATERCLN_BUILT",
        "STORE_BUILT",
        "CLUB2_BUILT",
        "EVENT_MALE_APPEAL_1",
        "EVENT_FEMALE_APPEAL_2",
};

double AverageAppeal(const Env& env, int building, bool female)
{
    const int slots = env.BuildingCommodityCount(building);
    if (slots == 0) return -1.0;
    double sum = 0.0;
    for (int slot = 0; slot < slots; ++slot) sum += static_cast<double>(env.BuildingAppeal(building, slot, female));
    return sum / static_cast<double>(slots);
}

constexpr int kFillerWidth = 0x20; // UI3_Bar_Filler tile

// Draws the pictured sprite once at the widget corner on the UI layer.
void PlainPicture(const Widget& widget, Canvas& canvas, const char* sprite, CreateCall call)
{
    canvas.Create(sprite, call, true);
    canvas.DrawClipped(widget.x0, widget.y0, kUiLayer);
    canvas.Release();
}

// An icon with an own frame (animation type 3).
void FramePicture(const Widget& widget, Canvas& canvas, const char* sprite, CreateCall call, int frame)
{
    canvas.Create(sprite, call, true);
    canvas.SetAnimType(3);
    canvas.SetFrame(frame);
    canvas.DrawClipped(widget.x0, widget.y0, kUiLayer);
    canvas.Release();
}

constexpr const char* kDrawKeys[] = {
        "UI3_PANEL_REPEAT",
        "BUILD_LARGE_PREVIEW",
        "BUILD_SCENERY_PREVIEW",
        "BUILD_THUMB",
        "BUILD_ID_MALE_INTENSITY",
        "BUILD_ID_FEMALE_INTENSITY",
        "SP_ITEM_RATING",
        "MALE_APPEAL_ICON",
        "FEMALE_APPEAL_ICON",
        "BI_MALE_APPEAL_ICON",
        "BI_FEMALE_APPEAL_ICON",
        "OBJ_ICON_1",
        "STAFF_PHOTO",
        "SCENARIO_PIC_LARGE",
        "GUEST_PHOTO",
        "SANDBOX_TITLE",
};

constexpr const char kEventMale[] = "EVENT_MALE_APPEAL_"; // compared with strncmp over 0x12 bytes
constexpr const char kEventFemale[] = "EVENT_FEMALE_APPEAL_"; // ... and 0x14

bool Is(const char* key, const char* name)
{
    return std::strcmp(key, name) == 0;
}

// The object whose mood the smileys read: the widget's own, else the open window's.
const void* MoodObject(const Widget& widget, const Env& env)
{
    return widget.object ? widget.object : env.Provider();
}

} // namespace

int FloatToIntWrap(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<int64_t>(value)));
}

uint32_t Atoi(const char* text)
{
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = false;
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10u + static_cast<uint32_t>(*text++ - '0');
    return negative ? 0u - value : value;
}

Result DrawOverlay(const Widget& widget, const Env& env, Canvas& canvas)
{
    const char* key = widget.key;
    if (Is(key, "STAR_RATING_UI3")) {
        DrawStars(canvas, "UI3_BAR_STARS", widget, static_cast<uint32_t>(env.ResortRating()), false);
    } else if (Is(key, "STAR_RATING_UI2")) {
        DrawStars(canvas, "UI2_BAR_STARS", widget, static_cast<uint32_t>(env.ResortRating()), false);
    } else if (Is(key, "STAR_RATING_SMALL")) {
        DrawStars(canvas, "UI3_STARS", widget, static_cast<uint32_t>(env.ResortRating()), false);
    } else if (Is(key, "TOTTY_STARS")) {
        DrawStars(canvas, "UI3_STARS", widget,
                static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(env.AverageTotty()) * static_cast<double>(kHundred))), false);
    } else if (Is(key, "BEAUTY_STARS")) {
        DrawStars(canvas, "UI3_STARS", widget,
                static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(env.AverageBeauty()) * static_cast<double>(kHundred))), false);
    } else if (Is(key, "SAFETY_STARS")) {
        DrawStars(canvas, "UI3_STARS", widget,
                static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(env.AverageSafety()) * static_cast<double>(kHundred))), false);
    } else if (Is(key, "POWERBAR")) {
        DrawBar(canvas, "UI2_POWERBAR", widget);
    } else if (Is(key, "POWERBAR3")) {
        DrawBar(canvas, "UI3_POWERBAR", widget);
    } else if (Is(key, "POWER_OUTPUT")) {
        PowerOutput(widget, env, canvas);
    } else if (Is(key, "POWER_USE")) {
        PowerUse(widget, env, canvas);
    } else if (Is(key, "GUEST_SMILEY") || Is(key, "STAFF_SMILEY") || Is(key, "BROWSER_SMILEY") || Is(key, "STAFF_BROWSER_SMILEY")) {
        const void* object = MoodObject(widget, env);
        if (!object) return Result::Undefined;
        const bool staff = Is(key, "STAFF_SMILEY") || Is(key, "STAFF_BROWSER_SMILEY");
        const bool browser = Is(key, "BROWSER_SMILEY") || Is(key, "STAFF_BROWSER_SMILEY");
        Smiley(widget, canvas, browser ? "UI3_ob_listsmiles" : "UI3_gp_smile", staff ? env.StaffMood(object) : env.GuestMood(object));
    } else if (Is(key, "POWERGEN_STATUS")) {
        const void* object = MoodObject(widget, env);
        if (!object) return Result::Undefined;
        GeneratorStatus(widget, env, canvas, object);
    } else if (Is(key, "MP3_VOL_PIPS")) {
        VolumePips(widget, env, canvas);
    } else if (Is(key, "ISLAND_MALE_INTENSITY")) {
        IslandIntensity(widget, env, canvas, false);
    } else if (Is(key, "ISLAND_FEMALE_INTENSITY")) {
        IslandIntensity(widget, env, canvas, true);
    } else if (Is(key, "BI_MALE_APPEAL_STARS")) {
        return BuildingAppeal(widget, env, canvas, false);
    } else if (Is(key, "BI_FEMALE_APPEAL_STARS")) {
        return BuildingAppeal(widget, env, canvas, true);
    } else if (Is(key, "SCENARIO_PIC_LOAD")) {
        const int image = env.SelectedSaveImage();
        if (image != -1) ScenarioPicture(widget, canvas, image);
    } else if (Is(key, "SCENARIO_PIC_SAVE")) {
        ScenarioPicture(widget, canvas, env.ScenarioPicture());
    } else if (Is(key, "WATERCLN_BUILT")) {
        BuiltTick(widget, env, canvas, "WATERCLN");
    } else if (Is(key, "STORE_BUILT")) {
        BuiltTick(widget, env, canvas, "GENSTORE");
    } else if (Is(key, "CLUB2_BUILT")) {
        BuiltTick(widget, env, canvas, "CLUB2");
    } else if (std::strncmp(key, kEventMale, 0x12) == 0) {
        return EventAppeal(widget, env, canvas, key + 0x12, false);
    } else if (std::strncmp(key, kEventFemale, 0x14) == 0) {
        return EventAppeal(widget, env, canvas, key + 0x14, true);
    } else {
        return Result::Unhandled;
    }
    return Result::Handled;
}

Result DrawWidget(const Widget& widget, const Env& env, Canvas& canvas)
{
    const char* key = widget.key;
    if (Is(key, "UI3_PANEL_REPEAT")) {
        // A strip of 32-pixel tiles from the left edge, the last one laid against the right edge.
        if (widget.x0 == widget.x1) return Result::Handled;
        canvas.Create("UI3_Bar_Filler", CreateCall::Bank, true);
        const int limit = widget.x1 - kFillerWidth;
        for (int x = widget.x0; x < limit; x += kFillerWidth) canvas.DrawClipped(x, widget.y0, kUiLayer);
        canvas.DrawClipped(limit, widget.y0, kUiLayer);
        canvas.Release();
    } else if (Is(key, "BUILD_LARGE_PREVIEW")) {
        FramePicture(widget, canvas, env.SpriteName(NameSlot::BpPreview), CreateCall::Sprite, env.BuildingIcon(env.SelectedBuilding()));
    } else if (Is(key, "BUILD_SCENERY_PREVIEW")) {
        FramePicture(widget, canvas, env.SpriteName(NameSlot::BpPreview), CreateCall::Sprite, env.SceneryIcon(env.SelectedScenery()));
    } else if (Is(key, "BUILD_THUMB")) {
        const void* building = env.Provider();
        if (!building) return Result::Undefined;
        FramePicture(widget, canvas, env.SpriteName(NameSlot::BpThumb), CreateCall::Sprite, env.ProviderIcon(building));
    } else if (Is(key, "BUILD_ID_MALE_INTENSITY") || Is(key, "BUILD_ID_FEMALE_INTENSITY")) {
        const double percent =
                AverageAppeal(env, env.SelectedBuilding(), Is(key, "BUILD_ID_FEMALE_INTENSITY")) * static_cast<double>(kHundred);
        if (percent > 0.0) DrawStars(canvas, "UI3_STARS", widget, static_cast<uint32_t>(FloatToIntWrap(percent)), true);
    } else if (Is(key, "SP_ITEM_RATING")) {
        const double percent = static_cast<double>(env.SceneryRating(env.SelectedScenery())) * static_cast<double>(kHundred);
        if (percent > 0.0) DrawStars(canvas, "UI3_STARS", widget, static_cast<uint32_t>(FloatToIntWrap(percent)), false);
    } else if (Is(key, "MALE_APPEAL_ICON")) {
        if (AverageAppeal(env, env.SelectedBuilding(), false) > 0.0)
            PlainPicture(widget, canvas, env.SpriteName(NameSlot::BpGenderMale), CreateCall::Sprite);
    } else if (Is(key, "FEMALE_APPEAL_ICON")) {
        if (AverageAppeal(env, env.SelectedBuilding(), true) > 0.0)
            PlainPicture(widget, canvas, env.SpriteName(NameSlot::BpGenderFemale), CreateCall::Sprite);
    } else if (Is(key, "BI_MALE_APPEAL_ICON") || Is(key, "BI_FEMALE_APPEAL_ICON")) {
        const void* building = env.Provider();
        if (!building) return Result::Undefined;
        const bool female = Is(key, "BI_FEMALE_APPEAL_ICON");
        if ((female ? env.AverageFemaleIntensity(building) : env.AverageMaleIntensity(building)) > 0.0)
            PlainPicture(
                    widget, canvas, env.SpriteName(female ? NameSlot::BiSideiconFemale : NameSlot::BiSideiconMale), CreateCall::Sprite);
    } else if (std::strncmp(key, "OBJ_ICON_", 9) == 0) {
        // The tick, cross or dot in front of goal n of the goals panel, after the "progress" caption.
        const uint32_t goal = Atoi(key + 9);
        if (goal >= env.ObjectiveCount()) return Result::Handled;
        const int shift = env.ProgressLabelWidth() + 10;
        canvas.Create(env.SpriteName(NameSlot::ObjChecks), CreateCall::Sprite, true);
        canvas.SetAnimType(3);
        const uint32_t status = env.ObjectiveStatus(goal);
        if (status <= 2) canvas.SetFrame(static_cast<int>(status));
        canvas.DrawClipped(widget.x0 + shift, widget.y0, kUiLayer);
        canvas.Release();
    } else if (Is(key, "STAFF_PHOTO")) {
        const void* staff = env.Provider();
        if (!staff) return Result::Undefined;
        NameSlot slot;
        switch (env.StaffClass(staff)) {
        case 7: slot = env.StaffFemale(staff) ? NameSlot::StaffLifeguardFemale : NameSlot::StaffLifeguardMale; break;
        case 8: slot = NameSlot::StaffHandyman; break;
        case 9: slot = env.StaffFemale(staff) ? NameSlot::StaffRepFemale : NameSlot::StaffRepMale; break;
        case 0xa: slot = NameSlot::StaffBuilder; break;
        case 0xb: slot = NameSlot::StaffCleaner; break;
        case 0xc: slot = NameSlot::StaffSecurity; break;
        default: return Result::Undefined;
        }
        PlainPicture(widget, canvas, env.SpriteName(slot), CreateCall::Sprite);
    } else if (Is(key, "SCENARIO_PIC_LARGE")) {
        FramePicture(widget, canvas, "ui3_fe_is_pics_large", CreateCall::Bank, env.ScenarioPicture());
    } else if (Is(key, "GUEST_PHOTO")) {
        const void* guest = env.Provider();
        if (!guest) return Result::Undefined;
        // The picture of the four guest kinds follows the name of the guest's sprite.
        const char* name = env.GuestSprite(guest);
        int frame = 0;
        if (ascii::EqualsNoCase(name, "fit_bloke"))
            frame = 1;
        else if (ascii::EqualsNoCase(name, "gail"))
            frame = 2;
        else if (ascii::EqualsNoCase(name, "shaz"))
            frame = 3;
        FramePicture(widget, canvas, "ui3_gp_guests", CreateCall::Bank, frame);
    } else if (Is(key, "SANDBOX_TITLE")) {
        if (!env.SandboxMode()) return Result::Handled;
        static const char* const kLabels[] = {"ui3_fe_sandboxlabel_f", "ui3_fe_sandboxlabel_i", "ui3_fe_sandboxlabel_g",
                "ui3_fe_sandboxlabel_s", "ui3_fe_sandboxlabel_p"};
        const uint32_t index = static_cast<uint32_t>(env.Language()) - 1u;
        PlainPicture(widget, canvas, index < std::size(kLabels) ? kLabels[index] : "ui3_fe_sandboxlabel", CreateCall::Bank);
    } else {
        return Result::Unhandled;
    }
    return Result::Handled;
}

int DrawKeyCount()
{
    return static_cast<int>(std::size(kDrawKeys));
}
const char* DrawKey(int index)
{
    return kDrawKeys[index];
}

int OverlayKeyCount()
{
    return static_cast<int>(std::size(kKeys));
}
const char* OverlayKey(int index)
{
    return kKeys[index];
}

bool Call::operator==(const Call& other) const
{
    return kind == other.kind && create == other.create && name == other.name && animType == other.animType && frame == other.frame &&
            direction == other.direction && x == other.x && y == other.y && depth == other.depth && time == other.time &&
            left == other.left && top == other.top && right == other.right && bottom == other.bottom;
}

void RecordingCanvas::Create(const char* name, CreateCall call, bool)
{
    // Sprite creation falls back to the "dummy" sprite for an unknown name, but the instance keeps the requested one.
    m_name = name;
    m_metrics = m_env.Metrics(name);
    m_animType = 0;
    m_frame = 0;
    m_direction = 0;
    ++created;
    Call entry{};
    entry.kind = Call::Create;
    entry.create = call;
    entry.name = name;
    calls.push_back(entry);
}

void RecordingCanvas::DrawClipped(int x, int y, uint32_t depth)
{
    Call entry{};
    entry.kind = Call::DrawClipped;
    entry.name = m_name;
    entry.animType = m_animType;
    entry.frame = m_frame;
    entry.direction = m_direction;
    entry.x = x;
    entry.y = y;
    entry.depth = depth;
    entry.time = kClippedTime;
    calls.push_back(entry);
}

void RecordingCanvas::Draw(int x, int y, int left, int top, int right, int bottom, uint32_t depth)
{
    Call entry{};
    entry.kind = Call::Draw;
    entry.name = m_name;
    entry.animType = m_animType;
    entry.frame = m_frame;
    entry.direction = m_direction;
    entry.x = x;
    entry.y = y;
    entry.depth = depth;
    entry.left = left;
    entry.top = top;
    entry.right = right;
    entry.bottom = bottom;
    calls.push_back(entry);
}

} // namespace obl::gui_variable_image
