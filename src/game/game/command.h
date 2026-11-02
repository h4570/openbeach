#pragma once

// Simulation input as semantic commands (docs/architecture/commands.md). The GUI, test scripts and bots submit commands; the
// simulation applies them at the start of a tick; the recorder (command_recorder.h) writes them with that tick. Pure code: no
// game, no Windows API, so the headless simulation and the Linux unit tests share it.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace obl::command {

// The world's object id (m_dwObjectIDList, the ids messages use), stable across save/load unlike a pointer.
using ObjectId = uint32_t;

// Every command lists its fields in text order through Fields(command, f): f(name, member). The text form and the parser are generic
// over that list, so adding a command is one struct here plus its entry in Command.

// The place modes: x/y is the footprint origin the mode computed from the tile under the cursor (GetFootprintOrigin /
// AdjustFootprintOrigin), dir the preview's direction; the executor pays and posts the place-building command of the world. Scenery of category 1 (paths and
// grass) is painted by the surface modes, not placed.
struct PlaceBuilding {
    static constexpr std::string_view kName = "place-building";
    std::string key; // BuildingDef::m_key
    int32_t x = 0;
    int32_t y = 0;
    int32_t direction = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("key", command.key);
        f("x", command.x);
        f("y", command.y);
        f("dir", command.direction);
    }
    bool operator==(const PlaceBuilding&) const = default;
};

struct PlaceScenery {
    static constexpr std::string_view kName = "place-scenery";
    std::string key; // SceneryDef::m_key
    int32_t x = 0;
    int32_t y = 0;
    int32_t direction = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("key", command.key);
        f("x", command.x);
        f("y", command.y);
        f("dir", command.direction);
    }
    bool operator==(const PlaceScenery&) const = default;
};

// One tile of the surface modes (grass, paths): scenery of category 1 painted into the grid flags of tile x/y; the definition's
// m_PathFlag picks grass (non-zero) or path (zero) as the scenery panel does when it enters the mode. The modes paint while the button
// is held, so a drag submits one command per new tile.
struct PaintSurface {
    static constexpr std::string_view kName = "paint-surface";
    std::string key; // SceneryDef::m_key
    int32_t x = 0;
    int32_t y = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("key", command.key);
        f("x", command.x);
        f("y", command.y);
    }
    bool operator==(const PaintSurface&) const = default;
};

// A click of the scenery dozer on tile x/y: removes the user-removable scenery standing there, else the bare path, else the surface,
// with the removal income (the dozer mode's remove-at step).
struct Dozer {
    static constexpr std::string_view kName = "dozer";
    int32_t x = 0;
    int32_t y = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("x", command.x);
        f("y", command.y);
    }
    bool operator==(const Dozer&) const = default;
};

struct Demolish {
    static constexpr std::string_view kName = "demolish";
    ObjectId object = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f) { f("object", command.object); }
    bool operator==(const Demolish&) const = default;
};

struct HireStaff {
    static constexpr std::string_view kName = "hire-staff";
    // The staff browser's hire type by name: Cleaner, Builder, Mechanic, Rep, Lifeguard, SecurityGuard (docs/architecture/commands.md).
    std::string profession;
    template<class Self, class F> static void Fields(Self& command, F&& f) { f("profession", command.profession); }
    bool operator==(const HireStaff&) const = default;
};

struct DismissStaff {
    static constexpr std::string_view kName = "dismiss-staff";
    ObjectId staff = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f) { f("staff", command.staff); }
    bool operator==(const DismissStaff&) const = default;
};

// The wage slider of the staff menu: the member's virtual SetVariable("STAFF_WAGES"), wage thought and the minimum wage included.
struct SetStaffWage {
    static constexpr std::string_view kName = "set-staff-wage";
    ObjectId staff = 0;
    int32_t wage = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("staff", command.staff);
        f("wage", command.wage);
    }
    bool operator==(const SetStaffWage&) const = default;
};

// One member of an "apply wage to all" click (SetStaffWages, StaffSetAllWages): the wage written as it is, without the wage thought
// and the minimum wage of the slider, then the happiness recalculated. The GUI submits one per member of the class.
struct CopyStaffWage {
    static constexpr std::string_view kName = "copy-staff-wage";
    ObjectId staff = 0;
    int32_t wage = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("staff", command.staff);
        f("wage", command.wage);
    }
    bool operator==(const CopyStaffWage&) const = default;
};

// The shift hours (0..23) Staff::SetShift writes; the +/- buttons and "apply to all" submit the hours they computed.
struct SetStaffShift {
    static constexpr std::string_view kName = "set-staff-shift";
    ObjectId staff = 0;
    int32_t start = 0;
    int32_t end = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("staff", command.staff);
        f("start", command.start);
        f("end", command.end);
    }
    bool operator==(const SetStaffShift&) const = default;
};

// A send-staff click on the ground: the send-staff direct-to-point order with the world point under the cursor (copied bit for bit).
struct SendStaff {
    static constexpr std::string_view kName = "send-staff";
    ObjectId staff = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("staff", command.staff);
        f("x", command.x);
        f("y", command.y);
        f("z", command.z);
    }
    bool operator==(const SendStaff&) const = default;
};

// A send-staff click on an object the mode picked (the send-staff mode's can-direct check): the staff object's direct-to-target step.
struct SendStaffTo {
    static constexpr std::string_view kName = "send-staff-to";
    ObjectId staff = 0;
    ObjectId target = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("staff", command.staff);
        f("target", command.target);
    }
    bool operator==(const SendStaffTo&) const = default;
};

struct SetBuildingOpen {
    static constexpr std::string_view kName = "set-building-open";
    ObjectId building = 0;
    bool open = false;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("building", command.building);
        f("open", command.open);
    }
    bool operator==(const SetBuildingOpen&) const = default;
};

struct CallRepair {
    static constexpr std::string_view kName = "call-repair";
    ObjectId building = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f) { f("building", command.building); }
    bool operator==(const CallRepair&) const = default;
};

struct CallCleaner {
    static constexpr std::string_view kName = "call-cleaner";
    ObjectId building = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f) { f("building", command.building); }
    bool operator==(const CallCleaner&) const = default;
};

struct SetPrice {
    static constexpr std::string_view kName = "set-price";
    ObjectId building = 0;
    int32_t slot = 0;
    float price = 0.0f;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("building", command.building);
        f("slot", command.slot);
        f("price", command.price);
    }
    bool operator==(const SetPrice&) const = default;
};

// The price of a show slot (0..2) of a building with a show schedule: SetVariable("EVENT<slot>_PRICE"), which leaves a show in progress alone.
struct SetEventPrice {
    static constexpr std::string_view kName = "set-event-price";
    ObjectId building = 0;
    int32_t slot = 0;
    float price = 0.0f;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("building", command.building);
        f("slot", command.slot);
        f("price", command.price);
    }
    bool operator==(const SetEventPrice&) const = default;
};

// The beer or cocktail list of a bar's dialog: the building's set-beer-type step with the definition `key` (BeerDef::m_key).
struct SetBeer {
    static constexpr std::string_view kName = "set-beer";
    ObjectId building = 0;
    std::string key; // BeerDef::m_key
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("building", command.building);
        f("key", command.key);
    }
    bool operator==(const SetBeer&) const = default;
};

// A show list of a stage's dialog: BuildingEvents::SetSlotEvent(slot 0..2) with the definition `key` (EventDef::m_key). The GUI's
// "show in progress" entry is no selection and submits nothing.
struct SetShow {
    static constexpr std::string_view kName = "set-show";
    ObjectId building = 0;
    int32_t slot = 0;
    std::string key; // EventDef::m_key
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("building", command.building);
        f("slot", command.slot);
        f("key", command.key);
    }
    bool operator==(const SetShow&) const = default;
};

// The pause box's resume button (the exit-dialog resume action): clears the script's forced pause and the pause at one tick. The pause key
// and the ClosePause button only toggle `paused` (set-paused) and leave a forced pause alone.
struct ResumeGame {
    static constexpr std::string_view kName = "resume-game";
    template<class Self, class F> static void Fields(Self&, F&&) {}
    bool operator==(const ResumeGame&) const = default;
};

// The break-object action: the dialog's object gets Entity::m_debugBreak (the update that sees it clears it again).
struct BreakObject {
    static constexpr std::string_view kName = "break-object";
    ObjectId object = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f) { f("object", command.object); }
    bool operator==(const BreakObject&) const = default;
};

// The speed buttons: GAME_GAME_SPEED -> the world's game-speed setter (0..2, the game's speed table).
struct SetGameSpeed {
    static constexpr std::string_view kName = "set-game-speed";
    int32_t level = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f) { f("level", command.level); }
    bool operator==(const SetGameSpeed&) const = default;
};

struct SetPaused {
    static constexpr std::string_view kName = "set-paused";
    bool paused = false;
    template<class Self, class F> static void Fields(Self& command, F&& f) { f("paused", command.paused); }
    bool operator==(const SetPaused&) const = default;
};

// Compatibility input for version-1 replays. Live GUI controls submit the typed commands below.
// Object dialog controls preserve the setter's word semantics, including non-boolean policy values.
struct SetRepairInterval {
    static constexpr std::string_view kName = "set-repair-interval";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetRepairInterval&) const = default;
};

struct SetBeerStrength {
    static constexpr std::string_view kName = "set-beer-strength";
    ObjectId object = 0;
    float value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetBeerStrength&) const = default;
};

struct SetBeerPrice {
    static constexpr std::string_view kName = "set-beer-price";
    ObjectId object = 0;
    float value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetBeerPrice&) const = default;
};

struct SetBuildingOpenPolicy {
    static constexpr std::string_view kName = "set-building-open-policy";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetBuildingOpenPolicy&) const = default;
};

struct SetBeachOpenPolicy {
    static constexpr std::string_view kName = "set-beach-open-policy";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetBeachOpenPolicy&) const = default;
};

struct SetCleanerToilets {
    static constexpr std::string_view kName = "set-cleaner-toilets";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetCleanerToilets&) const = default;
};

struct SetCleanerBins {
    static constexpr std::string_view kName = "set-cleaner-bins";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetCleanerBins&) const = default;
};

struct SetCleanerLitter {
    static constexpr std::string_view kName = "set-cleaner-litter";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetCleanerLitter&) const = default;
};

struct SetMechanicRepair {
    static constexpr std::string_view kName = "set-mechanic-repair";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetMechanicRepair&) const = default;
};

struct SetMechanicVandals {
    static constexpr std::string_view kName = "set-mechanic-vandals";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetMechanicVandals&) const = default;
};

struct SetMechanicWater {
    static constexpr std::string_view kName = "set-mechanic-water";
    ObjectId object = 0;
    int32_t value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetMechanicWater&) const = default;
};

struct SetRepGuestTime {
    static constexpr std::string_view kName = "set-rep-guest-time";
    ObjectId object = 0;
    float value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetRepGuestTime&) const = default;
};

struct SetSecurityHarshness {
    static constexpr std::string_view kName = "set-security-harshness";
    ObjectId object = 0;
    float value = 0;
    template<class Self, class F> static void Fields(Self& command, F&& f)
    {
        f("object", command.object);
        f("value", command.value);
    }
    bool operator==(const SetSecurityHarshness&) const = default;
};

using ObjectControl = std::variant<SetRepairInterval, SetBeerStrength, SetBeerPrice, SetBuildingOpenPolicy, SetBeachOpenPolicy,
        SetCleanerToilets, SetCleanerBins, SetCleanerLitter, SetMechanicRepair, SetMechanicVandals, SetMechanicWater, SetRepGuestTime,
        SetSecurityHarshness>;

// Decode a GUI keyword or old replay input into a typed control; invalid integer/byte words are refused before conversion.
std::optional<ObjectControl> MakeObjectControl(ObjectId object, std::string_view name, float value, const char** error = nullptr);

using Command = std::variant<PlaceBuilding, PlaceScenery, PaintSurface, Dozer, Demolish, HireStaff, DismissStaff, SetStaffWage,
        CopyStaffWage, SetStaffShift, SendStaff, SendStaffTo, SetBuildingOpen, CallRepair, CallCleaner, SetPrice, SetEventPrice, SetBeer,
        SetShow, ResumeGame, BreakObject, SetGameSpeed, SetPaused, SetRepairInterval, SetBeerStrength, SetBeerPrice, SetBuildingOpenPolicy,
        SetBeachOpenPolicy, SetCleanerToilets, SetCleanerBins, SetCleanerLitter, SetMechanicRepair, SetMechanicVandals, SetMechanicWater,
        SetRepGuestTime, SetSecurityHarshness>;

std::string_view Name(const Command& command);

// "name field=value ..." in declared field order (docs/architecture/commands.md, "Text form").
std::string Format(const Command& command);

// Parses the text form. Fields may come in any order; each must appear exactly once. On failure returns nothing and, if `error` is
// given, says why.
std::optional<Command> Parse(std::string_view text, std::string* error = nullptr);

// Value encodings shared with the replay file.
std::string EncodeString(std::string_view value);
std::optional<std::string> DecodeString(std::string_view text);
std::string FormatFloat(float value);
std::optional<float> ParseFloat(std::string_view text);

// Commands submitted between ticks, handed to the simulation in submission order at the start of the next tick.
class CommandQueue {
public:
    void Submit(Command command) { m_pending.push_back(std::move(command)); }
    std::vector<Command> TakePending() { return std::exchange(m_pending, {}); }
    // A world transition (load, state change) drops what was meant for the old world.
    void Clear() { m_pending.clear(); }
    bool Empty() const { return m_pending.empty(); }

private:
    std::vector<Command> m_pending;
};

} // namespace obl::command
