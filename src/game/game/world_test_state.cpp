#include "core/char16.h"
#include "core/ascii_case.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/music_player.h"
#include "game/ui/renderer_api.h"
#include "game/objects/object_base.h"
#include "game/ui/input.h"
#include "game/ui/sound_api.h"
#include "game/ui/loop_sound_control.h"
#include "game/ui/sprite_bank.h"
#include "game/game/world_test_state.h"
#include "game/objects/object_vehicle.h"
#include "game/ui/gui_radar.h"
#include "game/game/game_api.h"
#include "game/world/world_queries_api.h"
#include "game/game/settings_store.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/object_manager_state.h"
#include "game/weather/weather.h"
#include "game/ui/gui_layouts_runtime.h"
#include "game/ui/gui_tooltip.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/feedback/feedback_manager.h"
#include "game/ferry/ferry.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/game/host_text.h"
#include "game/world/world_factory.h"
#include "game/world/world_lifecycle.h"
#include "game/world/world_step.h"
#include "game/world/world_view.h"
#include "game/game/game_states_logic.h"
#include "game/world/scenario_load.h"
#include "game/ui/sound_types.h"
#include "game/game/game.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/map_view.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/audio_types.h"
#include "game/ui/sprite.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include "game/world/world_params.h"
#include "game/ui/text_table.h"
#include "game/ui/map_view_blocks.h"
#include "game/ui/map_view_access.h"

namespace obl::game {
namespace {
using ferry::Arrival;
using messages::Message;
using object::Vehicle;

// Call counters (`call-report <name>`).
test::CallCounter g_calls("WorldTestState.calls"), g_arrivals("WorldTestState.arrivals");

// ---- small accessors ----

audio::Sound* SoundObject()
{
    return audio::CurrentSound();
}
ui::GuiManager* Gui()
{
    return GetGuiManager();
}
void* SpriteBank()
{
    return ui::GetSpriteBank();
}

void FadeChannel(int channel, float volume, float seconds)
{
    SoundObject()->FadeChannelTo(channel, volume, seconds);
}
void SetChannelVolume(int channel, float volume)
{
    SoundObject()->SetChannelVolume(channel, volume);
}
int PlayLoop(const char* name)
{
    return SoundObject()->Play(name, 2, 0.0f);
}
void StopAllSamples()
{
    SoundObject()->StopAllSamples();
}
void SetSoftwareCursor(bool enabled)
{
    (reinterpret_cast<ui::GuiManager*>(Gui()))->SetSoftwareCursorEnabled(enabled);
}
void ClearField15c()
{
    Gui()->HideProgressBar();
}
void SetField15cUnlessMenu()
{
    gui::SetProgressVisibleUnlessSandbox(Gui());
}
void* MusicPlayerOf()
{
    return Gui()->m_musicPlayer;
}
void SetNight(int night)
{
    static_cast<audio::MusicPlayer*>(MusicPlayerOf())->SetNight(night);
}
void WorldUpdate(world_data::World* world)
{
    world::Update(*world);
}

// ---- the helpers (MusicPlayer_*, Grid_*, GuestObject_*, Vehicle_*, WorldTestState_*) ----

// The stop-stream message: the playlist (first field) stops the stream.
void StopMusicStream()
{
    (*static_cast<audio::Playlist**>(MusicPlayerOf()))->StopStream();
}

// The restart-playlist message: the playlist restart stays the game's.
void RestartPlaylist()
{
    (*static_cast<audio::Playlist**>(MusicPlayerOf()))->RestartTimeOfDay();
}

// The set-control-mode message: mode 0 lets the day/night switch run (the sun / moon buttons are reset), mode 1 locks it (they are
// disabled and every track plays).
void SetControlMode(int mode)
{
    auto* player = static_cast<audio::MusicPlayer*>(MusicPlayerOf());
    player->manual = mode;
    if (mode == 0) {
        reinterpret_cast<ui::GuiManager*>(Gui())->ResetActionButtons("MP3_Sun");
        reinterpret_cast<ui::GuiManager*>(Gui())->ResetActionButtons("MP3_Moon");
        player->playlist->modeFilter = player->night == 0 ? 0 : 1;
    } else if (mode == 1) {
        reinterpret_cast<ui::GuiManager*>(Gui())->DisableActionButtons("MP3_Sun");
        reinterpret_cast<ui::GuiManager*>(Gui())->DisableActionButtons("MP3_Moon");
        player->playlist->modeFilter = 2;
    }
}

// The guest's opinion initialization: the seven opinions all start at `value` (the fourth bar never below one half), the happiness is their mean.
void InitOpinions(object::Guest* guest, float value)
{
    for (float& opinion : guest->m_opinion) opinion = value;
    if (!(value >= 0.5f)) guest->m_opinion[4] = 0.5f;
    double sum = 0.0;
    for (float opinion : guest->m_opinion) sum += opinion;
    guest->m_happiness = static_cast<float>(sum * static_cast<double>(0.14285715f));
}

// The vehicle's set-unload-point step: the unload point and the sprite direction the unload direction maps to.
void SetUnloadPoint(Vehicle* vehicle, uint8_t point)
{
    vehicle->m_UnloadPoint = point;
    auto* sprite = static_cast<ui::SpriteInstance*>(vehicle->m_sprite);
    const int directions = sprite->m_sprite->m_anims[sprite->m_animIndex]->m_directions;
    int direction = vehicle->m_unloadDir % directions;
    if (direction >= directions)
        direction = directions - 1; // never taken (the remainder is below the divisor); no log line is written here
    sprite->m_direction = direction;
}

// The world-test state's crowd and cricket update: the crowd follows the number of guests drawn, the crickets answer at night when the sea is quiet.
void UpdateCrowdCricket(WorldTestState* state, float seaVolume)
{
    auto* scripted = GetScriptedSound();
    const float maxVolume = scripted->m_maxVolume;
    const float seconds = scripted->m_fadeTime;
    const auto drawn = static_cast<uint32_t>(ui::GetMapView()->m_guestObjectsDrawn);
    const double product = static_cast<double>(drawn) / static_cast<double>(scripted->m_maxNumber) * static_cast<double>(maxVolume);
    float volume = static_cast<float>(product);
    if (product > static_cast<double>(maxVolume)) volume = maxVolume;
    const double change = std::fabs(static_cast<double>(volume) - static_cast<double>(state->m_crowdVolume));
    if (!(change >= 1.1920928955078125e-07)) return;
    FadeChannel(state->m_crowdChannel, volume, seconds);
    state->m_crowdVolume = volume;
    const float hour = GetWorld()->m_gameDayTime;
    const bool night = hour > 21.0f || !(hour >= 5.0f);
    if (night && !(seaVolume >= 0.1f))
        FadeChannel(state->m_cricketChannel, static_cast<float>(static_cast<double>(0.4f) - static_cast<double>(volume)), seconds);
    else
        FadeChannel(state->m_cricketChannel, 0.0f, seconds);
}

// The world-test state's random bird sound: by day, zoomed out and with a quiet crowd, now and then a bird near the listener.
void PlayRandomBird(WorldTestState* state, float zoomOut)
{
    const float hour = state->m_world->m_gameDayTime;
    if (!(hour > 5.0f)) return;
    if (!(hour < 21.0f)) return;
    if (!(zoomOut > 0.9f)) return;
    if (Rand() % 2000 <= 0x7c1) return;
    audio::Sound* sound = SoundObject();
    float position[3] = {sound->position[0], sound->position[1], sound->position[2]};
    const float radius = sound->maxDistance;
    const float edgeX = static_cast<float>(static_cast<double>(position[0]) + static_cast<double>(radius));
    const int range = static_cast<int>(radius + radius);
    position[0] = static_cast<float>(static_cast<double>(edgeX) - static_cast<double>(Rand() % range));
    const float edgeY = static_cast<float>(static_cast<double>(position[1]) + static_cast<double>(radius));
    position[1] = static_cast<float>(static_cast<double>(edgeY) - static_cast<double>(Rand() % range));
    // The bird used to be drawn after the crowd check. The crowd volume follows MapView::m_guestObjectsDrawn, which only a render
    // updates, so the number of simulation draws depended on rendering (a `step` batch boundary changed a replay). The bird is
    // drawn first so the count depends only on the world and the camera (docs/testing/determinism.md).
    static const char* const kBirds[3] = {"bird01", "bird02", "bird03"};
    const char* bird = kBirds[Rand() % 3];
    const double room = 0.75 - static_cast<double>(state->m_crowdVolume);
    if (room < 0.0 || std::isnan(room)) return;
    audio::CurrentLoopSounds()->PlaySpriteSound(bird, position, static_cast<float>(room), 1, -1);
}

// ---- lifecycle ----

// The shared tail of Exit and the destructor: stop the sounds and the music, drop the radar flag and the message balloon, destroy the
// world, zoom back to 1.
void LeaveGame(WorldTestState* state)
{
    StopAllSamples();
    StopMusicStream();
    Gui()->m_radarEnabled = 0;
    gui::DropGuiMessage(); // our fix: the next world starts without the old balloon
    ClearField15c();
    state->m_world.reset();
    ui::renderer::SetZoomLevel(GetRenderer(), 1);
    state->m_entered = 0;
}

const char* LayoutScript()
{
    return gui::gui_layouts::FileFor(obl::game_states::kInGameLayout, settings_store::Current().m_shownLanguage);
}

// The part Enter (a fresh game) and FinishLoad (a loaded save) share, from the GUI mode to the sounds: GUI script, ambience channels at volume 0,
// the music player's control mode and the radar flag.
void StartAmbience(WorldTestState* state)
{
    Gui()->SetMode(4);
    const char* script = state->GetGuiScript();
    game_api::LoadGuiScript(script);
    gui::UpdateRadarViewRectDirect(Gui());
    StopAllSamples();
    state->m_wavesChannel = PlayLoop("waves_loop");
    state->m_crowdChannel = PlayLoop("crowdbig");
    state->m_cricketChannel = PlayLoop("cricket_loop");
    state->m_crowdVolume = 0.0f;
    SetChannelVolume(state->m_wavesChannel, 0.0f);
    SetChannelVolume(state->m_crowdChannel, 0.0f);
    SetChannelVolume(state->m_cricketChannel, 0.0f);
    SetSoftwareCursor(true);
    InputClearButtonState(*GetInput());
}

void FinishAmbience()
{
    SetControlMode(settings_store::Current().m_playlistMode == 0 ? 1 : 0);
}

void EnterWorld(WorldTestState* state)
{
    game::Game* game = GetGame();
    state->m_entered = 1;
    SetSoftwareCursor(false);
    // The scenario by its id (s3, sandbox3): the menu index the save holds names the entry of the campaign order the pack's campaign.json gives.
    scenario_load::Campaign campaign;
    LoadCampaignOrFatal(campaign);
    const std::string scenarioId = campaign.Id(game->m_lastUnlockedScenario, game->m_sandbox != 0);
    if (state->m_loadingSave) {
        char screen[128];
        ::obl::ui::host_text::PrintNarrow(screen, "data/screens/loading_%d.tga", Rand() % 2 + 1);
        game_api::DrawFullscreenTga(screen);
    }
    SetNight(0);
    state->m_world.reset(
            world::Create(reinterpret_cast<ui::Cursor*>(GetCursor()), GetInput(), scenarioId.c_str(), state->m_loadingSave != 0));
    if (!state->m_loadingSave) {
        Settings& settings = settings_store::Current();
        int width = settings.m_resolutionWidth, height = settings.m_resolutionHeight;
        if (ui::renderer::IsModeSupported(GetRenderer(), width)) {
            game_api::ChangeResolution(width, height);
        } else {
            width = 800;
            height = 600;
            game_api::ChangeResolution(width, height);
            settings.m_resolutionWidth = width;
            settings.m_resolutionHeight = height;
            settings_store::Save(settings);
        }
        weather::runtime::RandomizeRainDrops();
        StartAmbience(state);
        FinishAmbience();
        if (game->m_sandbox)
            ClearField15c();
        else
            SetField15cUnlessMenu();
        WorldUpdate(state->m_world.get());
        RestartPlaylist();
    }
}

void FinishWorldLoad(WorldTestState* state)
{
    game::Game* game = GetGame();
    const Settings& settings = settings_store::Current();
    game_api::ChangeResolution(settings.m_resolutionWidth, settings.m_resolutionHeight);
    weather::runtime::RandomizeRainDrops(); // the order of these calls is part of the replay
    StartAmbience(state);
    FinishAmbience();
    SetNight(GetWorld()->m_gameDayTime >= 19.0f ? 1 : 0);
    RestartPlaylist();
    if (game->m_sandbox)
        ClearField15c();
    else
        SetField15cUnlessMenu();
}

void ResizeWorldView(int width, int height)
{
    auto* view = ui::GetMapView();
    view->UnmapBlocks();
    view->SetViewportSize(width, height);
    view->ClampCamera();
}

// ---- Update ----

bool Post(Message& message)
{
    return messages::message_facade::Post(messages::message_facade::Current(), &message);
}

Message MakeMessage(int receiver, int type, int data)
{
    Message message{};
    message.sender = -2;
    message.receiver = receiver;
    message.type = type;
    message.data = data;
    return message;
}

object::Base* CreateObject(const char* name, int type)
{
    return GetObjectManager()->CreateObject(name, static_cast<uint8_t>(type));
}

void Teleport(object::Base* object, const float* position)
{
    static_cast<object::Mover*>(object)->Teleport(position[0], position[1], position[2]);
}

void SetTypeAndState(object::Base* object, uint8_t type, uint32_t state)
{
    object->SetType(type);
    object->SetState(state);
}

void* CreateSprite(const char* name)
{
    return (static_cast<ui::SpriteBank*>(SpriteBank()))->CreateObservedInstance(name);
}

// The boat (or the bus, when the map has no wharf) arrives with its guests: a vehicle that drives the grid's boat route, one guest object per
// guest of the arrival, each asked to leave the vehicle at a random moment within two seconds, and the feedback line.
void ArrivalOfGuests(WorldTestState* state, Arrival& arrival)
{
    g_arrivals.Count();
    world_data::World* world = GetWorld();
    map::Grid* grid = GetGrid();
    tables::WorldParams* params = GetWorldParams();
    object::Manager* objects = GetObjectManager();
    const char* key = !object::object_manager::FindObjectsByName(objects, "WHARF").empty() ? "BOAT" : "BUS";
    const auto* definition = world::world_params::GetVehicleDefinition(params, world::world_params::GetVehicleId(params, key));
    auto* vehicle = static_cast<Vehicle*>(CreateObject(key, 4));
    vehicle->m_ownerId = -2;
    SetTypeAndState(vehicle, 4, 1);
    vehicle->m_sprite = CreateSprite(definition->m_Sprite.c_str());
    const float offscreen[3] = {10.0f, 10.0f, 0.0f};
    Teleport(vehicle, offscreen);
    Message vehicleState = MakeMessage(static_cast<int>(vehicle->m_id), 3, 1);
    Post(vehicleState);

    const map::BoatRoute& route = grid->m_boat;
    const auto pointCount = static_cast<uint32_t>(route.m_numPoints);
    const map::BoatPoint* points = route.m_points.data();
    for (uint32_t i = 0; i < pointCount; ++i) vehicle->AddRouteStop(points[i].x, points[i].y, points[i].z);
    const float start[3] = {points[0].x, points[0].y, points[0].z};
    Teleport(vehicle, start);
    vehicle->m_DestroyPoint = 0;

    // The entry and exit of the route: positions, the two direction numbers (the entry's also becomes the unload direction) and the offsets.
    std::memcpy(vehicle->m_FerryEntryPoint, route.m_entry.m_pos, sizeof(vehicle->m_FerryEntryPoint));
    vehicle->m_ferryEntryDir = static_cast<uint32_t>(route.m_entry.m_number);
    vehicle->m_unloadDir = route.m_entry.m_number;
    std::memcpy(vehicle->m_FerryEntryOffset, route.m_entry.m_dir, sizeof(vehicle->m_FerryEntryOffset));
    std::memcpy(vehicle->m_FerryExitPoint, route.m_exit.m_pos, sizeof(vehicle->m_FerryExitPoint));
    vehicle->m_ferryExitDir = static_cast<uint32_t>(route.m_exit.m_number);
    std::memcpy(vehicle->m_FerryExitOffset, route.m_exit.m_dir, sizeof(vehicle->m_FerryExitOffset));
    SetUnloadPoint(vehicle, static_cast<uint8_t>(route.m_unloadPointIndex));

    for (int guestIndex = 0; guestIndex < arrival.numGuests; ++guestIndex) {
        // A random guest type that still has guests left in this arrival.
        uint32_t type = static_cast<uint32_t>(Rand()) % static_cast<uint32_t>(params->m_numGuests);
        while (arrival.guestTypes[type] == 0) type = static_cast<uint32_t>(Rand()) % static_cast<uint32_t>(params->m_numGuests);
        --arrival.guestTypes[type];
        const auto* guestDef = world::world_params::GetGuestDefinition(params, static_cast<int>(type));
        ++state->m_world->m_totalGuestArrive[guestDef->m_Gender];
        ++state->m_world->m_todayGuestArrive[guestDef->m_Gender];
        auto* guest = static_cast<object::Guest*>(CreateObject(guestDef->m_key.c_str(), 3));
        SetTypeAndState(guest, 3, 0);
        Teleport(guest, start);
        auto* sprite = static_cast<ui::SpriteInstance*>(CreateSprite(guestDef->m_Sprite.c_str()));
        guest->m_sprite = sprite;
        if (sprite->m_animIndex != guestDef->m_DefSprite) {
            sprite->m_animIndex = guestDef->m_DefSprite;
            sprite->m_frameIndex = 0;
        }
        sprite->m_keyline = static_cast<uint8_t>(objects->m_globalKeyline);
        guest->m_visible = 0;
        guest->m_VehicleID = static_cast<int32_t>(vehicle->m_id);
        if (arrival.performance != 0) {
            const double rating = static_cast<double>(static_cast<uint32_t>(world->ResortRating()));
            const double scaled = rating * static_cast<double>(0.01f);
            float opinion = static_cast<float>(scaled);
            if (scaled < static_cast<double>(0.22f)) opinion = 0.22f;
            InitOpinions(guest, opinion);
        }
        ++world->m_numGuests;
        Message arrive = MakeMessage(static_cast<int>(guest->m_id), 3, 0);
        Post(arrive);
        Message leave = MakeMessage(static_cast<int>(guest->m_id), 0x3f, 0);
        const double delay = static_cast<double>(Rand()) * static_cast<double>(3.051851e-05f);
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &leave, static_cast<float>(delay + delay));
        vehicle->m_Guests[vehicle->m_currCap] = static_cast<int32_t>(guest->m_id);
        ++vehicle->m_currCap;
    }
    Message depart = MakeMessage(static_cast<int>(vehicle->m_id), 3, 0x47);
    Post(depart);

    // "FEEDBACK_<BUS|FERRY>_ARRIVAL" with the guest count, pointing at the only bus stop / wharf if there is exactly one.
    const bool bus = ascii::EqualsNoCase("BUS", vehicle->m_Name);
    const Char16* text = ui::text_table::Lookup(bus ? "FEEDBACK_BUS_ARRIVAL" : "FEEDBACK_FERRY_ARRIVAL");
    const auto stops = object::object_manager::FindObjectsByName(objects, bus ? "BUSSTOP" : "WHARF");
    int objectId = -2;
    if (stops.size() == 1) objectId = static_cast<int>(stops.front()->m_id);
    Char16 line[140];
    ::obl::ui::host_text::Print(line, text, arrival.numGuests);
    feedback::Feedback(world->m_feedbackManager).AddText(line, objectId, 1);
}

int UpdateWorld(WorldTestState* state)
{
    world_data::World* world = GetWorld();
    (static_cast<ui::SpriteBank*>(SpriteBank()))->UnloadStale(world->m_gameTime);
    if (state->m_updateSuspended) return 0;
    WorldUpdate(state->m_world.get());
    if (!state->m_world->m_paused) {
        // The sea ambience follows how much sea the view shows; the crowd and the birds read the same number.
        const double range = ui::GetMapView()->GetVisibleTileRange();
        const float zoomOut = static_cast<float>(1.0 - range);
        const double product = range * static_cast<double>(0.95f);
        float seaVolume = static_cast<float>(product);
        if (product > static_cast<double>(0.8f)) seaVolume = 0.8f;
        if (!(seaVolume >= 0.05f)) seaVolume = 0.05f;
        FadeChannel(state->m_wavesChannel, seaVolume, 0.3f);
        UpdateCrowdCricket(state, seaVolume);
        PlayRandomBird(state, zoomOut);
    }
    Arrival arrival{};
    if (!ferry::ferry_facade::PopDueBoat(GetWorld()->m_ferryManager, &arrival)) return 0;
    ArrivalOfGuests(state, arrival);
    return 0;
}

} // namespace

// ---- the state ----

WorldTestState::WorldTestState() : GameState("WorldTest")
{
    g_calls.Count();
    SeedGameRand(static_cast<uint32_t>(std::time(nullptr)));
}
WorldTestState::~WorldTestState()
{
    g_calls.Count();
    if (m_entered != 0) LeaveGame(this);
}
void WorldTestState::WorldDeleter::operator()(obl::world::World* world) const
{
    world::Destroy(world);
}
int WorldTestState::Update()
{
    g_calls.Count();
    return UpdateWorld(this);
}
void WorldTestState::Render()
{
    g_calls.Count();
    world::Draw(*m_world);
}
void WorldTestState::PostRender()
{
    g_calls.Count();
    world::DrawObjectOverlays(*m_world);
}
void WorldTestState::Enter()
{
    g_calls.Count();
    EnterWorld(this);
}
void WorldTestState::Exit()
{
    g_calls.Count();
    LeaveGame(this);
}
const char* WorldTestState::GetGuiScript()
{
    g_calls.Count();
    return LayoutScript();
}
void WorldTestState::ChangeResolution(int width, int height)
{
    g_calls.Count();
    ResizeWorldView(width, height);
}
void WorldTestState::FinishLoad()
{
    g_calls.Count();
    FinishWorldLoad(this);
}

} // namespace obl::game
