#include "game/game/save_load.h"
#include "game/map/path_queries.h"

#include "formats/save_document.h"
#include "formats/save_records.h"
#include "formats/schemas.h"
#include "game/tables/commodity_providers.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_scratch.h"
#include "game/construction/construction_state.h"
#include "game/construction/construction_manager.h"
#include "game/game/diagnostics.h"
#include "game/events/event_manager.h"
#include "game/cleaning/cleaning_state.h"
#include "game/feedback/feedback_state.h"
#include "game/ferry/ferry_state.h"
#include "game/messages/message_state.h"
#include "game/repair/repair_state.h"
#include "game/objects/building_queries.h"
#include "game/ui/save_slots.h"
#include "game/world/save_objects_json.h"
#include "game/world/save_sections_json.h"
#include "game/world/world_load_json.h"
#include "game/world/map_load_json.h"
#include "game/world/save_objects_load.h"
#include "game/ui/sprite_bank.h"
#include "game/world/world_queries_api.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_vehicle.h"
#include "game/objects/object_performers.h"
#include "game/objects/object_table.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_guest.h"
#include "game/objects/scenery_lifecycle.h"
#include "game/objects/building_serial.h"
#include "game/map/grid_state.h"
#include "game/objects/object_staff.h"
#include "game/objects/staff_serial.h"
#include "game/objects/object_moving.h"
#include "game/ui/sprite.h"
#include "game/world/save_sections_load.h"
#include "game/ui/map_view.h"
#include "game/player/player_state.h"
#include "game/weather/weather.h"
#include "game/world/world_state.h"
#include "game/world/world_save_json.h"
#include "game/objects/building_queue_state.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "core/code_page.h"
#include "core/code_pages.h"
#include "core/pack_path.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/log.h"
#include "core/watchdog.h"

#include "core/json_schema.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>

namespace obl::game {
namespace {
// Relative to the game directory, like the other converted data (docs/build/game-data.md "Converted pack").
std::filesystem::path PackRoot()
{
    return pack_path::Selected();
}

bool ReadJson(const std::filesystem::path& path, nlohmann::json& out, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path.generic_string();
        return false;
    }
    out = nlohmann::json::parse(in, nullptr, false);
    if (out.is_discarded()) {
        error = "invalid JSON in " + path.generic_string();
        return false;
    }
    return true;
}

bool LoadSchemas(json_schema::Registry*& out, std::string& error)
{
    static json_schema::Registry schemas; // the embedded data/schemas, loaded on the first save
    static bool schemasLoaded = false;
    if (!schemasLoaded && !(schemasLoaded = formats::LoadEmbeddedSchemas(schemas, error))) return false;
    out = &schemas;
    return true;
}

struct DroppedFields {
    std::map<uint32_t, nlohmann::json> guests; // the `guest` and `staff` sections by object id
    std::map<uint32_t, nlohmann::json> staff;
    std::optional<std::array<uint32_t, commodity::kFamilies>> complaints;
    // Per construction job in list order (the document's order): which builder token holds each work point. Without them a
    // reloaded builder claims the first free point instead of its own (chaos bot, s1_tricky: two sites, saved at tick 40).
    std::vector<std::optional<std::array<int32_t, construction::kWorkPoints>>> workPointOwners;
    // The beers served by every bar since the last random effect (building_lifecycle.h BeerRound). A load keeps the counter of the session
    // before it, so a reloaded world drew its next random beer at another time (chaos bot, s1_beer_counter_save).
    std::optional<int32_t> servedBeers;
    // The `building.queue` sections by building id: where the next guest stands and the stand points. A load lays the points out in a
    // straight line from the serve point, while the live table bends around what RebuildPoints met, so a guard sent to the queue tail
    // walked elsewhere after a reload (chaos bot, s12_queue_points_save).
    std::map<uint32_t, nlohmann::json> queues;
};

// Every live object by id: guests have their own list, staff are among the others.
std::map<uint32_t, object::Base*> LiveObjects()
{
    std::map<uint32_t, object::Base*> objects;
    object::Manager* manager = GetObjectManager();
    for (uint32_t i = 0; i < manager->m_guests.size(); ++i) objects[manager->m_guests[i]->m_id] = manager->m_guests[i];
    for (uint32_t i = 0; i < manager->m_others.size(); ++i) objects[manager->m_others[i]->m_id] = manager->m_others[i];
    return objects;
}

// The queue of every live building that has one, by building id.
std::map<uint32_t, object::BuildingQueue*> LiveQueues()
{
    std::map<uint32_t, object::BuildingQueue*> queues;
    for (object::Base* object : GetObjectManager()->m_buildings)
        if (auto* queue = static_cast<object::BuildingQueue*>(static_cast<object::Building*>(object)->m_queue))
            queues[object->m_id] = queue;
    return queues;
}

// The stand points of a queue with a points table (limit 1..10): limit + 1 of them, the serve side first.
uint32_t QueuePointCount(const object::BuildingQueue& queue)
{
    return queue.m_points && queue.m_maxLength >= 1 && queue.m_maxLength <= 10 ? queue.m_maxLength + 1 : 0;
}

// The sections of a validated document (QueueOblSave) that hold any of the fields.
DroppedFields ReadDroppedFields(const nlohmann::json& document)
{
    DroppedFields fields;
    for (const nlohmann::json& object : document.at("objects")) {
        const uint32_t id = object.at("id").get<uint32_t>();
        if (const auto guest = object.find("guest"); guest != object.end()) fields.guests[id] = *guest;
        if (const auto staff = object.find("staff"); staff != object.end()) fields.staff[id] = *staff;
        if (const auto building = object.find("building"); building != object.end() && building->contains("queue"))
            fields.queues[id] = building->at("queue");
    }
    if (const auto managers = document.find("managers"); managers != document.end() && managers->contains("construction"))
        for (const nlohmann::json& job : managers->at("construction").at("jobs")) {
            auto& owners = fields.workPointOwners.emplace_back();
            if (const auto found = job.find("work_point_owners"); found != job.end())
                owners = found->get<std::array<int32_t, construction::kWorkPoints>>();
        }
    if (const auto managers = document.find("managers"); managers != document.end() && managers->contains("commodities")) {
        const nlohmann::json& complaints = managers->at("commodities").at("complaints");
        fields.complaints.emplace();
        for (size_t i = 0; i < fields.complaints->size(); ++i) (*fields.complaints)[i] = complaints.at(i).get<uint32_t>();
    }
    if (const auto managers = document.find("managers"); managers != document.end() && managers->contains("beer"))
        fields.servedBeers = managers->at("beer").at("served").get<int32_t>();
    return fields;
}

template<typename T> void Put(const nlohmann::json& section, const char* key, T& field)
{
    const auto value = section.find(key);
    if (value == section.end()) return;
    if constexpr (std::is_same_v<T, float>)
        field = std::bit_cast<float>(formats::SaveFloatBits(*value).value_or(0));
    else
        field = value->get<T>();
}

void ApplyDroppedFields(const DroppedFields& fields)
{
    const auto live = LiveObjects();
    for (const auto& [id, section] : fields.guests) {
        const auto found = live.find(id);
        if (found == live.end()) continue;
        auto* guest = static_cast<object::Guest*>(found->second);
        Put(section, "topless", guest->m_topless);
        Put(section, "times_blocked", guest->m_NumTimesBlocked);
        Put(section, "mood_icon_time", guest->m_moodIconTime);
        Put(section, "worst", guest->m_Worst);
    }
    for (const auto& [id, section] : fields.staff) {
        const auto found = live.find(id);
        if (found == live.end()) continue;
        auto* staff = static_cast<object::Staff*>(found->second);
        Put(section, "target_happiness", staff->m_targetHappiness);
        Put(section, "happiness_rate", staff->m_happinessRate);
        if (const auto changing = section.find("happiness_changing"); changing != section.end())
            staff->m_happinessChanging = changing->get<bool>() ? 1 : 0;
    }
    const auto queues = LiveQueues();
    for (const auto& [id, section] : fields.queues) {
        const auto found = queues.find(id);
        if (found == queues.end()) continue;
        object::BuildingQueue& queue = *found->second;
        if (const auto tail = section.find("tail"); tail != section.end()) {
            Put(*tail, "x", queue.m_tail[0]);
            Put(*tail, "y", queue.m_tail[1]);
        }
        // Only a table of the size the load made: the limit is in the old record, so a matching save always has one.
        const auto points = section.find("points");
        if (points == section.end() || points->size() != QueuePointCount(queue)) continue;
        for (uint32_t i = 0; i < points->size(); ++i) {
            Put(points->at(i), "x", queue.m_points[i * 2]);
            Put(points->at(i), "y", queue.m_points[i * 2 + 1]);
        }
    }
    if (construction::Manager* construction = GetWorld()->m_constructionManager) {
        size_t i = 0;
        for (auto& job : construction->jobs) {
            if (i < fields.workPointOwners.size() && fields.workPointOwners[i]) job->m_workPointOwners = *fields.workPointOwners[i];
            ++i;
        }
    }
    if (fields.servedBeers) ServedBeerCounter() = *fields.servedBeers;
    if (fields.complaints) std::copy(fields.complaints->begin(), fields.complaints->end(), GetCommodityProviders()->complaints.data());
}
} // namespace

namespace {
// The slot header (WriteRecords' first records, the converter's Header): save time, scenario, day, cash, guests and the title, whose
// text in the pack's language lets the slot list read without the text table; `converted_from` is null for a game save.
bool WriteHeaderJson(const formats::SaveOrigin& origin, const world_save::JsonContext& context, uint32_t timestamp,
        nlohmann::ordered_json& h, std::string& error)
{
    const Game& game = *GetGame();
    const world::World& world = *GetWorld();
    const int scenario = game.m_lastUnlockedScenario;
    h = nlohmann::ordered_json::object();
    h["saved_at"] = timestamp;
    h["scenario"] = (game.m_sandbox ? "sandbox" : "s") + std::to_string(scenario + 1);
    h["sandbox"] = game.m_sandbox != 0;
    const std::string title = game.m_sandbox ? save_slots::SandboxTitleKey(scenario) : world.m_scenarioTitle;
    const std::string_view stored = std::string_view(title).substr(0, title.find('\0'));
    const bool isKey = !stored.empty() && std::all_of(stored.begin(), stored.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    });
    if (isKey) h["title"] = std::string(stored);
    const auto text = isKey ? origin.texts.find(stored) : origin.texts.end();
    h["title_text"] = text != origin.texts.end() ? text->second : context.narrowToUtf8(stored);
    h["day"] = world.m_gameDay;
    h["day_time"] = formats::SaveFloat(std::bit_cast<uint32_t>(world.m_gameDayTime));
    h["cash"] = formats::SaveFloat(std::bit_cast<uint32_t>(world.m_player->m_cash));
    if (world.m_numGuests < 0) {
        error = "header: negative guest count " + std::to_string(world.m_numGuests);
        return false;
    }
    h["guests"] = world.m_numGuests;
    h["packs"] = nlohmann::ordered_json::array(
            {nlohmann::ordered_json{{"id", origin.packId}, {"content_version", origin.contentVersion}, {"pack_hash", origin.packHash}}});
    h["converted_from"] = nullptr;
    return true;
}

bool WriteDocument(
        const formats::SaveOrigin& origin, uint32_t timestamp, nlohmann::ordered_json& document, size_t& objectCount, std::string& error)
{
    nlohmann::json index;
    if (!ReadJson(pack_path::Resolve(PackRoot(), "catalog/index.json"), index, error)) return false;
    const bool polish = origin.language == "pl";
    world_save::JsonContext context;
    context.narrowToUtf8 = [polish](std::string_view text) {
        std::u16string wide;
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            const unsigned code = code_pages::CodePoint(polish, byte);
            wide.push_back(static_cast<char16_t>(code == code_pages::kUndefinedCodePoint ? byte : code));
        }
        return code_page::Utf16ToUtf8(wide);
    };
    const auto catalogKey = [&index](const char* name) {
        return [&index, name](int32_t id) -> std::string {
            const auto list = index.find(name);
            if (list == index.end() || !list->is_array() || id < 0 || static_cast<size_t>(id) >= list->size()) return {};
            return (*list)[static_cast<size_t>(id)].get<std::string>();
        };
    };
    context.objectStates = origin.objectStates;
    context.buildingKey = catalogKey("buildings");
    context.beerKey = catalogKey("beers");
    context.eventKey = catalogKey("events");
    context.underConstruction = [](int32_t id) {
        return ProgressIsNonZero(construction::runtime::IsBuilding(GetWorld()->m_constructionManager, id));
    };
    context.underDemolition = [](int32_t id) {
        return ProgressIsNonZero(construction::runtime::IsDemolishing(GetWorld()->m_constructionManager, id));
    };

    const world::World& live = *GetWorld();
    document = nlohmann::ordered_json::object();
    document["format"] = "obl.save";
    document["version"] = 2;
    if (!WriteHeaderJson(origin, context, timestamp, document["header"], error) ||
            !world_save::WriteWorldJson(live, live.m_worldScript->m_currentState, context, document["world"], error))
        return false;
    world_save::WritePlayerJson(*live.m_player, document["player"]);
    if (!world_save::WriteMapJson(*live.m_grid, document["map"], error)) return false;
    // The objects in the object manager's save's order (buildings, guests, others).
    const auto& objects = *live.m_objectManager;
    nlohmann::ordered_json& list = document["objects"] = nlohmann::ordered_json::array();
    objectCount = 0;
    for (const auto* family : {&objects.m_buildings, &objects.m_guests, &objects.m_others})
        for (const object::Base* object : *family) {
            nlohmann::ordered_json entry;
            if (!world_save::WriteObjectJson(*object, context, entry, error)) return false;
            list.push_back(std::move(entry));
            ++objectCount;
        }
    nlohmann::ordered_json& managers = document["managers"] = nlohmann::ordered_json::object();
    if (!world_save::WriteObjectManagerJson(objects.m_nextFreeObjectId, objects.m_globalKeyline, managers["objects"], error) ||
            !world_save::WriteRepairJson(*live.m_repairManager, managers["repair"], error) ||
            !world_save::WriteCleaningJson(*live.m_cleaningManager, managers["cleaning"], error) ||
            !world_save::WriteWeatherJson(*live.m_weather, managers["weather"], error) ||
            !world_save::WriteEventsJson(*live.m_eventManager, managers["events"], error) ||
            !world_save::WriteFerryJson(*live.m_ferryManager, context, managers["ferry"], error) ||
            !world_save::WriteFeedbackJson(*live.m_feedbackManager, context, managers["feedback"], error) ||
            !world_save::WriteMessagesJson(*live.m_messageHandler, managers["messages"], error) ||
            !world_save::WriteConstructionJson(*live.m_constructionManager, managers["construction"], error) ||
            !world_save::WriteViewJson(*live.m_mapView, document["view"], error))
        return false;
    // Adding "view" may move the ordered document's storage (a vector), so `managers` is looked up again.
    nlohmann::ordered_json& moved = document["managers"];
    world_save::WriteCommoditiesJson(GetCommodityProviders()->complaints, moved["commodities"]);
    world_save::WriteBeerJson(ServedBeerCounter(), moved["beer"]);
    return true;
}

} // namespace

namespace {
constexpr std::string_view kReadSections[] = {"World", "WorldMap", "Path", "ObjectManager", "Player", "FerryManager", "EventManager",
        "RepairManager", "FeedbackManager", "MessageHandler", "ZEngine", "CleaningManager", "ConstructionManager", "Weather"};

bool HasSectionReader(std::string_view kind)
{
    return std::find(std::begin(kReadSections), std::end(kReadSections), kind) != std::end(kReadSections);
}

world_save::ObjectLoadCallbacks LiveObjectCallbacks()
{
    world_save::ObjectLoadCallbacks callbacks;
    callbacks.setPosition = [](obl::object::Base& o, const uint32_t xyz[3]) { o.SetPosition(reinterpret_cast<const float*>(xyz)); };
    callbacks.createSprite = [](const char* name) -> void* {
        return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name);
    };
    callbacks.circuitPointCount = [](int32_t type) { return GetWorld()->CircuitPointCount(type); };
    callbacks.circuitPoints = [](int32_t type) { return GetWorld()->CircuitPoints(type); };
    callbacks.sceneryDraw = [](obl::object::Scenery& s) { object::ScenerySetDraw(s); };
    callbacks.sceneryFootprint = [](obl::object::Scenery& s) { object::SceneryJoinFootprint(s); };
    callbacks.staffCounter = [](const char* key, int32_t value) { obl::staff::lifecycle::CounterValue(key) = value; };
    callbacks.beachArea = [](int32_t index) -> const obl::map::Area* { return obl::map::Grid::AreaAt(GetGrid()->m_beachAreas, index); };
    callbacks.buildingEvents = &object::BuildingLoadEventsJson;
    callbacks.buildingQueue = &object::BuildingLoadQueueJson;
    callbacks.buildingThoughts = &object::BuildingLoadThoughtsJson;
    return callbacks;
}

// ReadObjectJson on a scratch object of class T (no grid move, a scratch sprite, the route freed after; its teardown never runs).
template<class T> bool CheckObject(const nlohmann::json& in, const world_save::ReadContext& context, uint8_t type, std::string& error)
{
    auto scratch = std::make_unique<obl::object::Scratch<T>>();
    T* object = scratch->Get();
    object->m_type = static_cast<obl::object::ObjectType>(type);
    obl::ui::SpriteInstance sprite{}, attached{};
    // The sprite a performer's constructor attaches (the reader fills it).
    if constexpr (requires { object->m_attachedSprite; }) object->m_attachedSprite = &attached;
    // A table's chairs and plates come from its definition: the scratch one has none, so only the table's own scalars and the
    // static and base layers are checked.
    obl::tables::TableDef noChairs{};
    if constexpr (std::is_same_v<T, obl::object::Furniture>) object->m_definition = &noChairs;
    // A building's animation sprites come from its definition too: the scratch one has none (the extra sprite neither).
    obl::tables::BuildingDef noSprites{};
    if constexpr (std::is_same_v<T, obl::object::Building>) object->m_definition = &noSprites;
    world_save::ObjectLoadCallbacks callbacks;
    callbacks.setPosition = [](obl::object::Base&, const uint32_t*) {};
    callbacks.createSprite = [&sprite](const char*) -> void* { return &sprite; };
    callbacks.circuitPointCount = [](int32_t) { return 0u; };
    callbacks.circuitPoints = [](int32_t) -> void* { return nullptr; };
    callbacks.sceneryDraw = [](obl::object::Scenery&) {};
    callbacks.sceneryFootprint = [](obl::object::Scenery&) {};
    callbacks.staffCounter = [](const char*, int32_t) {};
    callbacks.beachArea = [](int32_t) -> const obl::map::Area* { return nullptr; };
    callbacks.buildingEvents = [](obl::object::Building&, const world_save::BuildingEventsJson&) {};
    callbacks.buildingQueue = [](obl::object::Building&, const world_save::BuildingQueueJson&) {};
    callbacks.buildingThoughts = [](obl::object::Building&, const std::vector<world_save::BuildingThoughtJson>&) {};
    const bool read = world_save::ReadObjectJson(in, context, callbacks, *object, error);
    if constexpr (std::is_base_of_v<obl::object::Mover, T>) obl::path::ReleaseCursor(object->m_pathCursor);
    if constexpr (std::is_same_v<T, obl::object::Guest>) ::operator delete(object->m_name); // the heap name the reader made
    return read;
}

// Every object read on scratch state, its header (type byte and name) read from the document.
bool CheckObjects(const nlohmann::json& objects, const world_save::ReadContext& context, std::string& error)
{
    if (!objects.is_array()) {
        error = "objects: not an array";
        return false;
    }
    for (size_t i = 0; i < objects.size(); ++i) {
        uint8_t type = 0;
        std::string name;
        if (!world_save::ReadObjectHeaderJson(objects[i], context, type, name, error)) return false;
        bool read = true;
        switch (static_cast<obl::object::ObjectType>(type)) {
        case obl::object::ObjectType::Shark: read = CheckObject<obl::object::Shark>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Vehicle: read = CheckObject<obl::object::Vehicle>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Cabaret: read = CheckObject<obl::object::Cabaret>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Singer: read = CheckObject<obl::object::Singer>(objects[i], context, type, error); break;
        case obl::object::ObjectType::WeightLifter: read = CheckObject<obl::object::WeightLifter>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Catwalk: read = CheckObject<obl::object::Catwalk>(objects[i], context, type, error); break;
        case obl::object::ObjectType::WetTShirt: read = CheckObject<obl::object::WetTShirt>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Aerobic: read = CheckObject<obl::object::Aerobic>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Table: read = CheckObject<obl::object::Furniture>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Scenery: read = CheckObject<obl::object::Scenery>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Building: read = CheckObject<obl::object::Building>(objects[i], context, type, error); break;
        case obl::object::ObjectType::ServiceStaff: read = CheckObject<obl::object::ServiceStaff>(objects[i], context, type, error); break;
        case obl::object::ObjectType::SnootyStaff: read = CheckObject<obl::object::SnootyStaff>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Lifeguard: read = CheckObject<obl::object::Lifeguard>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Mechanic: read = CheckObject<obl::object::Mechanic>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Rep: read = CheckObject<obl::object::Rep>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Builder: read = CheckObject<obl::object::Builder>(objects[i], context, type, error); break;
        case obl::object::ObjectType::Cleaner: read = CheckObject<obl::object::Cleaner>(objects[i], context, type, error); break;
        case obl::object::ObjectType::SecurityGuard:
            read = CheckObject<obl::object::SecurityGuard>(objects[i], context, type, error);
            break;
        case obl::object::ObjectType::Guest: read = CheckObject<obl::object::Guest>(objects[i], context, type, error); break;
        default:
            error = "objects[" + std::to_string(i) + "]: no reader for object type " + std::to_string(type);
            read = false;
            break;
        }
        if (!read) return false;
    }
    return true;
}

// One obl.save load: the document and the catalog the readers resolve keys against, held from QueueOblSave to the end of the world load.
struct QueuedLoad {
    nlohmann::json document;
    nlohmann::json index;
    world_save::ReadContext context;
};
std::shared_ptr<const QueuedLoad> g_activeLoad; // current from the header reads to the end of the world load

// The converter's code page mapping (save_writer.cpp JsonSource): each code point to the first byte that shows it, an undefined
// byte standing for itself.
world_save::ReadContext MakeReadContext(const formats::SaveOrigin& origin, const nlohmann::json& index)
{
    auto narrow = std::make_shared<std::map<char16_t, uint8_t>>();
    const bool polish = origin.language == "pl";
    for (unsigned b = 0; b < 256; ++b) {
        const unsigned code = code_pages::CodePoint(polish, b);
        narrow->emplace(static_cast<char16_t>(code == code_pages::kUndefinedCodePoint ? b : code), static_cast<uint8_t>(b));
    }
    world_save::ReadContext context;
    context.utf8ToNarrow = [narrow](std::string_view text, std::string& out) {
        out.clear();
        for (const char16_t unit : code_page::Utf8ToUtf16(text)) {
            const auto it = narrow->find(unit);
            if (it == narrow->end()) return false;
            out.push_back(static_cast<char>(it->second));
        }
        return true;
    };
    const auto catalogId = [&index](const char* name) {
        return [&index, name](std::string_view key) -> int32_t {
            const auto list = index.find(name);
            if (list == index.end() || !list->is_array()) return -1;
            for (size_t i = 0; i < list->size(); ++i)
                if ((*list)[i].is_string() && (*list)[i].get_ref<const std::string&>() == key) return static_cast<int32_t>(i);
            return -1;
        };
    };
    context.buildingId = catalogId("buildings");
    context.objectStates = origin.objectStates;
    context.beerId = catalogId("beers");
    context.eventId = catalogId("events");
    return context;
}

// A top-level section of the document; a missing optional one reads as empty (every key at its default).
const nlohmann::json& Section(const QueuedLoad& load, const char* key)
{
    static const nlohmann::json kEmpty = nlohmann::json::object();
    const auto it = load.document.find(key);
    return it == load.document.end() ? kEmpty : *it;
}

// A manager section (`managers.<key>`); a missing one reads as empty, as the bridge reads it.
const nlohmann::json& ManagerSection(const QueuedLoad& load, const char* key)
{
    static const nlohmann::json kEmpty = nlohmann::json::object();
    const nlohmann::json& managers = Section(load, "managers");
    const auto it = managers.find(key);
    return it == managers.end() ? kEmpty : *it;
}

// Every native section read into scratch state, so a refusal stops QueueOblSave before anything is queued. The events, weather and
// construction readers need the live world for their post-steps, so their values are only checked here.
bool CheckSections(const QueuedLoad& load, std::string& error)
{
    auto world = std::make_unique<obl::world::World>();
    uint32_t scriptState = 0;
    obl::player::Player player;
    auto view = std::make_unique<obl::ui::MapView>();
    std::vector<int32_t> burnQueue;
    auto ferry = std::make_unique<obl::ferry::Manager>();
    auto repair = std::make_unique<obl::repair::Manager>();
    auto feedback = std::make_unique<obl::feedback::Manager>();
    auto messages = std::make_unique<obl::messages::Handler>();
    auto cleaning = std::make_unique<obl::cleaning::Manager>();
    obl::object_manager::SavedHeader objects;
    return world_save::ReadWorldJson(load.document.at("world"), load.context, *world, scriptState, error) &&
            world_save::CheckMapJson(load.document.at("map"), error) &&
            world_save::ReadObjectManagerJson(ManagerSection(load, "objects"), load.document.at("objects"), load.context, objects, error) &&
            world_save::ReadPlayerJson(Section(load, "player"), player, error) &&
            world_save::ReadFerryJson(ManagerSection(load, "ferry"), load.context, *ferry, error) &&
            world_save::CheckEventsJson(ManagerSection(load, "events"), error) &&
            world_save::ReadRepairJson(ManagerSection(load, "repair"), *repair, error) &&
            world_save::ReadFeedbackJson(ManagerSection(load, "feedback"), load.context, *feedback, error) &&
            world_save::ReadMessagesJson(ManagerSection(load, "messages"), *messages, error) &&
            world_save::ReadViewJson(Section(load, "view"), *view, burnQueue, error) &&
            world_save::ReadCleaningJson(ManagerSection(load, "cleaning"), *cleaning, error) &&
            world_save::CheckConstructionJson(ManagerSection(load, "construction"), error) &&
            world_save::CheckWeatherJson(ManagerSection(load, "weather"), error);
}

void ReadFailed(const std::string& error)
{
    FatalError("obl.save load: %s", error.c_str());
}
} // namespace

namespace obl_load {
bool ReadsSection(std::string_view kind)
{
    return g_activeLoad && HasSectionReader(kind);
}

uint32_t ReadWorld(obl::world::World& world)
{
    uint32_t scriptState = 0;
    std::string error;
    if (!world_save::ReadWorldJson(g_activeLoad->document.at("world"), g_activeLoad->context, world, scriptState, error)) ReadFailed(error);
    return scriptState;
}

void ReadMap(obl::map::Grid& grid)
{
    std::string error;
    if (!world_save::ReadMapJson(g_activeLoad->document.at("map"), grid, error)) ReadFailed(error);
}

obl::object_manager::SavedHeader ReadObjectManager()
{
    obl::object_manager::SavedHeader header;
    std::string error;
    if (!world_save::ReadObjectManagerJson(
                ManagerSection(*g_activeLoad, "objects"), g_activeLoad->document.at("objects"), g_activeLoad->context, header, error))
        ReadFailed(error);
    return header;
}

void ReadObjectHeader(size_t index, uint8_t& type, std::string& name)
{
    std::string error;
    const nlohmann::json& objects = g_activeLoad->document.at("objects");
    if (index >= objects.size())
        error = "objects: no entry " + std::to_string(index);
    else
        world_save::ReadObjectHeaderJson(objects[index], g_activeLoad->context, type, name, error);
    if (!error.empty()) ReadFailed(error);
}

void ReadObject(obl::object::Base& object, uint8_t type, size_t index)
{
    // The base object load's `m_type` record: the header's type byte (the converter writes both from the entry's `kind`).
    object.m_type = static_cast<obl::object::ObjectType>(type);
    std::string error;
    const nlohmann::json& objects = g_activeLoad->document.at("objects");
    if (index >= objects.size())
        error = "objects: no entry " + std::to_string(index);
    else
        world_save::ReadObjectJson(objects[index], g_activeLoad->context, LiveObjectCallbacks(), object, error);
    if (!error.empty()) ReadFailed(error);
}

void ReadPlayer(obl::player::Player& player)
{
    std::string error;
    if (!world_save::ReadPlayerJson(Section(*g_activeLoad, "player"), player, error)) ReadFailed(error);
}

void ReadView(ui::MapView* view)
{
    std::string error;
    std::vector<int32_t> burnQueue;
    if (!world_save::ReadViewJson(Section(*g_activeLoad, "view"), *view, burnQueue, error)) ReadFailed(error);
    view->RestoreAfterLoad(burnQueue);
}

void ReadManager(std::string_view kind, obl::world::World& world)
{
    const QueuedLoad& load = *g_activeLoad;
    std::string error;
    bool ok = false;
    if (kind == "FerryManager")
        ok = world_save::ReadFerryJson(ManagerSection(load, "ferry"), load.context, *world.m_ferryManager, error);
    else if (kind == "EventManager")
        ok = disaster::event_facade::LoadJson(world.m_eventManager, ManagerSection(load, "events"), error);
    else if (kind == "RepairManager")
        ok = world_save::ReadRepairJson(ManagerSection(load, "repair"), *world.m_repairManager, error);
    else if (kind == "FeedbackManager")
        ok = world_save::ReadFeedbackJson(ManagerSection(load, "feedback"), load.context, *world.m_feedbackManager, error);
    else if (kind == "MessageHandler")
        ok = world_save::ReadMessagesJson(ManagerSection(load, "messages"), *world.m_messageHandler, error);
    else if (kind == "CleaningManager")
        ok = world_save::ReadCleaningJson(ManagerSection(load, "cleaning"), *world.m_cleaningManager, error);
    else if (kind == "ConstructionManager")
        ok = construction::runtime::LoadJson(world.m_constructionManager, ManagerSection(load, "construction"), error);
    else if (kind == "Weather")
        ok = weather::runtime::LoadJson(world.m_weather, ManagerSection(load, "weather"), error);
    else
        error = std::string(kind) + ": no section reader";
    if (!ok) ReadFailed(error);
}
} // namespace obl_load

bool QueueOblSave(const std::filesystem::path& file, std::string& error)
{
    // Mapping a whole document runs inside one frame and takes over 20 s on a loaded PC: give it the save load's limit.
    WatchdogLongCommand mapping(true);
    json_schema::Registry* schemas = nullptr;
    if (!LoadSchemas(schemas, error)) return false;
    formats::SaveOrigin origin;
    nlohmann::json index;
    nlohmann::json document;
    std::vector<std::string> warnings;
    if (!formats::ReadSavePackOrigin(PackRoot(), origin, error) ||
            !ReadJson(pack_path::Resolve(PackRoot(), "catalog/index.json"), index, error) || !ReadJson(file, document, error))
        return false;
    const bool loaded = formats::ValidateOblSaveDocument(document, origin, *schemas, warnings, error);
    for (const std::string& warning : warnings) OBL_LOG("obl.save %s: %s", file.generic_string().c_str(), warning.c_str());
    if (!loaded) return false;
    formats::FillSaveDefaults(document, *schemas);
    formats::RecordsToSlots(document);
    auto load = std::make_shared<QueuedLoad>();
    load->index = std::move(index);
    load->context = MakeReadContext(origin, load->index);
    load->document = std::move(document);
    if (!CheckSections(*load, error) || !CheckObjects(load->document.at("objects"), load->context, error)) return false;
    // Every section and object is read from the document (world::LoadFromSave); the header names the scenario.
    const nlohmann::json& header = load->document.at("header");
    const std::string scenario = header.at("scenario").get<std::string>();
    const size_t digits = scenario.find_first_of("0123456789");
    if (digits == std::string::npos) {
        error = "header: scenario " + scenario + " has no number";
        return false;
    }
    ui::PendingLoad pending;
    pending.scenario = std::atoi(scenario.c_str() + digits) - 1;
    pending.sandbox = header.at("sandbox").get<bool>();
    pending.beforeLoad = [load] { g_activeLoad = load; };
    pending.afterLoad = [load, fields = ReadDroppedFields(load->document)] {
        g_activeLoad = nullptr;
        ApplyDroppedFields(fields);
    };
    ui::QueueSaveLoad(std::move(pending));
    return true;
}

bool WriteOblSave(const std::filesystem::path& file, uint32_t timestamp, std::string& error, size_t* objectCount)
{
    WatchdogLongCommand mapping(true);
    json_schema::Registry* schemas = nullptr;
    if (!LoadSchemas(schemas, error)) return false;
    formats::SaveOrigin origin;
    if (!formats::ReadSavePackOrigin(PackRoot(), origin, error)) return false;
    nlohmann::ordered_json document;
    size_t written = 0;
    if (!WriteDocument(origin, timestamp, document, written, error)) return false;
    if (objectCount) *objectCount = written;
    formats::SlotsToRecords(document);
    return formats::WriteOblSaveDocument(document, file, *schemas, error);
}

} // namespace obl::game
