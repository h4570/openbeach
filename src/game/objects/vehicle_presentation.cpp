#include "core/char16.h"
#include "game/objects/object_vehicle.h"
#include "game/ui/sprite.h"

#include "core/code_page.h"
#include "game/game/game_accessors.h"
#include "game/objects/building_api.h"
#include "game/objects/guest.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"
#include "game/objects/vehicle_presentation.h"
#include "game/objects/object_base.h"
#include "game/game/diagnostics.h"

#include <array>

namespace obl::object {
using game::FatalError;
using game::GetObjectManager;
using game::GetRenderer;
using game::GetWorld;

namespace {
using object::Vehicle;
using ui::SpriteInstance;
namespace present = vehicle::presentation;
test::CallCounter g_draw("objects.vehicle-draw");
void DriveLive(int rounds);
// `call-drive objects.vehicle-presentation-live <rounds>`: every live vehicle drawn and asked for its name and state text.
test::CallCounter g_liveDriver("objects.vehicle-presentation-live", &DriveLive);

struct Env {
    float Time() { return GetWorld()->m_gameTime; }
    float Delta() { return GetWorld()->m_gameDeltaTime; }
    int Zoom() { return GetRenderer()->m_zoomLevel; }
    void BaseDraw(Vehicle& vehicle) { vehicle.object::Base::Draw(); }
    void Draw(SpriteInstance* sprite, float* world, float time) { sprite->DrawInWorld(world, time); }
    void Toned(SpriteInstance* sprite, float* world, float time, float tone, float illness)
    {
        (sprite)->DrawInWorldToned(world, time, tone, illness);
    }
    object::Base* Object(int id) { return object_manager::ObjectById(GetObjectManager(), id); }
    bool Open(object::Base* owner) { return building_api::IsOpen(static_cast<const object::Building*>(owner)); }
    float Illness(object::Base* guest) { return GuestFoodIllness(*reinterpret_cast<object::Guest*>(guest)); }
    double Tone(object::Base* guest) { return GuestPaletteTone(*reinterpret_cast<object::Guest*>(guest)); }
    void Wide(const char* text, Char16* out) { code_page::Widen(text, reinterpret_cast<char16_t*>(out), 256); }
    void Fatal(const char* text) { FatalError("%s", text); }
};

void DriveLive(int rounds)
{
    auto* manager = GetObjectManager();
    for (int r = 0; r < rounds; ++r) {
        for (auto* base : manager->m_others) {
            if (!base || base->m_type != object::ObjectType::Vehicle) continue;
            auto* vehicle = static_cast<Vehicle*>(base);
            vehicle->Vehicle::Draw();
            std::array<Char16, 256> text{};
            vehicle->Vehicle::GetVariableText("VEHICLE_NAME", text.data());
            g_liveDriver.Record(text[0] != 0, "live vehicle name");
            vehicle->Vehicle::GetVariableText("VEHICLE_STATE", text.data());
            g_liveDriver.Record(text[0] != 0, "live vehicle state");
        }
    }
}
} // namespace

void Vehicle::GetVariableText(const char* key, Char16* out)
{
    Env env;
    vehicle::presentation::GetVariableText(*this, key, out, env);
}
void Vehicle::Draw()
{
    g_draw.Count();
    Env env;
    vehicle::presentation::Draw(*this, env);
}
} // namespace obl::object
