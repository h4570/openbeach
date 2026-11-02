#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/renderer_api.h"
#include "game/ui/line_draw.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/guest.h"
#include "game/objects/object_guest.h"
#include "game/ui/map_view_access.h"
#include "game/objects/staff_roster.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/renderer_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/tables/world_params_tables.h"
#include "core/log.h"
#include "game/objects/staff_select_draw.h"
#include "game/objects/object_scratch.h"
#include "core/call_counter.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include "game/ui/map_view_math.h"
#include "game/ui/map_view.h"

// Selecting and drawing staff (the logic is staff_select_draw.h): OnSelect of the base class and of the professions, Draw of the
// service staff, the lifeguard and the representative, DrawOverlay and the happiness marker. The drivers draw synthetic states on a
// copy of a real member.
namespace obl::object {
using game::GetGuiManager;
using game::GetObjectManager;
using game::GetRenderer;
using game::GetWorld;
using ui::GetMapView;

namespace {

using object::Staff;
using ui::SpriteInstance;
using Sprite = SpriteInstance;

ui::GuiManager* Gui()
{
    return GetGuiManager();
}

// ---- the Env of the functions ---------------------------------------------------------------------------------------------------------

void DrawHappinessMarker(int32_t* point, float happiness);

struct GameEnv {
    Staff& member;

    void ForgetGuiSelection() { Gui()->m_selection = -1; }
    void WorldToInt(const float* world, int32_t& x, int32_t& y)
    {
        float copy[3] = {world[0], world[1], world[2]};
        ui::MapView::WorldToInt(copy, reinterpret_cast<int*>(&x), reinterpret_cast<int*>(&y));
    }
    uint32_t ViewWidth() const { return static_cast<uint32_t>(GetRenderer()->m_width); }
    uint32_t ViewHeight() const { return static_cast<uint32_t>(GetRenderer()->m_height); }
    float ZoomScale() const { return GetRenderer()->m_zoomScale; }
    void SetCamera(int32_t x, int32_t y) { GetMapView()->SetCameraPos(x, y); }
    void StopCamera() { GetMapView()->StopCamera(); }

    void ClearStaffSelection() { object_manager::ClearStaffSelection(GetObjectManager()); }
    void ShowDialog(const char* name) { Gui()->ShowDialog(name, &member, false); }
    void CloseDialog() { Gui()->CloseDialogByProvider(&member); }
    void Base(bool clicked, bool center) { member.Staff::OnSelect(clicked, center); }

    float Time() const { return GetWorld()->m_gameTime; }
    void InWorld(Sprite* sprite, const float* world, float time) { (sprite)->DrawInWorld(world, time); }
    object::Base* Object(int32_t id) { return object_manager::ObjectById(GetObjectManager(), id); }
    float GuestIllness(object::Base* guest) { return GuestFoodIllness(static_cast<object::Guest&>(*guest)); }
    float GuestTone(object::Base* guest) { return static_cast<float>(GuestPaletteTone(static_cast<object::Guest&>(*guest))); }
    void Toned(Sprite* sprite, const float* world, float time, float tone, float illness)
    {
        (sprite)->DrawInWorldToned(world, time, tone, illness);
    }

    void WorldToScreen(const float* world, int32_t* out)
    {
        float copy[3] = {world[0], world[1], world[2]};
        int buffer[2] = {};
        const int* at = GetMapView()->ProjectWorldPoint(buffer, copy);
        out[0] = at[0];
        out[1] = at[1];
    }
    int32_t DrawOffsetX() { return (staff_draw::SpriteOf(member.m_sprite))->GetDrawOffsetX(); }
    void FirstOpaquePixel(int32_t& x, int32_t& y)
    {
        (staff_draw::SpriteOf(member.m_sprite))->FindFirstOpaque(reinterpret_cast<int*>(&x), reinterpret_cast<int*>(&y));
    }
    void HappinessMarker(int32_t* point, float happiness) { DrawHappinessMarker(point, happiness); }
};

// The marker's own Env (the guest's marker routines on a face sprite made for the call).
struct MarkerEnv {
    void* CreateMood() { return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateInstance(staff_draw::kMoodSprite); }
    void ReleaseMood(void* sprite) { (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(sprite); }
    float Zoom() { return GetRenderer()->m_zoomScale; }
    uint32_t Color(uint32_t r, uint32_t g, uint32_t b) { return ui::renderer::PackColor(GetRenderer(), r, g, b); }
    void Line(int x, int y, int x1, int y1, uint32_t color) { ui::pixel_primitives::DrawLine(GetRenderer(), x, y, x1, y1, color); }
    void Clipped(Sprite* s, int x, int y) { (s)->DrawClipped(x, y, 0xffff, 999999.0f); }
};

// ---- OnSelect ----------------------------------------------------------------------------------------------------------------------

struct BaseSelect {
    using Object = Staff;
    static constexpr object::ObjectType kType =
            object::ObjectType::ServiceStaff; // not looked up: the base class takes the first member of any class
    static constexpr bool kWrapper = false;
    static void Run(Object& member, bool, bool center, GameEnv& env) { staff_draw::OnSelect(member, center, env); }
};
struct LifeguardSelect {
    using Object = object::Lifeguard;
    static constexpr object::ObjectType kType = object::ObjectType::Lifeguard;
    static constexpr bool kWrapper = true;
    static void Run(Object& member, bool clicked, bool center, GameEnv& env)
    {
        staff_draw::ProfessionOnSelect(member, clicked, center, staff_draw::kLifeguardDialog, env);
    }
};
struct RepSelect {
    using Object = object::Rep;
    static constexpr object::ObjectType kType = object::ObjectType::Rep;
    static constexpr bool kWrapper = true;
    static void Run(Object& member, bool clicked, bool center, GameEnv& env)
    {
        staff_draw::ProfessionOnSelect(member, clicked, center, staff_draw::kRepDialog, env);
    }
};

struct CleanerSelect {
    using Object = object::Cleaner;
    static constexpr object::ObjectType kType = object::ObjectType::Cleaner;
    static constexpr bool kWrapper = true;
    static void Run(Object& member, bool clicked, bool center, GameEnv& env)
    {
        staff_draw::ProfessionOnSelect(member, clicked, center, staff_draw::kCleanerDialog, env);
    }
};
struct MechanicSelect {
    using Object = object::Mechanic;
    static constexpr object::ObjectType kType = object::ObjectType::Mechanic;
    static constexpr bool kWrapper = true;
    static void Run(Object& member, bool clicked, bool center, GameEnv& env)
    {
        staff_draw::ProfessionOnSelect(member, clicked, center, staff_draw::kMechanicDialog, env);
    }
};
struct SecurityGuardSelect {
    using Object = object::SecurityGuard;
    static constexpr object::ObjectType kType = object::ObjectType::SecurityGuard;
    static constexpr bool kWrapper = true;
    static void Run(Object& member, bool clicked, bool center, GameEnv& env)
    {
        staff_draw::ProfessionOnSelect(member, clicked, center, staff_draw::kSecurityDialog, env);
    }
};
struct BuilderSelect {
    using Object = object::Builder;
    static constexpr object::ObjectType kType = object::ObjectType::Builder;
    static constexpr bool kWrapper = true;
    static void Run(Object& member, bool clicked, bool center, GameEnv& env)
    {
        staff_draw::ProfessionOnSelect(member, clicked, center, staff_draw::kBuilderDialog, env);
    }
};

// Only the low byte of each flag counts.
template<class D> void Select(typename D::Object& member, uint32_t clicked, uint32_t center)
{
    GameEnv env{member};
    D::Run(member, (clicked & 0xff) != 0, (center & 0xff) != 0, env);
}

// ---- Draw and DrawOverlay ------------------------------------------------------------------------------------------------------------

template<class D> void DriveDraw(int rounds);

struct ServiceDrawD {
    using Object = object::ServiceStaff;
    inline static test::CallCounter calls{"objects.staff-draw.service", &DriveDraw<ServiceDrawD>};
    static void Run(Object& member, GameEnv& env) { staff_draw::ServiceDraw(member, env); }
};
struct LifeguardDrawD {
    using Object = object::Lifeguard;
    inline static test::CallCounter calls{"objects.staff-draw.lifeguard", &DriveDraw<LifeguardDrawD>};
    static void Run(Object& member, GameEnv& env) { staff_draw::LifeguardDraw(member, env); }
};
struct RepDrawD {
    using Object = object::Rep;
    inline static test::CallCounter calls{"objects.staff-draw.rep", &DriveDraw<RepDrawD>};
    static void Run(Object& member, GameEnv& env) { staff_draw::RepDraw(member, env); }
};
struct OverlayD {
    using Object = Staff;
    inline static test::CallCounter calls{"objects.staff-overlay", &DriveDraw<OverlayD>};
    static void Run(Object& member, GameEnv& env) { staff_draw::DrawOverlay(member, env); }
};

template<class D> void Draw(typename D::Object& member)
{
    D::calls.Count();
    GameEnv env{member};
    D::Run(member, env);
}

void DriveMarker(int rounds);
test::CallCounter g_marker("objects.staff-happiness-marker", &DriveMarker);

// The point is moved to the marker's left edge.
void DrawHappinessMarker(int32_t* point, float happiness)
{
    MarkerEnv env;
    g_marker.Count();
    staff_draw::HappinessMarker(point, happiness, env);
}

// ---- call-drive ------------------------------------------------------------------------------------------------------------------------

struct Rng {
    uint32_t seed;
    uint32_t Next() { return (seed = seed * 1664525u + 1013904223u) >> 8; }
};

Staff* MemberOfClass(object::ObjectType type)
{
    for (Staff* member : StaffOfEveryClass())
        if (member->m_type == type) return member;
    return nullptr;
}

constexpr int32_t kLevels[] = {0, 1, 2, 3, -1, 258, INT32_MIN};

// Draw and DrawOverlay: the member at every state of what the function reads; the attached sprite is a copy of the member's own with a shorter or longer
// direction count than the member's direction (the clamp), the carried guest is a real one. Sprites are put back.
template<class D> void DriveDraw(int rounds)
{
    using T = typename D::Object;
    object::ObjectType type = object::ObjectType::Rep;
    if constexpr (std::is_same_v<T, object::Lifeguard>) type = object::ObjectType::Lifeguard;
    if constexpr (std::is_same_v<T, object::ServiceStaff>) type = object::ObjectType::ServiceStaff;
    Staff* real = std::is_same_v<D, OverlayD> ? FirstStaffMember() : MemberOfClass(type);
    if (!real || !real->m_sprite) {
        OBL_LOG("autotest: FAILED call-drive %s: needs a member with a sprite (member %d)", D::calls.Name(), real ? 1 : 0);
        return;
    }
    object::Manager* manager = GetObjectManager();
    object::Scratch<T> copy(*static_cast<T*>(real)); // shares the member's sprites: its teardown must not run
    Sprite* body = staff_draw::SpriteOf(copy->m_sprite);
    const Sprite savedBody = *body;
    Sprite attached = *body;
    Sprite guestBody{};
    const object::Base* guest = manager->m_guests.size() ? manager->m_guests[0] : nullptr;
    tables::StaffDef savedDefinition{};
    tables::StaffDef* definition = const_cast<tables::StaffDef*>(copy->m_staffDefinition);
    if (definition) savedDefinition = *definition;
    const float savedZoom = GetRenderer()->m_zoomScale;
    Rng rng{0x5e1ed7u};
    for (int round = 0; round < rounds; ++round) {
        // The marker reads the pixels of the direction the sprite is in: only the clamp of the attached sprite needs an arbitrary one.
        const int32_t directions = std::max<int32_t>(1, staff_draw::AnimOf(*body).m_directions);
        body->m_direction =
                std::is_same_v<D, OverlayD> ? static_cast<int32_t>(rng.Next() % directions) : static_cast<int32_t>(rng.Next() % 14) - 1;
        copy->m_insideBuilding = round % 5 == 0;
        copy->m_visible = round % 7 != 0;
        copy->m_SelectLevel = kLevels[round % std::size(kLevels)];
        copy->m_happiness = float(rng.Next() % 130) / 100.0f - 0.15f;
        copy->m_WorldXYZ[0] = 100.0f + float(rng.Next() & 0xffff) / 65536.0f * 3800.0f;
        copy->m_WorldXYZ[1] = 100.0f + float(rng.Next() & 0xffff) / 65536.0f * 7800.0f;
        GetRenderer()->m_zoomScale = (round / 3) % 3 == 0 ? 0.5f : (round / 3) % 3 == 1 ? 1.0f : 2.0f;
        if constexpr (std::is_same_v<T, object::ServiceStaff>) {
            if (definition) definition->m_Sprite = round % 2 ? "NONE" : "BARMAN";
        }
        if constexpr (std::is_same_v<T, object::Lifeguard> || std::is_same_v<T, object::Rep>) {
            attached.m_direction = static_cast<int32_t>(rng.Next() % 14) - 1;
            copy->m_attachedSprite = round % 3 ? &attached : nullptr;
            copy->m_AttachedOffsetX = round % 4 ? int32_t(rng.Next() % 40) : -7;
            copy->m_AttachedOffsetY = round % 4 ? int32_t(rng.Next() % 40) : -13;
        }
        if constexpr (std::is_same_v<T, object::Lifeguard>) {
            copy->m_DrawGuest = guest && round % 2 == 0;
            if (guest) copy->m_saving_id = static_cast<int32_t>(guest->m_id);
        }
        object::Draw<D>(*copy);
    }
    *body = savedBody;
    if (definition) *definition = savedDefinition;
    GetRenderer()->m_zoomScale = savedZoom;
    (void)guestBody;
}

void DriveMarker(int rounds)
{
    ui::Renderer* renderer = GetRenderer();
    const float savedZoom = renderer->m_zoomScale;
    // Within the game's 0..1: a mood of 1.2 or -0.2 would select frame 5 or -1, past the face sprite's frames.
    constexpr float values[] = {0.0f, 0.1f, 0.2f, 0.19999999f, 0.5f, 0.8f, 0.99f, 1.0f, 0.4f, 0.6f};
    Rng rng{0x5e1ee7u};
    for (int round = 0; round < rounds; ++round) {
        renderer->m_zoomScale = (round / 10) % 3 == 0 ? 0.5f : (round / 10) % 3 == 1 ? 1.0f : 2.0f;
        int32_t point[2] = {int32_t(rng.Next() % 2000) - 1000, int32_t(rng.Next() % 2000) - 1000};
        DrawHappinessMarker(point, values[round % std::size(values)]);
    }
    renderer->m_zoomScale = savedZoom;
}

} // namespace

void Staff::DrawOverlay()
{
    object::Draw<OverlayD>(*this);
}
void Staff::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    Select<BaseSelect>(*this, clicked, centerCamera);
}
void ServiceStaff::Draw()
{
    object::Draw<ServiceDrawD>(*this);
}
void Builder::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    Select<BuilderSelect>(*this, clicked, centerCamera);
}
void Cleaner::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    Select<CleanerSelect>(*this, clicked, centerCamera);
}
void Lifeguard::Draw()
{
    object::Draw<LifeguardDrawD>(*this);
}
void Lifeguard::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    Select<LifeguardSelect>(*this, clicked, centerCamera);
}
void Mechanic::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    Select<MechanicSelect>(*this, clicked, centerCamera);
}
void Rep::Draw()
{
    object::Draw<RepDrawD>(*this);
}
void Rep::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    Select<RepSelect>(*this, clicked, centerCamera);
}
void SecurityGuard::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    Select<SecurityGuardSelect>(*this, clicked, centerCamera);
}
} // namespace obl::object
