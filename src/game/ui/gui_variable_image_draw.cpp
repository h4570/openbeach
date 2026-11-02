#include "core/char16.h"
#include "gui_manager_dialogs.h"
#include "game/objects/building_methods.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/sprite_instance_render.h"
#include "game/ui/text_renderer.h"
#include "game/ui/gui_data.h"
#include "game/ui/save_slots.h"
#include "game/game/settings_store.h"
#include "game/ui/sprite.h"
#include "game/world/world_queries.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"
#include "gui_variable_image.h"
#include "gui_variable_lifecycle.h"

#include "game/game/game_accessors.h"
#include "game/ui/gui_variable_image.h"
#include "game/ui/sound_api.h"
#include "game/objects/building_events_state.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_base.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_building.h"
#include "core/ascii_case.h"
#include "core/call_counter.h"
#include "game/ui/text_table.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace obl::gui {
namespace {

using Widget = ui::GuiVariableImage;
using Instance = ui::SpriteInstance;
using Call = gui_variable_image::Call;
using Env = gui_variable_image::Env;

test::CallCounter g_counter("ui.gui-variable-image"); // counts the draws
test::CallCounter g_undefined("GuiVariableImageUndefined");

class GameEnv final : public Env {
public:
    int ResortRating() const override { return game::GetWorld()->ResortRating(); }
    float AverageTotty() const override { return game::GetWorld()->m_averageTotty; }
    float AverageBeauty() const override { return game::GetWorld()->m_averageBeauty; }
    float AverageSafety() const override { return game::GetWorld()->m_averageSafety; }
    float PowerUse() const override { return game::GetWorld()->m_powerUse; }
    float PowerCapacity() const override { return game::GetWorld()->m_maxPower; }
    float PowerOutput() const override { return game::GetWorld()->m_currentPower; }
    float GameTime() const override { return game::GetWorld()->m_gameTime; }
    uint32_t ScreenWidth() const override { return static_cast<uint32_t>(game::GetRenderer()->m_width); }
    float MusicVolume() const override { return audio::CurrentSound()->musicVolume; }
    const char* SpriteName(gui_variable_image::NameSlot slot) const override
    {
        using Slot = gui_variable_image::NameSlot;
        const char* variable = gui_data::g_GuiSpriteMp3Pips;
        switch (slot) {
        case Slot::Mp3Pips: variable = gui_data::g_GuiSpriteMp3Pips; break;
        case Slot::PowerWarnings: variable = gui_data::g_GuiSpritePowerWarnings; break;
        case Slot::BpPreview: variable = gui_data::g_GuiSpriteBpPreview; break;
        case Slot::BpThumb: variable = gui_data::g_GuiSpriteBpThumb; break;
        case Slot::BpGenderMale: variable = gui_data::g_GuiSpriteBpGenderMale; break;
        case Slot::BpGenderFemale: variable = gui_data::g_GuiSpriteBpGenderFemale; break;
        case Slot::BiSideiconMale: variable = gui_data::g_GuiSpriteBiSideiconMale; break;
        case Slot::BiSideiconFemale: variable = gui_data::g_GuiSpriteBiSideiconFemale; break;
        case Slot::ObjChecks: variable = gui_data::g_GuiSpriteObjChecks; break;
        case Slot::StaffCleaner: variable = gui_data::g_GuiSpriteStaffCleaner; break;
        case Slot::StaffBuilder: variable = gui_data::g_GuiSpriteStaffBuilder; break;
        case Slot::StaffHandyman: variable = gui_data::g_GuiSpriteStaffHandyman; break;
        case Slot::StaffSecurity: variable = gui_data::g_GuiSpriteStaffSecurity; break;
        case Slot::StaffRepMale: variable = gui_data::g_GuiSpriteStaffRepMale; break;
        case Slot::StaffRepFemale: variable = gui_data::g_GuiSpriteStaffRepFemale; break;
        case Slot::StaffLifeguardMale: variable = gui_data::g_GuiSpriteStaffLifeguardMale; break;
        case Slot::StaffLifeguardFemale: variable = gui_data::g_GuiSpriteStaffLifeguardFemale; break;
        }
        return variable;
    }
    gui_variable_image::PowerBlink GetPowerBlink() const override
    {
        const game::Context& context = game::GetContext();
        return {context.powerBarBlinkActive, context.powerBarBlinkNext, context.powerBarBlinkVisible};
    }
    void SetPowerBlink(const gui_variable_image::PowerBlink& blink) const override
    {
        game::Context& context = game::GetContext();
        context.powerBarBlinkActive = blink.active;
        context.powerBarBlinkNext = blink.nextToggle;
        context.powerBarBlinkVisible = blink.visible;
    }

    const void* Provider() const override { return static_cast<ui::GuiManager*>(Gui())->VariableProvider(); }
    float GuestMood(const void* guest) const override { return static_cast<const object::Guest*>(guest)->m_happiness; }
    float StaffMood(const void* staff) const override { return static_cast<const object::Staff*>(staff)->m_happiness; }
    int GeneratorOutput(const void* building) const override
    {
        return object::BuildingGetPowerOutput(static_cast<object::Building*>(const_cast<void*>(building)));
    }
    bool GeneratorFaulted(const void* building) const override
    {
        return static_cast<const object::Building*>(building)->m_RepairState != 0;
    }
    double AverageMaleIntensity(const void* building) const override
    {
        return object::building_api::GetAverageMaleIntensity(static_cast<const object::Building*>(building));
    }
    double AverageFemaleIntensity(const void* building) const override
    {
        return object::building_api::GetAverageFemaleIntensity(static_cast<const object::Building*>(building));
    }
    uint32_t BuildingCount() const override { return game::GetObjectManager()->m_buildings.size(); }
    const void* BuildingAt(uint32_t index) const override { return game::GetObjectManager()->m_buildings[index]; }
    bool EventIntensity(const void* provider, uint32_t slot, bool female, float& intensity) const override
    {
        return gui_variable_image::EventSlotIntensity(static_cast<const object::Building*>(provider)->m_events, slot, female, intensity);
    }
    int SelectedBuilding() const override { return Gui()->m_context[0]; }
    int SelectedScenery() const override { return Gui()->m_context[5]; }
    int BuildingIcon(int building) const override { return BuildingAt(building)->m_Icon; }
    int SceneryIcon(int scenery) const override { return SceneryAt(scenery)->m_Icon; }
    float SceneryRating(int scenery) const override { return SceneryAt(scenery)->m_Rating; }
    int BuildingCommodityCount(int building) const override { return BuildingAt(building)->m_NumCommodities; }
    float BuildingAppeal(int building, int slot, bool female) const override
    {
        return (&BuildingAt(building)->m_CommodityAB[0][0])[slot * 2 + (female ? 1 : 0)];
    }
    int ProviderIcon(const void* building) const override { return static_cast<const object::Building*>(building)->m_definition->m_Icon; }
    uint32_t ObjectiveCount() const override { return game::GetWorld()->m_numObjectives; }
    uint32_t ObjectiveStatus(uint32_t index) const override { return (&game::GetWorld()->m_objectiveStatus[0])[index]; }
    int ProgressLabelWidth() const override
    {
        const Char16* text = ui::text_table::Lookup("OBJ_SCENARIO_PROGRESS");
        return ui::font_text::MeasureWidth(Gui()->m_textRenderers[1], text);
    }
    int StaffClass(const void* staff) const override { return static_cast<int>(static_cast<const object::Base*>(staff)->m_type); }
    bool StaffFemale(const void* staff) const override
    {
        return static_cast<const object::Staff*>(staff)->m_staffDefinition->m_female != 0;
    }
    const char* GuestSprite(const void* guest) const override
    {
        return static_cast<const Instance*>(static_cast<const object::Base*>(guest)->m_sprite)->m_name;
    }
    bool SandboxMode() const override { return game::GetGame()->m_sandbox != 0; }
    int Language() const override { return game::settings_store::Current().m_shownLanguage; }
    int SelectedSaveImage() const override { return ui::save_panel::SelectedImageId(); }
    int ScenarioPicture() const override { return game::GetGame()->m_lastUnlockedScenario; }
    bool HasIdleBuilding(const char* name) const override
    {
        const auto found = object::object_manager::FindObjectsByName(game::GetObjectManager(), name);
        const auto count = static_cast<uint32_t>(found.size());
        for (uint32_t i = 0; i < count; ++i)
            if (!object::building_api::HasBuildJob(static_cast<const object::Building*>(found[i])) &&
                    !object::building_api::HasDemolishJob(static_cast<const object::Building*>(found[i])))
                return true;
        return false;
    }
    // Sprite creation picks the first sprite whose name matches without regard to case, else "dummy".
    gui_variable_image::SpriteMetrics Metrics(const char* name) const override
    {
        const ui::SpriteBank* bank = Bank();
        const ui::Sprite* sprite = Find(bank, name);
        if (!sprite) sprite = Find(bank, "dummy");
        if (!sprite || sprite->m_animCount <= 0) return {};
        const ui::SpriteAnim* anim = sprite->m_anims[0];
        return {anim->m_directions, anim->m_width, anim->m_height};
    }

    static ui::GuiManager* Gui() { return game::GetGuiManager(); }
    static ui::SpriteBank* Bank() { return ui::GetSpriteBank(); }

    static const tables::BuildingDef* BuildingAt(int id) { return game::GetWorldParams()->m_buildings.data() + id; }
    static const tables::SceneryDef* SceneryAt(int id) { return game::GetWorldParams()->m_scenery.data() + id; }

private:
    static const ui::Sprite* Find(const ui::SpriteBank* bank, const char* name)
    {
        for (int i = 0; i < bank->m_spriteCount; ++i)
            if (ascii::EqualsNoCase(bank->m_sprites[i]->m_name, name)) return bank->m_sprites[i];
        return nullptr;
    }
};

// Performs the calls on the game's own sprite instance of one widget.
class LiveCanvas final : public gui_variable_image::Canvas {
public:
    explicit LiveCanvas(Widget& widget) : m_widget(widget) {}

    void Create(const char* name, gui_variable_image::CreateCall call, bool keepInWidget) override
    {
        auto* bank = static_cast<ui::SpriteBank*>(GameEnv::Bank());
        m_instance = call == gui_variable_image::CreateCall::Bank ? bank->CreateObservedInstance(name) : bank->CreateInstance(name);
        if (keepInWidget) m_widget.m_instance = m_instance;
    }
    gui_variable_image::SpriteMetrics Metrics() const override
    {
        const ui::SpriteAnim* anim = m_instance->m_sprite->m_anims[m_instance->m_animIndex];
        return {anim->m_directions, anim->m_width, anim->m_height};
    }
    void SetAnimType(int type) override { m_instance->m_animType = type; }
    void SetFrame(int frame) override { m_instance->m_frameIndex = frame; }
    void SetDirection(int direction) override { m_instance->m_direction = direction; }
    void DrawClipped(int x, int y, uint32_t depth) override { (m_instance)->DrawClipped(x, y, depth, gui_variable_image::kClippedTime); }
    void Draw(int x, int y, int left, int top, int right, int bottom, uint32_t depth) override
    {
        (m_instance)->Draw(x, y, left, top, right, bottom, depth);
    }
    void Release() override { (static_cast<ui::SpriteBank*>(GameEnv::Bank()))->ReleaseInstance(m_instance); }

private:
    Widget& m_widget;
    Instance* m_instance = nullptr;
};

gui_variable_image::Widget View(const Widget* widget)
{
    return {widget->m_key, widget->m_x0, widget->m_y0, widget->m_x1, widget->m_y1, widget->m_object};
}

} // namespace

} // namespace obl::gui

namespace obl::ui {

void GuiVariableImage::Draw()
{
    if (!m_dirty) return;
    const gui::GameEnv env;
    gui::LiveCanvas canvas(*this);
    if (gui_variable_image::DrawWidget(gui::View(this), env, canvas) == gui_variable_image::Result::Undefined) gui::g_undefined.Count();
    m_dirty = 0;
    gui::g_counter.Count();
}

void GuiVariableImage::DrawOverlay()
{
    const gui::GameEnv env;
    gui::LiveCanvas canvas(*this);
    if (gui_variable_image::DrawOverlay(gui::View(this), env, canvas) == gui_variable_image::Result::Undefined) gui::g_undefined.Count();
    gui::g_counter.Count();
}

} // namespace obl::ui
