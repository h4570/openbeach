#include "game/game/game_parameters.h"

#include "core/call_counter.h"
#include "core/log.h"
#include "core/pack_path.h"
#include "game/game/diagnostics.h"
#include "game/game/game_context.h"
#include "game/game/game_api.h"
#include "game/game/game_parameters_construct.h"

#include <cstring>
#include <fstream>
#include <string>

namespace obl::game {
namespace {
using ParameterList = game::GameParameters;
using ParameterNode = game::GameParameterNode;

// Calls answered by the parameter list (`call-report GameParameters.calls`).
test::CallCounter g_calls("GameParameters.calls");

void FatalErrorWith(const char* format, const char* first, const char* second)
{
    FatalError(format, first, second);
}

// The first node of the key, case-sensitive; an empty list answers "not found".
bool Find(const ParameterList& list, ParameterNode** out, const char* key)
{
    *out = nullptr;
    for (ParameterNode* node = list.m_first; node; node = node->m_next) {
        if (!std::strcmp(node->m_name, key)) {
            *out = node;
            return true;
        }
    }
    return false;
}

// The game's fatal error and operator new as the common construct (game_parameters_construct.h) takes them.
game_parameters_construct::Env ConstructEnv()
{
    return {&FatalErrorWith, [](size_t bytes) -> void* { return ::operator new(bytes); }};
}

// The converted parameters in the converted pack, relative to the game directory; docs/build/game-data.md "Converted pack".
constexpr const char* kGameParametersJson = "rules/game_parameters.json";
} // namespace

namespace game_parameters {
// The game reads only the converted rules/game_parameters.json (`path`, the old script path, is unused), which yields the node list
// of game_parameters_construct::FromJson. A missing or refused file stops the game with the reason.
void* Construct(void* block, const char* /*path*/)
{
    g_calls.Count();
    auto* list = static_cast<ParameterList*>(block);
    SetCurrent(list);
    list->m_first = nullptr;
    std::string error;
    std::ifstream file(pack_path::Resolve(pack_path::Selected(), kGameParametersJson), std::ios::binary);
    const auto document = file ? nlohmann::ordered_json::parse(file, nullptr, false) : nlohmann::ordered_json();
    if (!file)
        error = "cannot open";
    else if (document.is_discarded())
        error = "invalid JSON";
    if (error.empty() && game_parameters_construct::FromJson(*list, document, ConstructEnv(), error)) return list;
    OBL_LOG("game parameters: %s: %s", kGameParametersJson, error.c_str());
    FatalError("Cannot load %s (%s).\r\nImport the game data again", kGameParametersJson, error.c_str());
    return list;
}
// Frees every node and its string; the head pointer of the object is left dangling.
void Destruct(void* block)
{
    g_calls.Count();
    ParameterNode* node = static_cast<ParameterList*>(block)->m_first;
    while (node) {
        ParameterNode* next = node->m_next;
        if (node->m_string) ::operator delete(node->m_string);
        ::operator delete(node);
        node = next;
    }
    SetCurrent(nullptr);
}
bool GetBool(const void* list, uint8_t* out, const char* key)
{
    g_calls.Count();
    ParameterNode* node;
    if (!Find(*static_cast<const ParameterList*>(list), &node, key)) return false;
    *out = node->m_boolValue;
    return true;
}
bool GetInt(const void* list, int32_t* out, const char* key)
{
    g_calls.Count();
    ParameterNode* node;
    if (!Find(*static_cast<const ParameterList*>(list), &node, key)) return false;
    *out = node->m_intValue;
    return true;
}
// The stored string pointer itself, not a copy.
bool GetString(const void* list, const char** out, const char* key)
{
    g_calls.Count();
    ParameterNode* node;
    if (!Find(*static_cast<const ParameterList*>(list), &node, key)) return false;
    *out = node->m_string;
    return true;
}
const void* Current()
{
    return GetContext().gameParameters;
}
void SetCurrent(game::GameParameters* list)
{
    GetContext().gameParameters = list;
}
} // namespace game_parameters
} // namespace obl::game
