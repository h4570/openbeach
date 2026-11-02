#include "game/world/save_sections_load.h"

#include "game/cleaning/cleaning_lifecycle.h"
#include "game/construction/construction_lifecycle.h"
#include "game/events/event_lifecycle.h"
#include "game/feedback/feedback_lifecycle.h"
#include "game/ferry/ferry_lifecycle.h"
#include "game/messages/message_lifecycle.h"
#include "game/player/player_state.h"
#include "game/repair/repair_lifecycle.h"
#include "game/ui/map_view.h"
#include "game/weather/weather_save.h"
#include "game/world/save_json_values.h"
#include "game/world/save_table_reader.h"
#include "core/code_page.h"

#include <bit>
#include <memory>

namespace obl::world_save {
namespace {

using nlohmann::json;

// Reads the keys of one section; the first refusal names the section and the key.
class Section {
public:
    Section(const char* name, const json& in, std::string& error) : m_name(name), m_in(in), m_error(error) { m_error.clear(); }

    bool Ok() const { return m_error.empty(); }
    void Fail(const std::string& key, const std::string& why)
    {
        if (m_error.empty()) m_error = std::string(m_name) + "." + key + ": " + why;
    }
    const json* Find(const char* key) const
    {
        const auto it = m_in.find(key);
        return it == m_in.end() ? nullptr : &*it;
    }
    uint32_t Integer(const char* key, const json* value)
    {
        if (!value) return 0;
        const auto bits = IntegerBits(*value);
        if (!bits) Fail(key, "not an integer");
        return bits.value_or(0);
    }
    uint32_t Integer(const char* key) { return Integer(key, Find(key)); }
    float Float(const char* key, const json* value)
    {
        if (!value) return 0.0f;
        const auto bits = FloatBits(*value);
        if (!bits) Fail(key, "not a float");
        return std::bit_cast<float>(bits.value_or(0));
    }
    float Float(const char* key) { return Float(key, Find(key)); }
    // Element `i` of a pair; 0 when the pair is missing.
    const json* PairItem(const char* key, size_t i)
    {
        const json* pair = Find(key);
        if (!pair) return nullptr;
        if (!pair->is_array() || pair->size() != 2) {
            Fail(key, "not a pair");
            return nullptr;
        }
        return &(*pair)[i];
    }
    std::string Text(const char* key)
    {
        const json* value = Find(key);
        if (!value) return {};
        if (!value->is_string()) Fail(key, "not a text");
        return value->is_string() ? value->get<std::string>() : std::string();
    }

private:
    const char* m_name;
    const json& m_in;
    std::string& m_error;
};

} // namespace

bool ReadPlayerJson(const json& in, player::Player& player, std::string& error)
{
    Section s("Player", in, error);
    const std::u16string name = code_page::Utf8ToUtf16(s.Text("name"));
    size_t length = 0;
    while (length < name.size() && length < static_cast<size_t>(player::kNameCapacity) && name[length] != 0) ++length;
    player.m_name.assign(name.begin(), name.begin() + static_cast<std::ptrdiff_t>(length));
    player.m_cash = s.Float("cash");
    return s.Ok();
}

bool ReadViewJson(const json& in, ui::MapView& view, std::vector<int32_t>& burnQueue, std::string& error)
{
    Section s("ZEngine", in, error);
    view.m_cameraX = static_cast<int32_t>(s.Integer("camera", s.PairItem("camera", 0)));
    view.m_cameraY = static_cast<int32_t>(s.Integer("camera", s.PairItem("camera", 1)));
    view.m_cameraPositionX = s.Float("position", s.PairItem("position", 0));
    view.m_cameraPositionY = s.Float("position", s.PairItem("position", 1));
    view.m_velocityX = s.Float("velocity", s.PairItem("velocity", 0));
    view.m_velocityY = s.Float("velocity", s.PairItem("velocity", 1));
    burnQueue.clear();
    if (const json* queue = s.Find("burn_queue")) {
        if (!queue->is_array())
            s.Fail("burn_queue", "not a list");
        else
            for (const json& entry : *queue) burnQueue.push_back(static_cast<int32_t>(s.Integer("burn_queue", &entry)));
    }
    view.m_mapLocked = static_cast<uint8_t>(s.Integer("locked"));
    view.m_movingMap = static_cast<uint8_t>(s.Integer("moving"));
    view.m_moveMapDuration = s.Float("move_duration");
    view.m_moveMapStartTime = s.Float("move_start_time");
    view.m_moveMapStartX = static_cast<int32_t>(s.Integer("move_start", s.PairItem("move_start", 0)));
    view.m_moveMapStartY = static_cast<int32_t>(s.Integer("move_start", s.PairItem("move_start", 1)));
    view.m_moveMapDestX = static_cast<int32_t>(s.Integer("move_destination", s.PairItem("move_destination", 0)));
    view.m_moveMapDestY = static_cast<int32_t>(s.Integer("move_destination", s.PairItem("move_destination", 1)));
    view.m_lastUnBurnTime = s.Float("last_unburn_time");
    view.m_guestObjectsDrawn = static_cast<int32_t>(s.Integer("guests_drawn"));
    return s.Ok();
}

namespace {

template<class M> bool ReadTable(const char* section, const ReadContext* context, const json& in, M& manager, std::string& error)
{
    PlainTableReader reader(section, context, in, error);
    if (reader.Ok()) LoadFields(manager, reader);
    return reader.Ok();
}

template<class M> bool CheckTable(const char* section, const json& in, M& scratch, std::string& error)
{
    PlainTableReader reader(section, nullptr, in, error);
    if (reader.Ok()) VisitFields(scratch, reader);
    return reader.Ok();
}

} // namespace

bool ReadRepairJson(const json& in, repair::Manager& manager, std::string& error)
{
    return ReadTable("RepairManager", nullptr, in, manager, error);
}

bool ReadCleaningJson(const json& in, cleaning::Manager& manager, std::string& error)
{
    return ReadTable("CleaningManager", nullptr, in, manager, error);
}

bool ReadMessagesJson(const json& in, messages::Handler& handler, std::string& error)
{
    return ReadTable("MessageHandler", nullptr, in, handler, error);
}

bool ReadFeedbackJson(const json& in, const ReadContext& context, feedback::Manager& manager, std::string& error)
{
    return ReadTable("FeedbackManager", &context, in, manager, error);
}

bool ReadFerryJson(const json& in, const ReadContext& context, ferry::Manager& manager, std::string& error)
{
    return ReadTable("FerryManager", &context, in, manager, error);
}

bool CheckEventsJson(const json& in, std::string& error)
{
    disaster::Manager scratch;
    return CheckTable("EventManager", in, scratch, error);
}

bool CheckWeatherJson(const json& in, std::string& error)
{
    auto scratch = std::make_unique<weather::Weather>();
    return CheckTable("Weather", in, *scratch, error);
}

bool CheckConstructionJson(const json& in, std::string& error)
{
    auto scratch = std::make_unique<construction::Manager>();
    return CheckTable("ConstructionManager", in, *scratch, error);
}

} // namespace obl::world_save
