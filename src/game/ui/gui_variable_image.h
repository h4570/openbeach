#pragma once

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace obl::gui_variable_image {

inline constexpr float kClippedTime = 999999.0f;

struct SpriteMetrics {
    int directions;
    int width;
    int height;
};

enum class CreateCall { Sprite, Bank };

enum class NameSlot {
    Mp3Pips, // g_GuiSpriteMp3Pips
    PowerWarnings, // g_GuiSpritePowerWarnings
    BpPreview, // UI3_bp_preview, the large building / scenery picture
    BpThumb, // UI3_bp_thumb
    BpGenderMale, // UI3_bp_gender_male
    BpGenderFemale,
    BiSideiconMale, // UI3_bi_sideicon_male
    BiSideiconFemale,
    ObjChecks, // UI3_obj_checks
    StaffCleaner, // ui3_st_ph_*: the staff photos
    StaffBuilder,
    StaffHandyman,
    StaffSecurity,
    StaffRepMale,
    StaffRepFemale,
    StaffLifeguardMale,
    StaffLifeguardFemale,
};

// The three globals behind the over-capacity blink of POWER_USE.
struct PowerBlink {
    bool active; // g_PowerBarBlinkActive: the load exceeds the output
    float nextToggle; // g_PowerBarBlinkNext: game time of the next flip
    bool visible; // g_PowerBarBlinkVisible: the bar is drawn this phase
};

class Env {
public:
    virtual ~Env() = default;

    virtual int ResortRating() const = 0; // the world's resort rating, 0..100
    virtual float AverageTotty() const = 0; // World::m_averageTotty
    virtual float AverageBeauty() const = 0; // World::m_averageBeauty
    virtual float AverageSafety() const = 0; // World::m_averageSafety
    virtual float PowerUse() const = 0; // World::m_powerUse
    virtual float PowerCapacity() const = 0; // World::m_maxPower
    virtual float PowerOutput() const = 0; // World::m_currentPower
    virtual float GameTime() const = 0; // World::m_gameTime (seconds of game time, the blink clock)
    virtual uint32_t ScreenWidth() const = 0; // Renderer::m_width; below 800 the UI2 power bar is used
    virtual float MusicVolume() const = 0; // Sound::musicVolume, 0..1
    virtual const char* SpriteName(NameSlot slot) const = 0;
    virtual PowerBlink GetPowerBlink() const = 0;
    virtual void SetPowerBlink(const PowerBlink& blink) const = 0;

    // The object whose window is open (GuiManager::m_variableProvider); null without one.
    virtual const void* Provider() const = 0;
    virtual float GuestMood(const void* guest) const = 0; // Guest::m_happiness
    virtual float StaffMood(const void* staff) const = 0; // Staff::m_happiness
    virtual int GeneratorOutput(const void* building) const = 0; // the power the generator makes now
    virtual bool GeneratorFaulted(const void* building) const = 0; // Building::m_RepairState != 0
    virtual double AverageMaleIntensity(const void* building) const = 0; // the building's average intensity for male guests
    virtual double AverageFemaleIntensity(const void* building) const = 0; // and for female guests
    virtual uint32_t BuildingCount() const = 0; // ObjectManager::m_numBuildings
    virtual const void* BuildingAt(uint32_t index) const = 0;

    virtual bool EventIntensity(const void* provider, uint32_t slot, bool female, float& intensity) const = 0;
    virtual int SelectedSaveImage() const = 0; // ui.game-get-selected-save-image-id, -1 none
    virtual int ScenarioPicture() const = 0;

    virtual int SelectedBuilding() const = 0;
    virtual int SelectedScenery() const = 0;
    virtual int BuildingIcon(int building) const = 0; // BuildingDef::m_Icon, the frame of the build pictures
    virtual int SceneryIcon(int scenery) const = 0; // SceneryDef::m_Icon
    virtual float SceneryRating(int scenery) const = 0; // SceneryDef::m_Rating
    virtual int BuildingCommodityCount(int building) const = 0; // BuildingDef::m_NumCommodities
    virtual float BuildingAppeal(int building, int slot, bool female) const = 0; // BuildingDef::m_CommodityAB[slot][female], unchecked slot
    virtual int ProviderIcon(const void* building) const = 0; // the open building's BuildingDef::m_Icon
    virtual uint32_t ObjectiveCount() const = 0; // Number of scenario objectives.
    virtual uint32_t ObjectiveStatus(uint32_t index) const = 0; // World::m_objectiveStatus[index], unchecked
    virtual int ProgressLabelWidth() const = 0; // width of the OBJ_SCENARIO_PROGRESS caption on the second text renderer
    virtual int StaffClass(const void* staff) const = 0; // Entity::m_type of the open window's worker (7..0xc)
    virtual bool StaffFemale(const void* staff) const = 0; // StaffDef::m_female != 0
    virtual const char* GuestSprite(const void* guest) const = 0; // name of the guest's sprite instance
    virtual bool SandboxMode() const = 0; // Game::m_sandbox
    virtual int Language() const = 0; // Language index, English through Polish.
    // Some built building of that type (found by name among the objects) has neither a build nor a demolish job.
    virtual bool HasIdleBuilding(const char* name) const = 0;
    // First animation of the sprite `name` resolves to, with the game's fallback to "dummy" for an unknown name.
    virtual SpriteMetrics Metrics(const char* name) const = 0;
};

class Canvas {
public:
    virtual ~Canvas() = default;
    // `keepInWidget`: the instance pointer is also stored in GuiVariableImage::m_instance (most branches do).
    virtual void Create(const char* name, CreateCall call, bool keepInWidget) = 0;
    virtual SpriteMetrics Metrics() const = 0; // animation 0 of the instance just created
    virtual void SetAnimType(int type) = 0; // SpriteInstance::m_animType (3 = frame set from outside)
    virtual void SetFrame(int frame) = 0; // SpriteInstance::m_frameIndex
    virtual void SetDirection(int direction) = 0; // SpriteInstance::m_direction
    virtual void DrawClipped(int x, int y, uint32_t depth) = 0; // clipped draw with the animation time 999999.0
    virtual void Draw(int x, int y, int left, int top, int right, int bottom, uint32_t depth) = 0; // partial draw of the frame
    virtual void Release() = 0; // hands the instance back to the sprite bank
};

// The widget as the draw reads it.
struct Widget {
    const char* key;
    int x0, y0, x1, y1;
    const void* object;
};

enum class Result {
    Unhandled, // no branch knows the key: nothing is drawn
    Handled, // a branch ran (it may still draw nothing, e.g. a bar without capacity)
    Undefined,
};

// The work of GuiVariableImage::DrawOverlay.
Result DrawOverlay(const Widget& widget, const Env& env, Canvas& canvas);

// The work of GuiVariableImage::Draw (behind the dirty flag, which the caller clears afterwards on every path). Keys: the panel
// filler, the build panel pictures and appeal icons, the goal ticks, the photos of the open window's worker or guest, the
// scenario picture and the sandbox title.
Result DrawWidget(const Widget& widget, const Env& env, Canvas& canvas);
int DrawKeyCount();
const char* DrawKey(int index);

// EVENT_*_APPEAL_<n>: the intensity of show `slot`. A building without shows or a number past its slots leaves the key undefined.
template<class Events> bool EventSlotIntensity(const Events* events, uint32_t slot, bool female, float& intensity)
{
    if (!events || slot >= std::size(events->m_slots)) return false;
    intensity = events->m_slots[slot].m_intensity[female ? 1 : 0];
    return true;
}

// The keys DrawOverlay handles by exact name, plus one example of each prefix family (`EVENT_*_APPEAL_<n>`), for tests.
int OverlayKeyCount();
const char* OverlayKey(int index);

int FloatToIntWrap(double value); // float to int: low 32 bits of the int64 truncation, 0 for NaN / beyond int64
uint32_t Atoi(const char* text); // msvcrt atoi, wrapping

// A canvas that only notes what was asked of it, with the metrics from `Env`.
struct Call {
    enum Kind { Create, DrawClipped, Draw } kind;
    CreateCall create; // Create
    std::string name; // Create: requested name; draws: the instance's name
    int animType, frame, direction; // instance state at the draw
    int x, y;
    uint32_t depth;
    float time; // DrawClipped: the time argument, always 999999.0
    int left, top, right, bottom; // Draw
    bool operator==(const Call& other) const;
};

class RecordingCanvas final : public Canvas {
public:
    explicit RecordingCanvas(const Env& env) : m_env(env) {}
    std::vector<Call> calls;
    int created = 0, released = 0;

    void Create(const char* name, CreateCall call, bool keepInWidget) override;
    SpriteMetrics Metrics() const override { return m_metrics; }
    void SetAnimType(int type) override { m_animType = type; }
    void SetFrame(int frame) override { m_frame = frame; }
    void SetDirection(int direction) override { m_direction = direction; }
    void DrawClipped(int x, int y, uint32_t depth) override;
    void Draw(int x, int y, int left, int top, int right, int bottom, uint32_t depth) override;
    void Release() override { ++released; }

private:
    const Env& m_env;
    std::string m_name;
    SpriteMetrics m_metrics{};
    int m_animType = 0, m_frame = 0, m_direction = 0;
};

} // namespace obl::gui_variable_image

namespace obl::ui {
struct GuiVariableImage;
} // namespace obl::ui
