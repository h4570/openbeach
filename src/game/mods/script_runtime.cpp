#include "game/mods/script_runtime.h"

#include "core/pack_path.h"
#include "formats/mod_pack.h"

#include <nlohmann/json.hpp>

// Lua is compiled as C++ (cmake/lua.cmake), so its headers are included without lua.hpp's extern "C": a Lua error is a C++
// exception that unwinds the frames of the functions below.
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <numeric>

namespace obl::mods {

namespace {

// The count hook runs every this many VM instructions; the budget is checked there.
constexpr int kHookStep = 1000;
// Lines one mod may log between Start and Stop; a script printing every frame would bury the log otherwise.
constexpr int kMaxLogLines = 200;
constexpr size_t kMaxStoreKey = 256;
constexpr const char* kStoreFormat = "obl.mod_store";
// Drawing calls one mod may make per frame; the overlay is redrawn every frame, so more is a script that forgot to stop.
constexpr int kMaxOverlayOps = 4096;
// Coordinates beyond this are refused as an error in the script rather than drawn or clicked somewhere far away.
constexpr double kMaxCoordinate = 1 << 20;
// Frames obl.stats.fps averages over.
constexpr size_t kFpsFrames = 30;
constexpr const char* kPlatform =
#if defined(__EMSCRIPTEN__)
        "web";
#elif defined(__ANDROID__)
        "android";
#elif defined(_WIN32)
        "windows";
#elif defined(__APPLE__)
        "macos";
#else
        "linux";
#endif

// The first list (#114): what the HUD's pause and speed controls and the place modes send. Selecting objects, panels and the build
// tool's preview are not commands (docs/architecture/commands.md); they come as script calls of their own.
constexpr std::string_view kCommands[] = {
        "set-paused", "resume-game", "set-game-speed", "place-building", "place-scenery", "paint-surface"};

} // namespace

struct ScriptRuntime::Mod {
    ScriptHost* host = nullptr;
    const ScriptLimits* limits = nullptr;
    std::string id;
    std::filesystem::path directory;
    std::filesystem::path storeFile;
    lua_State* L = nullptr;
    size_t used = 0; // bytes the state holds now
    uint64_t counted = 0; // instructions of the current call, in kHookStep steps
    bool exhausted = false; // the current call used up its budget
    bool running = false;
    int logLines = 0;
    nlohmann::json store = nlohmann::json::object();
    bool storeDirty = false;
    std::vector<settings_schema::Field> fields;
    std::map<std::string, std::string> settings; // setting id -> canonical value, as of the last read
    std::vector<std::string> takesOver;
    bool consumed = false; // the event of the current on_input call was consumed
    int overlayOps = 0; // drawing calls since the frame began
    const std::vector<float>* frameTimes = nullptr;
};

namespace {

using Mod = ScriptRuntime::Mod;

Mod& Of(lua_State* L)
{
    return **static_cast<Mod**>(lua_getextraspace(L));
}

// The memory cap: an allocation that would take the state over it fails, which Lua reports as a memory error.
void* Allocate(void* data, void* block, size_t oldSize, size_t newSize)
{
    Mod& mod = *static_cast<Mod*>(data);
    const size_t old = block ? oldSize : 0; // without a block, oldSize names the kind of object
    if (newSize == 0) {
        mod.used -= old;
        std::free(block);
        return nullptr;
    }
    if (newSize > old && mod.used - old + newSize > mod.limits->memoryPerMod) return nullptr;
    void* grown = std::realloc(block, newSize);
    if (grown) mod.used = mod.used - old + newSize;
    return grown;
}

// The instruction budget. Once it is used up every further instruction fails, also one after a pcall that caught the first error,
// so a script cannot keep running by catching it.
void CountHook(lua_State* L, lua_Debug* ar)
{
    Mod& mod = Of(L);
    if (!mod.exhausted) {
        mod.counted += kHookStep;
        if (mod.counted <= mod.limits->instructionsPerCall) return;
        mod.exhausted = true;
    }
    lua_sethook(L, CountHook, LUA_MASKCOUNT, 1);
    lua_getinfo(L, "Sl", ar);
    std::string where = ar->currentline > 0 ? std::string(ar->short_src) + ":" + std::to_string(ar->currentline) + ": " : "";
    lua_pushstring(L,
            (where + "the instruction budget of " + std::to_string(mod.limits->instructionsPerCall) +
                    " per call is used up (an endless loop?)")
                    .c_str());
    lua_error(L);
}

void Log(Mod& mod, const std::string& text)
{
    if (mod.logLines > kMaxLogLines) return;
    if (++mod.logLines > kMaxLogLines)
        mod.host->Log("mod " + mod.id + ": more than " + std::to_string(kMaxLogLines) + " lines, the rest is dropped");
    else
        mod.host->Log("mod " + mod.id + ": " + text);
}

std::string JoinArguments(lua_State* L, const char* separator)
{
    std::string text;
    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i) {
        size_t size = 0;
        const char* part = luaL_tolstring(L, i, &size);
        if (i > 1) text += separator;
        text.append(part, size);
        lua_pop(L, 1);
    }
    return text;
}

int Print(lua_State* L)
{
    Log(Of(L), JoinArguments(L, "\t"));
    return 0;
}

int ApiLog(lua_State* L)
{
    Log(Of(L), JoinArguments(L, " "));
    return 0;
}

bool IsFieldName(std::string_view name)
{
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

// obl.command(name, fields): the command's text form built from the table, then the parser the replays use, so a script's command
// is checked exactly like a test script's `command` line.
int ApiCommand(lua_State* L)
{
    const std::string name = luaL_checkstring(L, 1);
    const auto& list = kCommands;
    if (std::find(std::begin(list), std::end(list), name) == std::end(list))
        return luaL_error(L, "obl.command: \"%s\" is not a command open to scripts", name.c_str());
    std::string text = name;
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING || !IsFieldName(lua_tostring(L, -2)))
                return luaL_error(L, "obl.command: \"%s\": a field name is not a lowercase name", name.c_str());
            std::string value;
            switch (lua_type(L, -1)) {
            case LUA_TBOOLEAN: value = lua_toboolean(L, -1) ? "1" : "0"; break;
            case LUA_TNUMBER:
                if (lua_isinteger(L, -1))
                    value = std::to_string(lua_tointeger(L, -1));
                else {
                    // An integral float (2.0) is written as an integer, so integer fields accept it.
                    const lua_Number number = lua_tonumber(L, -1);
                    lua_Integer whole = 0;
                    value = lua_numbertointeger(number, &whole) && static_cast<lua_Number>(whole) == number
                            ? std::to_string(whole)
                            : command::FormatFloat(static_cast<float>(number));
                }
                break;
            case LUA_TSTRING: value = command::EncodeString(lua_tostring(L, -1)); break;
            default:
                return luaL_error(L, "obl.command: \"%s\": field %s is not a boolean, number or string", name.c_str(), lua_tostring(L, -2));
            }
            text += " " + std::string(lua_tostring(L, -2)) + "=" + value;
            lua_pop(L, 1);
        }
    }
    std::string error;
    std::optional<command::Command> parsed = command::Parse(text, &error);
    if (!parsed) return luaL_error(L, "obl.command: %s", error.c_str());
    Of(L).host->SubmitCommand(std::move(*parsed));
    return 0;
}

int ApiInWorld(lua_State* L)
{
    lua_pushboolean(L, Of(L).host->InWorld());
    return 1;
}

size_t StoreSize(const nlohmann::json& store)
{
    size_t size = 0;
    for (const auto& [key, value] : store.items())
        size += key.size() + (value.is_string() ? value.get_ref<const std::string&>().size() : 8);
    return size;
}

std::string StoreKey(lua_State* L)
{
    size_t size = 0;
    const char* key = luaL_checklstring(L, 1, &size);
    if (size == 0 || size > kMaxStoreKey) luaL_error(L, "obl.store: a key is 1 to %d bytes", static_cast<int>(kMaxStoreKey));
    return std::string(key, size);
}

int ApiStoreGet(lua_State* L)
{
    const std::string key = StoreKey(L);
    const nlohmann::json& store = Of(L).store;
    const auto it = store.find(key);
    if (it == store.end())
        lua_pushnil(L);
    else if (it->is_boolean())
        lua_pushboolean(L, it->get<bool>());
    else if (it->is_number_integer())
        lua_pushinteger(L, it->get<lua_Integer>());
    else if (it->is_number())
        lua_pushnumber(L, it->get<lua_Number>());
    else
        lua_pushstring(L, it->get_ref<const std::string&>().c_str());
    return 1;
}

int ApiStoreSet(lua_State* L)
{
    const std::string key = StoreKey(L);
    Mod& mod = Of(L);
    nlohmann::json value;
    switch (lua_type(L, 2)) {
    case LUA_TNIL:
    case LUA_TNONE:
        mod.store.erase(key);
        mod.storeDirty = true;
        return 0;
    case LUA_TBOOLEAN: value = static_cast<bool>(lua_toboolean(L, 2)); break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, 2))
            value = static_cast<int64_t>(lua_tointeger(L, 2));
        else
            value = static_cast<double>(lua_tonumber(L, 2));
        break;
    case LUA_TSTRING: value = std::string(lua_tostring(L, 2)); break;
    default: return luaL_error(L, "obl.store: a value is nil, a boolean, a number or a string");
    }
    nlohmann::json changed = mod.store;
    changed[key] = std::move(value);
    if (StoreSize(changed) > mod.limits->storeBytes)
        return luaL_error(L, "obl.store: the store would exceed %d bytes", static_cast<int>(mod.limits->storeBytes));
    mod.store = std::move(changed);
    mod.storeDirty = true;
    return 0;
}

const settings_schema::Field* FindField(const Mod& mod, const std::string& id)
{
    const auto it = std::find_if(mod.fields.begin(), mod.fields.end(), [&](const auto& field) { return field.id == id; });
    return it == mod.fields.end() ? nullptr : &*it;
}

// A setting as a script sees it: a toggle is a boolean, a choice its option's value (a string), a number a Lua integer when every
// value of the setting is whole, else a float.
void PushSetting(lua_State* L, const settings_schema::Field& field, const std::string& value)
{
    switch (field.kind) {
    case settings_schema::Kind::Toggle: lua_pushboolean(L, settings_schema::IsOn(value)); break;
    case settings_schema::Kind::Choice: lua_pushlstring(L, value.data(), value.size()); break;
    case settings_schema::Kind::Number:
        if (settings_schema::IsWhole(field))
            lua_pushinteger(L, static_cast<lua_Integer>(std::strtod(value.c_str(), nullptr)));
        else
            lua_pushnumber(L, std::strtod(value.c_str(), nullptr));
        break;
    }
}

// obl.settings.get(id): the mod's own setting, the player's value or the default; an id the mod did not declare is an error.
int ApiSettingsGet(lua_State* L)
{
    const std::string id = luaL_checkstring(L, 1);
    const Mod& mod = Of(L);
    const settings_schema::Field* field = FindField(mod, id);
    if (!field) return luaL_error(L, "obl.settings.get: \"%s\" is not a setting this mod declares in mod.json", id.c_str());
    PushSetting(L, *field, mod.settings.at(id));
    return 1;
}

// ---- input ---------------------------------------------------------------------------------------------------------------------

// A coordinate argument: any number, rounded down, so `e.x / 2` works as well as an integer.
int CheckCoordinate(lua_State* L, int index)
{
    const lua_Number value = luaL_checknumber(L, index);
    if (!(std::fabs(value) <= kMaxCoordinate)) luaL_argerror(L, index, "a coordinate out of range");
    return static_cast<int>(std::floor(value));
}

int CheckButton(lua_State* L, int index)
{
    const std::string_view button = luaL_optstring(L, index, "left");
    if (button == "left") return 0;
    if (button == "right") return 1;
    return luaL_argerror(L, index, "the button is \"left\" or \"right\"");
}

int SynthesizeAt(lua_State* L, SyntheticInput::Kind kind)
{
    SyntheticInput input;
    input.kind = kind;
    input.x = CheckCoordinate(L, 1);
    input.y = CheckCoordinate(L, 2);
    input.button = CheckButton(L, 3);
    Of(L).host->SynthesizeInput(input);
    return 0;
}

int ApiInputClick(lua_State* L)
{
    return SynthesizeAt(L, SyntheticInput::Kind::Click);
}

int ApiInputPress(lua_State* L)
{
    return SynthesizeAt(L, SyntheticInput::Kind::Press);
}

int ApiInputRelease(lua_State* L)
{
    return SynthesizeAt(L, SyntheticInput::Kind::Release);
}

// obl.input.move(x, y) holds the pointer there; obl.input.move() gives it back to the device.
int ApiInputMove(lua_State* L)
{
    SyntheticInput input;
    if (lua_isnoneornil(L, 1)) {
        input.kind = SyntheticInput::Kind::Free;
    } else {
        input.kind = SyntheticInput::Kind::Move;
        input.x = CheckCoordinate(L, 1);
        input.y = CheckCoordinate(L, 2);
    }
    Of(L).host->SynthesizeInput(input);
    return 0;
}

int ApiInputLastPointer(lua_State* L)
{
    const std::string pointer = Of(L).host->LastPointer();
    if (pointer.empty())
        lua_pushnil(L);
    else
        lua_pushstring(L, pointer.c_str());
    return 1;
}

// `e:consume()` in on_input: the event goes no further, to earlier mods or the game.
int EventConsume(lua_State* L)
{
    Of(L).consumed = true;
    return 0;
}

int ObservedConsume(lua_State* L)
{
    return luaL_error(L, "on_input_observe cannot consume an event; consume it in on_input");
}

// ---- camera --------------------------------------------------------------------------------------------------------------------

constexpr double kZoomScales[] = {2.0, 1.0, 0.5}; // by zoom level, as the renderer scales the view

int ApiCameraPosition(lua_State* L)
{
    const std::optional<CameraView> camera = Of(L).host->Camera();
    if (!camera) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, camera->x);
    lua_pushinteger(L, camera->y);
    return 2;
}

int ApiCameraSetPosition(lua_State* L)
{
    const int x = CheckCoordinate(L, 1), y = CheckCoordinate(L, 2);
    lua_pushboolean(L, Of(L).host->SetCameraPosition(x, y));
    return 1;
}

int ApiCameraPan(lua_State* L)
{
    const int dx = CheckCoordinate(L, 1), dy = CheckCoordinate(L, 2);
    Mod& mod = Of(L);
    const std::optional<CameraView> camera = mod.host->Camera();
    lua_pushboolean(L, camera && mod.host->SetCameraPosition(camera->x + dx, camera->y + dy));
    return 1;
}

int ApiCameraZoom(lua_State* L)
{
    const std::optional<CameraView> camera = Of(L).host->Camera();
    if (!camera) {
        lua_pushnil(L);
        return 1;
    }
    // 2 and 1 as integers, so they print as "2" and "1".
    const double scale = kZoomScales[std::clamp(camera->zoomLevel, 0, 2)];
    if (scale >= 1)
        lua_pushinteger(L, static_cast<lua_Integer>(scale));
    else
        lua_pushnumber(L, scale);
    return 1;
}

// The game has three zoom levels; the scale picks the nearest (on a log scale, so 1.4 is nearer 1 than 2).
int ApiCameraSetZoom(lua_State* L)
{
    const lua_Number scale = luaL_checknumber(L, 1);
    if (!(scale > 0)) return luaL_argerror(L, 1, "the zoom is a scale above 0");
    int best = 1;
    for (int level = 0; level < 3; ++level)
        if (std::fabs(std::log(scale / kZoomScales[level])) < std::fabs(std::log(scale / kZoomScales[best]))) best = level;
    lua_pushboolean(L, Of(L).host->SetZoomLevel(best));
    return 1;
}

// ---- GUI -----------------------------------------------------------------------------------------------------------------------

int RunGui(lua_State* L, GuiRequest& request)
{
    lua_pushboolean(L, Of(L).host->Gui(request));
    return 1;
}

int GuiByName(lua_State* L, GuiRequest::Op op, bool position)
{
    GuiRequest request;
    request.op = op;
    request.name = luaL_checkstring(L, 1);
    if (position) {
        request.x = CheckCoordinate(L, 2);
        request.y = CheckCoordinate(L, 3);
    }
    return RunGui(L, request);
}

int ApiGuiShow(lua_State* L)
{
    return GuiByName(L, GuiRequest::Op::ShowControl, false);
}

int ApiGuiHide(lua_State* L)
{
    return GuiByName(L, GuiRequest::Op::HideControl, false);
}

int ApiGuiMove(lua_State* L)
{
    return GuiByName(L, GuiRequest::Op::MoveControl, true);
}

int ApiGuiOpen(lua_State* L)
{
    return GuiByName(L, GuiRequest::Op::OpenDialog, false);
}

int ApiGuiClose(lua_State* L)
{
    return GuiByName(L, GuiRequest::Op::CloseDialog, false);
}

int ApiGuiMoveDialog(lua_State* L)
{
    return GuiByName(L, GuiRequest::Op::MoveDialog, true);
}

int ApiGuiSelect(lua_State* L)
{
    GuiRequest request;
    request.op = GuiRequest::Op::Select;
    const lua_Integer id = luaL_checkinteger(L, 1);
    if (id < 0 || id > static_cast<lua_Integer>(UINT32_MAX)) return luaL_argerror(L, 1, "not an object id");
    request.id = static_cast<uint32_t>(id);
    return RunGui(L, request);
}

int ApiGuiPlace(lua_State* L)
{
    GuiRequest request;
    request.op = GuiRequest::Op::Place;
    request.kind = luaL_checkstring(L, 1);
    if (request.kind != "building" && request.kind != "scenery") return luaL_argerror(L, 1, "the kind is \"building\" or \"scenery\"");
    request.name = luaL_checkstring(L, 2);
    return RunGui(L, request);
}

int ApiGuiCancel(lua_State* L)
{
    GuiRequest request;
    request.op = GuiRequest::Op::CancelTool;
    return RunGui(L, request);
}

// ---- overlay -------------------------------------------------------------------------------------------------------------------

uint32_t CheckColor(lua_State* L, int index)
{
    const lua_Integer color = luaL_checkinteger(L, index);
    if (color < 0 || color > 0xFFFFFFFFll) luaL_argerror(L, index, "a color is 0xAARRGGBB");
    return static_cast<uint32_t>(color);
}

void Draw(lua_State* L, const OverlayOp& op)
{
    Mod& mod = Of(L);
    if (++mod.overlayOps > kMaxOverlayOps) luaL_error(L, "obl.overlay: more than %d drawing calls in one frame", kMaxOverlayOps);
    mod.host->Draw(op);
}

int Box(lua_State* L, OverlayOp::Kind kind)
{
    OverlayOp op;
    op.kind = kind;
    op.x = CheckCoordinate(L, 1);
    op.y = CheckCoordinate(L, 2);
    op.w = CheckCoordinate(L, 3);
    op.h = CheckCoordinate(L, 4);
    op.color = CheckColor(L, 5);
    Draw(L, op);
    return 0;
}

int ApiOverlayRect(lua_State* L)
{
    return Box(L, OverlayOp::Kind::Rect);
}

int ApiOverlayFrame(lua_State* L)
{
    return Box(L, OverlayOp::Kind::Frame);
}

int ApiOverlayText(lua_State* L)
{
    OverlayOp op;
    op.kind = OverlayOp::Kind::Text;
    op.x = CheckCoordinate(L, 1);
    op.y = CheckCoordinate(L, 2);
    op.text = luaL_checkstring(L, 3);
    op.font = luaL_checkstring(L, 4);
    op.color = static_cast<uint32_t>(luaL_opt(L, CheckColor, 5, 0xFFFFFFFFll));
    if (!Of(L).host->MeasureText(op.text, op.font)) return luaL_error(L, "obl.overlay.text: the pack has no font \"%s\"", op.font.c_str());
    Draw(L, op);
    return 0;
}

int ApiOverlayMeasure(lua_State* L)
{
    const std::string text = luaL_checkstring(L, 1), font = luaL_checkstring(L, 2);
    const auto size = Of(L).host->MeasureText(text, font);
    if (!size) return luaL_error(L, "obl.overlay.measure: the pack has no font \"%s\"", font.c_str());
    lua_pushinteger(L, size->first);
    lua_pushinteger(L, size->second);
    return 2;
}

bool IsImageName(std::string_view name)
{
    return name.size() > 4 && name.ends_with(".png") && name.size() <= 128 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    }) && name.find("..") == std::string_view::npos;
}

// obl.overlay.image(x, y, name [, w, h]): a PNG of the mod's overlay/, at its size or scaled to w x h.
int ApiOverlayImage(lua_State* L)
{
    OverlayOp op;
    op.kind = OverlayOp::Kind::Image;
    op.x = CheckCoordinate(L, 1);
    op.y = CheckCoordinate(L, 2);
    const std::string name = luaL_checkstring(L, 3);
    if (!IsImageName(name)) return luaL_argerror(L, 3, "an image is a file name in overlay/ (lowercase, ending in .png)");
    if (!lua_isnoneornil(L, 4)) {
        op.w = CheckCoordinate(L, 4);
        op.h = CheckCoordinate(L, 5);
    }
    const auto shipped = Of(L).directory / mod_pack::kOverlayImages / name;
    std::error_code ec;
    if (std::filesystem::is_regular_file(shipped, ec)) {
        op.image = shipped;
    } else if (const auto cache = pack_path::ModCacheDirectory(Of(L).directory.parent_path().parent_path(), Of(L).id)) {
        const auto cached = *cache / mod_pack::kOverlayImages / name;
        op.image = std::filesystem::is_regular_file(cached, ec) ? cached : shipped;
    } else {
        op.image = shipped;
    }
    Draw(L, op);
    return 0;
}

// obl.overlay.view(sx, sy, sw, sh, dx, dy, dw, dh): the frame's region (sx, sy, sw, sh) scaled into (dx, dy, dw, dh), a magnifier.
int ApiOverlayView(lua_State* L)
{
    OverlayOp op;
    op.kind = OverlayOp::Kind::View;
    op.sx = CheckCoordinate(L, 1);
    op.sy = CheckCoordinate(L, 2);
    op.sw = CheckCoordinate(L, 3);
    op.sh = CheckCoordinate(L, 4);
    op.x = CheckCoordinate(L, 5);
    op.y = CheckCoordinate(L, 6);
    op.w = CheckCoordinate(L, 7);
    op.h = CheckCoordinate(L, 8);
    Draw(L, op);
    return 0;
}

// ---- screen and frame statistics -----------------------------------------------------------------------------------------------

int ApiScreenSize(lua_State* L)
{
    const ScreenInfo screen = Of(L).host->Screen();
    lua_pushinteger(L, screen.width);
    lua_pushinteger(L, screen.height);
    return 2;
}

int ApiScreenAspect(lua_State* L)
{
    const ScreenInfo screen = Of(L).host->Screen();
    lua_pushnumber(L, screen.height > 0 ? static_cast<lua_Number>(screen.width) / screen.height : 0.0);
    return 1;
}

void SetInteger(lua_State* L, const char* field, lua_Integer value)
{
    lua_pushinteger(L, value);
    lua_setfield(L, -2, field);
}

void SetNumber(lua_State* L, const char* field, lua_Number value)
{
    lua_pushnumber(L, value);
    lua_setfield(L, -2, field);
}

void SetText(lua_State* L, const char* field, const std::string& value)
{
    lua_pushstring(L, value.c_str());
    lua_setfield(L, -2, field);
}

int ApiScreenSafeArea(lua_State* L)
{
    const ScreenInfo screen = Of(L).host->Screen();
    lua_createtable(L, 0, 4);
    SetInteger(L, "left", screen.safeLeft);
    SetInteger(L, "top", screen.safeTop);
    SetInteger(L, "right", screen.safeRight);
    SetInteger(L, "bottom", screen.safeBottom);
    return 1;
}

int ApiStatsFrameMs(lua_State* L)
{
    const std::vector<float>& times = *Of(L).frameTimes;
    lua_pushnumber(L, times.empty() ? 0.0 : static_cast<lua_Number>(times.back()) * 1000.0);
    return 1;
}

int ApiStatsFps(lua_State* L)
{
    const std::vector<float>& times = *Of(L).frameTimes;
    const double total = std::accumulate(times.begin(), times.end(), 0.0);
    lua_pushnumber(L, total > 0 ? static_cast<lua_Number>(times.size()) / total : 0.0);
    return 1;
}

// ---- game state ----------------------------------------------------------------------------------------------------------------

void PushObject(lua_State* L, const ObjectInfo& object)
{
    lua_createtable(L, 0, 6);
    SetInteger(L, "id", object.id);
    SetText(L, "kind", object.kind);
    if (!object.key.empty()) SetText(L, "key", object.key);
    SetInteger(L, "x", object.x);
    SetInteger(L, "y", object.y);
    SetInteger(L, "state", object.state);
}

int ApiGameState(lua_State* L)
{
    const GameSnapshot* game = Of(L).host->Snapshot();
    if (!game) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 8);
    SetNumber(L, "money", game->money);
    SetInteger(L, "day", game->day);
    SetNumber(L, "hour", game->hour);
    SetInteger(L, "speed", game->speedLevel);
    lua_pushboolean(L, game->paused);
    lua_setfield(L, -2, "paused");
    SetText(L, "weather", game->weather);
    SetInteger(L, "guests", game->guests);
    SetNumber(L, "mood", game->mood);
    return 1;
}

int ApiGameBuildings(lua_State* L)
{
    const GameSnapshot* game = Of(L).host->Snapshot();
    if (!game) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, static_cast<int>(game->buildings.size()), 0);
    for (size_t i = 0; i < game->buildings.size(); ++i) {
        PushObject(L, game->buildings[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int PushOptionalObject(lua_State* L, const std::optional<ObjectInfo> GameSnapshot::* field)
{
    const GameSnapshot* game = Of(L).host->Snapshot();
    if (!game || !(game->*field)) {
        lua_pushnil(L);
        return 1;
    }
    PushObject(L, *(game->*field));
    return 1;
}

int ApiGameSelected(lua_State* L)
{
    return PushOptionalObject(L, &GameSnapshot::selected);
}

int ApiGameHovered(lua_State* L)
{
    return PushOptionalObject(L, &GameSnapshot::hovered);
}

void SetFunctions(lua_State* L, std::initializer_list<std::pair<const char*, lua_CFunction>> functions)
{
    for (const auto& [name, function] : functions) {
        lua_pushcfunction(L, function);
        lua_setfield(L, -2, name);
    }
}

// The `obl` table: the whole script API. RegisteredApi walks it, and the reference page lists the same names.
void SetTable(lua_State* L, const char* name, std::initializer_list<std::pair<const char*, lua_CFunction>> functions)
{
    lua_newtable(L);
    SetFunctions(L, functions);
    lua_setfield(L, -2, name);
}

void RegisterApi(lua_State* L)
{
    lua_newtable(L);
    lua_pushinteger(L, mod_pack::kPresentationApi);
    lua_setfield(L, -2, "api");
    SetFunctions(L, {{"log", ApiLog}, {"command", ApiCommand}});
    SetTable(L, "game",
            {{"in_world", ApiInWorld}, {"state", ApiGameState}, {"buildings", ApiGameBuildings}, {"selected", ApiGameSelected},
                    {"hovered", ApiGameHovered}});
    SetTable(L, "store", {{"get", ApiStoreGet}, {"set", ApiStoreSet}});
    SetTable(L, "settings", {{"get", ApiSettingsGet}});
    // Input, camera, GUI, overlay and information (docs/modding/reference/scripting.md).
    lua_pushstring(L, kPlatform);
    lua_setfield(L, -2, "platform");
    SetTable(L, "input",
            {{"click", ApiInputClick}, {"press", ApiInputPress}, {"release", ApiInputRelease}, {"move", ApiInputMove},
                    {"last_pointer", ApiInputLastPointer}});
    SetTable(L, "camera",
            {{"position", ApiCameraPosition}, {"set_position", ApiCameraSetPosition}, {"pan", ApiCameraPan}, {"zoom", ApiCameraZoom},
                    {"set_zoom", ApiCameraSetZoom}});
    SetTable(L, "gui",
            {{"show", ApiGuiShow}, {"hide", ApiGuiHide}, {"move", ApiGuiMove}, {"open", ApiGuiOpen}, {"close", ApiGuiClose},
                    {"move_dialog", ApiGuiMoveDialog}, {"select", ApiGuiSelect}, {"place", ApiGuiPlace}, {"cancel", ApiGuiCancel}});
    SetTable(L, "overlay",
            {{"rect", ApiOverlayRect}, {"frame", ApiOverlayFrame}, {"text", ApiOverlayText}, {"measure", ApiOverlayMeasure},
                    {"image", ApiOverlayImage}, {"view", ApiOverlayView}});
    SetTable(L, "screen", {{"size", ApiScreenSize}, {"aspect", ApiScreenAspect}, {"safe_area", ApiScreenSafeArea}});
    SetTable(L, "stats", {{"frame_ms", ApiStatsFrameMs}, {"fps", ApiStatsFps}});
    lua_setglobal(L, "obl");
}

void RemoveField(lua_State* L, const char* table, const char* field)
{
    lua_getglobal(L, table);
    lua_pushnil(L);
    lua_setfield(L, -2, field);
    lua_pop(L, 1);
}

// The sandbox: only libraries without files, processes, the clock or code loading (cmake/lua.cmake does not even build io, os,
// package and debug), minus the functions of those that still reach outside or load bytecode.
int OpenSandbox(lua_State* L)
{
    const std::pair<const char*, lua_CFunction> libraries[] = {{LUA_GNAME, luaopen_base}, {LUA_COLIBNAME, luaopen_coroutine},
            {LUA_MATHLIBNAME, luaopen_math}, {LUA_STRLIBNAME, luaopen_string}, {LUA_TABLIBNAME, luaopen_table},
            {LUA_UTF8LIBNAME, luaopen_utf8}};
    for (const auto& [name, open] : libraries) {
        luaL_requiref(L, name, open, 1);
        lua_pop(L, 1);
    }
    for (const char* name : {"dofile", "loadfile", "load", "collectgarbage"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    RemoveField(L, LUA_STRLIBNAME, "dump");
    // math.random starts from a fixed seed instead of the clock, and randomseed() would read the clock again.
    lua_getglobal(L, LUA_MATHLIBNAME);
    lua_getfield(L, -1, "randomseed");
    lua_pushinteger(L, 0);
    lua_call(L, 1, 0);
    lua_pop(L, 1);
    RemoveField(L, LUA_MATHLIBNAME, "randomseed");
    lua_pushcfunction(L, Print);
    lua_setglobal(L, "print");
    RegisterApi(L);
    return 0;
}

struct LoadRequest {
    const std::string* text;
    const std::string* chunk;
    bool compileOnly;
};

// Text chunks only: a precompiled chunk could break the interpreter's memory safety.
int LoadAndRun(lua_State* L)
{
    const auto& request = *static_cast<const LoadRequest*>(lua_touserdata(L, 1));
    if (luaL_loadbufferx(L, request.text->data(), request.text->size(), request.chunk->c_str(), "t") != LUA_OK) return lua_error(L);
    if (!request.compileOnly) lua_call(L, 0, 0);
    return 0;
}

struct DispatchRequest {
    const char* function;
    float dt = 0;
    const InputEvent* event = nullptr;
    const char* setting = nullptr; // on_setting: the id of the changed setting
    const std::string* consumedBy = nullptr; // on_input_observe: the mod that consumed the event, empty when none did
    const std::string* action = nullptr; // on_action
};

void PushEvent(lua_State* L, const DispatchRequest& request)
{
    const InputEvent& e = *request.event;
    lua_createtable(L, 0, 8);
    SetText(L, "kind", e.kind);
    SetText(L, "phase", e.phase);
    if (!e.key.empty()) SetText(L, "key", e.key);
    if (!e.button.empty()) SetText(L, "button", e.button);
    if (e.kind != "key") {
        SetInteger(L, "x", e.x);
        SetInteger(L, "y", e.y);
    }
    if (e.kind == "touch") SetInteger(L, "finger", e.finger);
    if (e.kind == "wheel") SetNumber(L, "delta", e.delta);
    if (request.consumedBy) {
        if (!request.consumedBy->empty()) SetText(L, "consumed_by", *request.consumedBy);
        lua_pushcfunction(L, ObservedConsume);
    } else {
        lua_pushcfunction(L, EventConsume);
    }
    lua_setfield(L, -2, "consume");
}

// Calls the global `function` when the script defines one. Raw access, so a metatable on _G cannot run code outside the call.
int Dispatch(lua_State* L)
{
    const auto& request = *static_cast<const DispatchRequest*>(lua_touserdata(L, 1));
    lua_settop(L, 0);
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_pushstring(L, request.function);
    lua_rawget(L, -2);
    if (!lua_isfunction(L, -1)) return 0;
    int args = 0;
    if (request.setting) {
        const Mod& mod = Of(L);
        lua_pushstring(L, request.setting);
        PushSetting(L, *FindField(mod, request.setting), mod.settings.at(request.setting));
        args = 2;
    } else if (request.event) {
        PushEvent(L, request);
        args = 1;
    } else if (request.action) {
        lua_pushstring(L, request.action->c_str());
        args = 1;
    } else if (std::string_view(request.function) == "on_frame") {
        lua_pushnumber(L, request.dt);
        args = 1;
    }
    lua_call(L, args, 0);
    return 0;
}

// Runs `function(L)` protected with a fresh budget; false with `error` when it failed or used up its budget.
bool Protected(Mod& mod, lua_CFunction function, void* data, std::string& error)
{
    lua_State* L = mod.L;
    mod.counted = 0;
    mod.exhausted = false;
    lua_sethook(L, CountHook, LUA_MASKCOUNT, kHookStep);
    lua_settop(L, 0);
    lua_pushcfunction(L, function);
    lua_pushlightuserdata(L, data);
    const int status = lua_pcall(L, 1, 0, 0);
    if (status == LUA_ERRMEM)
        error = "the memory cap of " + std::to_string(mod.limits->memoryPerMod) + " bytes is reached";
    else if (status != LUA_OK)
        error = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : std::string("an error that is not a string");
    else if (mod.exhausted) // caught by the script, but the budget is gone all the same
        error = "the instruction budget of " + std::to_string(mod.limits->instructionsPerCall) + " per call is used up";
    lua_settop(L, 0);
    return status == LUA_OK && !mod.exhausted;
}

void LoadStore(Mod& mod)
{
    std::ifstream in(mod.storeFile, std::ios::binary);
    if (!in) return;
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    const bool valid = doc.is_object() && doc.value("format", "") == kStoreFormat && doc.value("version", 0) == 1 &&
            doc.contains("values") && doc["values"].is_object();
    if (!valid) {
        mod.host->Log("mod " + mod.id + ": " + mod.storeFile.generic_string() + " is not a store; starting empty");
        return;
    }
    for (const auto& [key, value] : doc["values"].items())
        if (!key.empty() && key.size() <= kMaxStoreKey && (value.is_boolean() || value.is_number() || value.is_string()))
            mod.store[key] = value;
}

// Reads the mod's settings from the host: the canonical text of a valid value, else the default. Returns the ids whose value changed.
std::vector<std::string> ReadSettings(Mod& mod)
{
    std::vector<std::string> changed;
    for (const auto& field : mod.fields) {
        const std::optional<std::string> text = mod.host->ReadSetting(mod.id, field.id);
        if (text && !settings_schema::Canonical(field, *text) && !mod.settings.count(field.id))
            mod.host->Log("mod " + mod.id + ": the setting " + field.id + " holds \"" + *text +
                    "\", which is not a value of it; using the default");
        const std::string value = settings_schema::ValueOrDefault(field, text);
        const auto old = mod.settings.find(field.id);
        if (old != mod.settings.end() && old->second != value) changed.push_back(field.id);
        mod.settings[field.id] = value;
    }
    return changed;
}

void WriteStore(Mod& mod)
{
    if (!mod.storeDirty || mod.storeFile.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(mod.storeFile.parent_path(), ec);
    std::ofstream out(mod.storeFile, std::ios::binary | std::ios::trunc);
    out << nlohmann::json{{"format", kStoreFormat}, {"version", 1}, {"values", mod.store}}.dump(1) << '\n';
    if (!out) mod.host->Log("mod " + mod.id + ": cannot write " + mod.storeFile.generic_string());
    mod.storeDirty = false;
}

void Close(Mod& mod)
{
    if (mod.L) lua_close(mod.L);
    mod.L = nullptr;
}

// The .lua files of `directory`/scripts/presentation in name order.
std::vector<std::filesystem::path> ScriptFiles(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(directory / mod_pack::kPresentationScripts, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileError;
        if (it->path().extension() == ".lua" && it->is_regular_file(fileError)) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

lua_State* NewState(Mod& mod)
{
    lua_State* L = lua_newstate(Allocate, &mod);
    if (L) *static_cast<Mod**>(lua_getextraspace(L)) = &mod;
    return L;
}

void CollectNames(lua_State* L, const std::string& prefix, std::vector<std::string>& names)
{
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const std::string name = prefix + "." + lua_tostring(L, -2);
            if (lua_istable(L, -1))
                CollectNames(L, name, names);
            else
                names.push_back(name);
        }
        lua_pop(L, 1);
    }
}

int WalkApi(lua_State* L)
{
    auto& names = *static_cast<std::vector<std::string>*>(lua_touserdata(L, 1));
    OpenSandbox(L);
    lua_getglobal(L, "obl");
    CollectNames(L, "obl", names);
    return 0;
}

} // namespace

ScriptRuntime::ScriptRuntime(ScriptHost& host, ScriptLimits limits) : m_host(host), m_limits(limits) {}

ScriptRuntime::~ScriptRuntime()
{
    Stop();
}

void ScriptRuntime::SwitchOff(Mod& mod, const std::string& why)
{
    mod.running = false;
    const std::string message = "mod \"" + mod.id + "\" is switched off: " + why;
    m_host.Log("presentation scripts: " + message);
    m_failures.push_back(message);
    WriteStore(mod);
    Close(mod);
}

void ScriptRuntime::Start(std::span<const ScriptMod> mods, bool checking)
{
    Stop();
    m_failures.clear();
    for (const ScriptMod& source : mods) {
        const std::vector<std::filesystem::path> files = ScriptFiles(source.directory);
        if (files.empty()) continue;
        auto mod = std::make_unique<Mod>();
        mod->host = &m_host;
        mod->limits = &m_limits;
        mod->id = source.id;
        mod->directory = source.directory;
        if (!checking) mod->storeFile = source.storeFile;
        mod->fields = source.settings;
        ReadSettings(*mod);
        mod->takesOver = source.takesOver;
        mod->frameTimes = &m_frameTimes;
        LoadStore(*mod);
        Mod& m = *m_mods.emplace_back(std::move(mod));
        m.L = NewState(m);
        std::string error;
        if (!m.L || !Protected(m, OpenSandbox, nullptr, error)) {
            SwitchOff(m, m.L ? error : "no memory for the interpreter");
            continue;
        }
        m.running = true;
        for (const auto& file : files) {
            const std::string chunk = "@" + file.lexically_relative(source.directory).generic_string();
            std::ifstream in(file, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            LoadRequest request{&text, &chunk, !m.running};
            if (!in.good() && !in.eof())
                error = chunk.substr(1) + ": cannot be read";
            else if (Protected(m, LoadAndRun, &request, error))
                continue;
            if (!checking) {
                SwitchOff(m, error);
                break;
            }
            m.running = false;
            m_failures.push_back("mod \"" + m.id + "\" (" + chunk.substr(1) + "): " + error);
        }
        if (m.running) m_host.Log("presentation scripts: mod \"" + m.id + "\" loaded " + std::to_string(files.size()) + " file(s)");
    }
    CallAll(false, DispatchRequest{"on_start"});
}

template<class Request> void ScriptRuntime::CallAll(bool reverse, const Request& request)
{
    const auto run = [&](Mod& mod) {
        if (!mod.running) return;
        std::string error;
        DispatchRequest copy = request;
        if (!Protected(mod, Dispatch, &copy, error)) SwitchOff(mod, error);
    };
    if (reverse)
        for (auto it = m_mods.rbegin(); it != m_mods.rend(); ++it) run(**it);
    else
        for (auto& mod : m_mods) run(*mod);
}

void ScriptRuntime::Frame(float dt)
{
    m_frameTimes.push_back(dt);
    if (m_frameTimes.size() > kFpsFrames) m_frameTimes.erase(m_frameTimes.begin());
    for (auto& mod : m_mods) mod->overlayOps = 0;
    DispatchRequest request{"on_frame"};
    request.dt = dt;
    CallAll(false, request);
}

std::string ScriptRuntime::Input(const InputEvent& event)
{
    // The owner of the event's area gets every event of it, before the cooperative order.
    const std::string_view area = event.kind == "touch"        ? "input.touch"
            : event.kind == "mouse" || event.kind == "pointer" ? "input.mouse_drag"
                                                               : "";
    const std::string owner = area.empty() ? std::string() : AreaOwner(area);
    std::vector<Mod*> order;
    for (auto it = m_mods.rbegin(); it != m_mods.rend(); ++it)
        if ((*it)->id == owner)
            order.insert(order.begin(), it->get());
        else
            order.push_back(it->get());
    std::string consumedBy;
    DispatchRequest request{"on_input"};
    request.event = &event;
    for (Mod* mod : order) {
        if (!mod->running) continue;
        mod->consumed = false;
        std::string error;
        if (!Protected(*mod, Dispatch, &request, error)) {
            SwitchOff(*mod, error);
            continue;
        }
        if (mod->consumed) {
            consumedBy = mod->id;
            break;
        }
    }
    DispatchRequest observe{"on_input_observe"};
    observe.event = &event;
    observe.consumedBy = &consumedBy;
    CallAll(true, observe);
    return consumedBy;
}

void ScriptRuntime::Action(std::string_view modId, std::string_view event)
{
    const std::string text(event);
    DispatchRequest request{"on_action"};
    request.action = &text;
    for (auto& mod : m_mods) {
        if (!mod->running || mod->id != modId) continue;
        std::string error;
        if (!Protected(*mod, Dispatch, &request, error)) SwitchOff(*mod, error);
    }
}

void ScriptRuntime::SettingsChanged()
{
    for (auto& mod : m_mods) {
        if (!mod->running) continue;
        for (const std::string& id : ReadSettings(*mod)) {
            if (!mod->running) break;
            DispatchRequest request{"on_setting"};
            request.setting = id.c_str();
            std::string error;
            if (!Protected(*mod, Dispatch, &request, error)) SwitchOff(*mod, error);
        }
    }
}

void ScriptRuntime::Stop()
{
    CallAll(true, DispatchRequest{"on_stop"});
    for (auto& mod : m_mods) {
        WriteStore(*mod);
        Close(*mod);
    }
    m_mods.clear();
}

std::vector<std::string> ScriptRuntime::RunningMods() const
{
    std::vector<std::string> ids;
    for (const auto& mod : m_mods)
        if (mod->running) ids.push_back(mod->id);
    return ids;
}

std::string ScriptRuntime::AreaOwner(std::string_view area) const
{
    for (auto it = m_mods.rbegin(); it != m_mods.rend(); ++it) {
        const Mod& mod = **it;
        if (mod.running && std::find(mod.takesOver.begin(), mod.takesOver.end(), area) != mod.takesOver.end()) return mod.id;
    }
    return {};
}

std::span<const std::string_view> ScriptCommands()
{
    return kCommands;
}

std::vector<std::string> RegisteredApi()
{
    ScriptLimits limits;
    std::vector<float> frameTimes;
    Mod mod;
    mod.limits = &limits;
    mod.frameTimes = &frameTimes;
    mod.id = "api";
    std::vector<std::string> names;
    if (lua_State* L = NewState(mod)) {
        mod.L = L;
        lua_pushcfunction(L, WalkApi);
        lua_pushlightuserdata(L, &names);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) names.clear();
        lua_close(L);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace obl::mods
