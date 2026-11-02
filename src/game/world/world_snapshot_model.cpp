#include "game/weather/weather_save.h"
#include "game/player/player_state.h"
#include "game/weather/weather_state.h"
#include "game/world/world_snapshot_model.h"
#include "game/world/world_save.h"

#include "game/map/grid_save.h"
#include "game/map/grid_state.h"
#include "game/feedback/feedback_lifecycle.h"
#include "game/events/event_lifecycle.h"
#include "game/ferry/ferry_lifecycle.h"
#include "game/repair/repair_lifecycle.h"
#include "game/cleaning/cleaning_lifecycle.h"
#include "game/construction/construction_lifecycle.h"
#include "game/messages/message_lifecycle.h"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace obl::world_snapshot {

namespace {

// The world field table (world_save.h) as snapshot records: the same names and types as the save block, without the save-only records.
struct WorldEmitter {
    WorldSnapshot& s;

    void String(const char* name, const char* text) { s.String(name, text); }
    void Int(const char* name, int32_t value) { s.Int(name, value); }
    void UInt(const char* name, uint32_t value) { s.UInt(name, value); }
    void Float(const char* name, const float& value) { s.Float(name, value); }
    void Bool(const char* name, uint8_t value) { s.Bool(name, value); }
    void Ints(const char* name, const int* values, int count, world_save::Scope scope)
    {
        if (scope == world_save::Scope::Both) s.Ints(name, values, static_cast<size_t>(count));
    }
    // Only the histories are float arrays, and they are save-only.
    void Floats(const char*, const float*, int, world_save::Scope) {}
    void ScriptState(const char* name, uint32_t state) { s.Int(name, static_cast<int32_t>(state)); }
};

} // namespace

void EmitWorldFields(WorldSnapshot& s, const world::World& w, uint32_t scriptState)
{
    s.Class("World");
    WorldEmitter emitter{s};
    world_save::VisitFields(w, static_cast<const uint32_t&>(scriptState), emitter);
}

namespace {

// The grid field table (grid_save.h) as snapshot records: the same names and types as the save block. Open areas record 1.
struct GridEmitter {
    WorldSnapshot& s;

    void Int(const char* name, int32_t value) { s.Int(name, value); }
    void Area(const char* name, const map::Area& area) { s.UInt(name, !area.m_shut); }
    void Dword(const char* name, uint32_t value) { s.Dword(name, value); }
    void Word(const char* name, uint16_t value) { s.Word(name, value); }
};

} // namespace

void EmitGrid(WorldSnapshot& s, const map::Grid& grid)
{
    s.Class("WorldMap");
    GridEmitter emitter{s};
    map::VisitFields(grid, emitter);
}

// The routes themselves belong to the objects that follow them; the Path class has only its header.
void EmitPath(WorldSnapshot& s)
{
    s.Class("Path");
}

void EmitPlayer(WorldSnapshot& s, const player::Player& player)
{
    s.Class("Player");
    s.WString("Name", player.m_name.c_str());
    s.Float("Cash", player.m_cash);
}

namespace {

// The ferry field table (ferry_lifecycle.h) as snapshot records: the same names and types as the save block. The Day of a daily boat
// (TimeFlags bit 0) is normalized by world_snapshot.cpp, which never reads it.
struct FerryEmitter {
    WorldSnapshot& s;

    template<class L> void Boats(const char* name, const L& list)
    {
        s.UInt(name, static_cast<uint32_t>(list.size()));
        for (const auto& boat : list) ferry::VisitBoat(boat, *this);
    }
    template<class M> void Cursor(const char* name, const M& m)
    {
        uint32_t current = ferry::kNoCursor, index = 0;
        if (m.cursor)
            for (auto node = m.schedule.begin(); node != m.schedule.end(); ++node, ++index)
                if (node == *m.cursor) current = index;
        s.UInt(name, current);
    }
    template<class T> void Int(const char* name, const T& field) { s.Int(name, static_cast<int32_t>(field)); }
    template<class T> void UInt(const char* name, const T& field) { s.UInt(name, static_cast<uint32_t>(field)); }
    template<class T> void Float(const char* name, const T& field) { s.Float(name, field); }
    template<class T> void Floats(const char* name, const T* values, int count) { s.Floats(name, values, count); }
    template<class T> void Ints(const char* name, const T* values, int count) { s.Ints(name, values, count); }
    template<class T> void Keyword(const char* name, const T& text) { s.String(name, text); }
};

} // namespace

void EmitFerry(WorldSnapshot& s, const ferry::Manager& ferry)
{
    s.Class("FerryManager");
    FerryEmitter emitter{s};
    ferry::VisitFields(ferry, emitter);
}

namespace {

// The event field table (event_lifecycle.h) as snapshot records: the same names and types as the save block.
struct EventEmitter {
    WorldSnapshot& s;

    template<class T> void Int(const char* name, const T& field) { s.Int(name, static_cast<int32_t>(field)); }
    template<class L> void Active(const char* countName, const char* idName, const L& list)
    {
        s.UInt(countName, static_cast<uint32_t>(list.size()));
        for (int id : list) s.Int(idName, id);
    }
};

} // namespace

// The world script supplies the event schedule, so only its progress and active events contribute to the checksum.
// Event records share the ferry section without an intervening class record.
void EmitEvents(WorldSnapshot& s, const disaster::Manager& events)
{
    EventEmitter emitter{s};
    disaster::VisitFields(events, emitter);
}

namespace {

// Repair snapshots omit scan distance because each scan overwrites that temporary value before using it.
struct RepairEmitter {
    WorldSnapshot& s;

    template<class Seq, class Apply> void List(const char* name, const Seq& list, Apply apply)
    {
        s.UInt(name, static_cast<uint32_t>(list.size()));
        for (const auto& element : list) apply(*this, element);
    }
    void Int(const char* name, int32_t field) { s.Int(name, field); }
    void UInt(const char* name, uint32_t field) { s.UInt(name, field); }
    void Bool(const char* name, uint8_t field) { s.Bool(name, field); }
    void Float(const char* name, const float& field) { s.Float(name, field); }
    void Scratch(const char*, const float&) {}
};

} // namespace

void EmitRepair(WorldSnapshot& s, const repair::Manager& repair)
{
    s.Class("RepairManager");
    RepairEmitter emitter{s};
    repair::VisitFields(repair, emitter);
}

namespace {

// The feedback field table (feedback_lifecycle.h) as snapshot records: the same names and types as the save block.
struct FeedbackEmitter {
    WorldSnapshot& s;

    template<class Seq, class Apply> void List(const char* name, const Seq& list, Apply apply)
    {
        s.UInt(name, static_cast<uint32_t>(list.size()));
        for (const auto& element : list) apply(*this, element);
    }
    template<class T> void Int(const char* name, const T& field) { s.Int(name, static_cast<int32_t>(field)); }
    void Bool(const char* name, uint8_t field) { s.Bool(name, field); }
    void Float(const char* name, const float& field) { s.Float(name, field); }
    template<size_t N> void Ints(const char* name, const std::array<uint32_t, N>& values) { s.Ints(name, values.data(), N); }
    // A feedback text is either UTF-16 (a formatted message) or a text-table key.
    void Text(const feedback::Text& text)
    {
        s.Bool("wide", text.wide);
        if (text.wide)
            s.WString("text", text.wideText.c_str());
        else
            s.String("text", text.key.c_str());
    }
};

} // namespace

void EmitFeedback(WorldSnapshot& s, const feedback::Manager& feedback)
{
    s.Class("FeedbackManager");
    FeedbackEmitter emitter{s};
    feedback::VisitFields(feedback, emitter);
}

namespace {

// Only delayed messages contribute: world_hash.cpp requires the working list to be empty at a settled frame.
// Type-specific payloads are excluded because not every sender initializes them.
struct MessageEmitter {
    WorldSnapshot& s;

    template<class Seq, class Apply> void List(const char* name, const Seq& list, Apply apply)
    {
        s.UInt(name, static_cast<uint32_t>(list.size()));
        for (const auto& element : list) apply(*this, element);
    }
    void Int(const char* name, int32_t field) { s.Int(name, field); }
    void Float(const char* name, const float& field) { s.Float(name, field); }
    void Payload(const char*, int32_t) {}
    void Payload(const char*, const float&) {}
};

} // namespace

void EmitMessages(WorldSnapshot& s, const messages::Handler& handler)
{
    s.Class("MessageHandler");
    MessageEmitter emitter{s};
    messages::VisitFields(handler, emitter);
}

namespace {

// The cleaning field table (cleaning_lifecycle.h) as snapshot records; the cleaning block has no scratch field.
struct CleaningEmitter {
    WorldSnapshot& s;

    template<class Seq, class Apply> void List(const char* name, const Seq& list, Apply apply)
    {
        s.UInt(name, static_cast<uint32_t>(list.size()));
        for (const auto& element : list) apply(*this, element);
    }
    void Int(const char* name, int32_t field) { s.Int(name, field); }
    void UInt(const char* name, uint32_t field) { s.UInt(name, field); }
    void Bool(const char* name, uint8_t field) { s.Bool(name, field); }
    void Float(const char* name, const float& field) { s.Float(name, field); }
};

} // namespace

void EmitCleaning(WorldSnapshot& s, const cleaning::Manager& queues)
{
    s.Class("CleaningManager");
    CleaningEmitter emitter{s};
    cleaning::VisitFields(queues, emitter);
}

namespace {

// Construction snapshots cap builder entries at the table capacity. The signed bound also emits no entries for a negative count;
// valid worlds always have a count within the table's bounds.
struct ConstructionEmitter {
    WorldSnapshot& s;

    template<class Apply> void Jobs(const char* name, const construction::JobList& jobs, Apply apply)
    {
        s.UInt(name, static_cast<uint32_t>(jobs.size()));
        for (const auto& job : jobs) apply(*this, *job);
    }
    void Define(const construction::Job&) {}
    template<class Apply> void Hoardings(const char* name, const std::vector<construction::Hoarding>& hoardings, Apply apply)
    {
        s.Int(name, static_cast<int32_t>(hoardings.size()));
        for (const auto& hoarding : hoardings) apply(*this, hoarding);
    }
    template<class Table, class Apply> void Builders(int32_t count, const Table& builders, Apply apply)
    {
        const int used = std::min(count, construction::kMaxBuilders);
        for (int i = 0; i < used; ++i) apply(*this, builders[i]);
    }
    void Int(const char* name, int32_t field) { s.Int(name, field); }
    void UInt(const char* name, uint32_t field) { s.UInt(name, field); }
    void Float(const char* name, const float& field) { s.Float(name, field); }
    void Bool(const char* name, uint8_t field) { s.Bool(name, field); }
};

} // namespace

// Ten work points per job, zero without them; Normalize zeroes X/Y of an unused point (direction 0).
void EmitConstruction(WorldSnapshot& s, const construction::Manager& construction)
{
    s.Class("ConstructionManager");
    ConstructionEmitter emitter{s};
    construction::VisitFields(construction, emitter);
}

// Weather records share the construction section without a class record; world_sections.cpp separates them at light_index.
// Normalize clears effect state until it becomes meaningful. The cached display color contributes nothing to the checksum.
namespace {
struct WeatherEmitter {
    WorldSnapshot& s;
    void UInt(const char* name, uint32_t value) { s.UInt(name, value); }
    void Int(const char* name, int32_t value) { s.Int(name, value); }
    void Float(const char* name, float value) { s.Float(name, value); }
    void Bool(const char* name, uint8_t value) { s.Bool(name, value); }
    void Dword(const char*, uint32_t) {} // cached tint is absent from schema 2
    void Area(const char* name, int value) { UInt(name, static_cast<uint32_t>(value)); }
    void Floats(const char* name, const float* values, int count) { s.Floats(name, values, count); }
    void Bitmap(const char* name, const weather::Weather& weather) { Bool(name, weather.m_lightningLightmap ? 1 : 0); }
};
} // namespace

void EmitWeather(WorldSnapshot& s, const weather::Weather& weather)
{
    WeatherEmitter emitter{s};
    weather::VisitFields(weather, emitter);
}

} // namespace obl::world_snapshot
