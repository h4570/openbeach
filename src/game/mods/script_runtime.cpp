#include "game/mods/script_runtime.h"

#include "formats/mod_pack.h"

#include <nlohmann/json.hpp>

// Lua is compiled as C++ (cmake/lua.cmake), so its headers are included without lua.hpp's extern "C": a Lua error is a C++
// exception that unwinds the frames of the functions below.
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>

namespace obl::mods {

namespace {

// The count hook runs every this many VM instructions; the budget is checked there.
constexpr int kHookStep = 1000;
// Lines one mod may log between Start and Stop; a script printing every frame would bury the log otherwise.
constexpr int kMaxLogLines = 200;
constexpr size_t kMaxStoreKey = 256;
constexpr const char* kStoreFormat = "obl.mod_store";

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

void SetFunctions(lua_State* L, std::initializer_list<std::pair<const char*, lua_CFunction>> functions)
{
    for (const auto& [name, function] : functions) {
        lua_pushcfunction(L, function);
        lua_setfield(L, -2, name);
    }
}

// The `obl` table: the whole script API. RegisteredApi walks it, and the reference page lists the same names.
void RegisterApi(lua_State* L)
{
    lua_newtable(L);
    lua_pushinteger(L, mod_pack::kPresentationApi);
    lua_setfield(L, -2, "api");
    SetFunctions(L, {{"log", ApiLog}, {"command", ApiCommand}});
    lua_newtable(L);
    SetFunctions(L, {{"in_world", ApiInWorld}});
    lua_setfield(L, -2, "game");
    lua_newtable(L);
    SetFunctions(L, {{"get", ApiStoreGet}, {"set", ApiStoreSet}});
    lua_setfield(L, -2, "store");
    lua_newtable(L);
    SetFunctions(L, {{"get", ApiSettingsGet}});
    lua_setfield(L, -2, "settings");
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
};

// Text chunks only: a precompiled chunk could break the interpreter's memory safety.
int LoadAndRun(lua_State* L)
{
    const auto& request = *static_cast<const LoadRequest*>(lua_touserdata(L, 1));
    if (luaL_loadbufferx(L, request.text->data(), request.text->size(), request.chunk->c_str(), "t") != LUA_OK) return lua_error(L);
    lua_call(L, 0, 0);
    return 0;
}

struct DispatchRequest {
    const char* function;
    float dt = 0;
    const InputEvent* event = nullptr;
    const char* setting = nullptr; // on_setting: the id of the changed setting
};

void SetString(lua_State* L, const char* field, const std::string& value)
{
    lua_pushstring(L, value.c_str());
    lua_setfield(L, -2, field);
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
        const InputEvent& e = *request.event;
        lua_newtable(L);
        SetString(L, "kind", e.kind);
        SetString(L, "phase", e.phase);
        if (!e.key.empty()) SetString(L, "key", e.key);
        if (!e.button.empty()) SetString(L, "button", e.button);
        if (e.kind == "mouse") {
            lua_pushinteger(L, e.x);
            lua_setfield(L, -2, "x");
            lua_pushinteger(L, e.y);
            lua_setfield(L, -2, "y");
        }
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

void ScriptRuntime::Start(std::span<const ScriptMod> mods)
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
        mod->storeFile = source.storeFile;
        mod->fields = source.settings;
        ReadSettings(*mod);
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
            LoadRequest request{&text, &chunk};
            if (!in.good() && !in.eof()) {
                SwitchOff(m, chunk.substr(1) + ": cannot be read");
                break;
            }
            if (!Protected(m, LoadAndRun, &request, error)) {
                SwitchOff(m, error);
                break;
            }
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
    DispatchRequest request{"on_frame"};
    request.dt = dt;
    CallAll(false, request);
}

void ScriptRuntime::Input(const InputEvent& event)
{
    DispatchRequest request{"on_input"};
    request.event = &event;
    CallAll(true, request);
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

std::span<const std::string_view> ScriptCommands()
{
    return kCommands;
}

std::vector<std::string> RegisteredApi()
{
    ScriptLimits limits;
    Mod mod;
    mod.limits = &limits;
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
