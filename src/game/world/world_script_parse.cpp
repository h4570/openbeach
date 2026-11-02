#include "game/world/world_script_parse.h"

#include "core/ascii_case.h"
#include "game/map/path_tile_script.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace obl::world_script_parse {
namespace {
using namespace world_script;

[[noreturn]] void Fail(const std::string& message)
{
    throw ParseError(message);
}
int Find(const std::vector<NamedId>& table, const std::string& token, bool noCase)
{
    for (const auto& row : table)
        if (noCase ? ascii::EqualsNoCase(row.name, token) : row.name == token) return row.id;
    return -1;
}
bool OwnsData(int kind)
{
    return kind == 0x36 || kind == 0xe3 || kind == 0xe4 || kind == 0xe8 || kind == 0xbc || kind == 0xc2 || kind == 0xc6 ||
            (kind >= 0xd2 && kind <= 0xd5);
}
bool TextData(int kind)
{
    return kind == 0x36 || (kind >= 0xd2 && kind <= 0xd5);
}

class Parser {
public:
    Parser(WorldScript& script, const char* text, size_t size, const Vocabulary& vocabulary, const Memory& memory, BeerId beer,
            ParseDetails* details)
        : m_script(script),
          m_before(script),
          m_text(text, size),
          m_vocabulary(vocabulary),
          m_memory(memory),
          m_beer(beer),
          m_outDetails(details)
    {}
    ~Parser()
    {
        if (!m_complete) {
            for (auto it = m_links.rbegin(); it != m_links.rend(); ++it) *it->first = it->second;
            m_script = m_before;
            for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it) m_memory.release(*it);
        }
    }
    void Load()
    {
        while (true) {
            Skip();
            if (m_pos == m_text.size()) break;
            if (Read() != "State") Fail("the world script must start with a State");
            ParseState();
        }
        m_script.m_text = nullptr; // cleared together with the text it pointed into
        if (m_outDetails) *m_outDetails = std::move(m_details);
        m_complete = true;
    }

private:
    WorldScript& m_script;
    WorldScript m_before;
    std::string m_text;
    const Vocabulary& m_vocabulary;
    const Memory& m_memory;
    BeerId m_beer;
    ParseDetails* m_outDetails;
    ParseDetails m_details;
    size_t m_pos = 0;
    bool m_complete = false;
    ScriptIfData* m_openIf = nullptr;
    std::vector<void*> m_blocks;
    std::vector<std::pair<ScriptCommand**, ScriptCommand*>> m_links;

    void* Allocate(size_t size)
    {
        void* block = m_memory.allocate(size);
        if (!block) throw std::bad_alloc();
        try {
            m_blocks.push_back(block);
        } catch (...) {
            m_memory.release(block);
            throw;
        }
        std::memset(block, 0, size);
        return block;
    }
    template<class T> T* New() { return static_cast<T*>(Allocate(sizeof(T))); }
    char* String(const std::string& value)
    {
        auto* block = static_cast<char*>(Allocate(value.size() + 1));
        std::memcpy(block, value.c_str(), value.size() + 1);
        return block;
    }
    float* Number(float value)
    {
        auto* block = New<float>();
        *block = value;
        return block;
    }
    int32_t* Index(int32_t value)
    {
        auto* block = New<int32_t>();
        *block = value;
        return block;
    }
    void ReplaceString(char*& out, const std::string& value)
    {
        if (out) {
            auto it = std::find(m_blocks.begin(), m_blocks.end(), out);
            if (it != m_blocks.end()) {
                m_memory.release(out);
                m_blocks.erase(it);
            }
        }
        out = String(value);
    }
    void Skip()
    {
        while (m_pos < m_text.size()) {
            if (path_tile_script::IsSpace(m_text[m_pos])) {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] != '/') break;
            while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
        }
    }
    std::string Read()
    {
        Skip();
        if (m_pos == m_text.size()) Fail("script ends inside a command");
        size_t start = m_pos++;
        if (m_text[start] != ';')
            while (m_pos < m_text.size() && !path_tile_script::IsSpace(m_text[m_pos]) && m_text[m_pos] != ';') ++m_pos;
        return m_text.substr(start, m_pos - start);
    }
    int Integer() { return std::atoi(Read().c_str()); }
    float Float() { return static_cast<float>(std::atof(Read().c_str())); }
    void DiscardTerminator() { Read(); } // fixed-arity commands consume one token, without checking it
    WorldScriptOperand Resolve(const std::string& token)
    {
        int id = Find(m_vocabulary.values, token, true);
        if (id >= 0) return {id, nullptr}; // the data word is unset for named values
        if (!token.empty() && token[0] == '"') return {0x36, String(token.size() >= 2 ? token.substr(1, token.size() - 2) : "")};
        constexpr const char* prefixes[] = {
                "RandDWORD", "Random", "Flag", "Var", "ScenarioObj", "BeerActive", "DefaultBeer", "BuildingCount", "EventActive"};
        constexpr int ids[] = {0xe3, 0xe4, 0xbc, 0xc2, 0xc6, 0xd2, 0xd3, 0xd4, 0xd5};
        for (size_t i = 0; i < std::size(prefixes); ++i) {
            size_t length = std::strlen(prefixes[i]);
            if (token.compare(0, length, prefixes[i]) == 0) {
                std::string suffix = token.substr(length);
                return {ids[i], i < 5 ? static_cast<void*>(Index(std::atoi(suffix.c_str()))) : static_cast<void*>(String(suffix))};
            }
        }
        if (token.empty() || token[0] < '0' || token[0] > '9') Fail("unrecognized name in the world script: " + token);
        return {0xe8, Number(static_cast<float>(std::atof(token.c_str())))};
    }
    void AppendList(ScriptCommand*& list, ScriptCommand* node)
    {
        ScriptCommand** slot = &list;
        while (*slot) slot = &(*slot)->m_next;
        m_links.push_back({slot, *slot});
        *slot = node;
    }
    void Append(int opcode, void* data)
    {
        auto* node = New<ScriptCommand>();
        node->m_opcode = opcode;
        node->m_data = data;
        if (m_openIf) {
            AppendList(m_openIf->m_unused ? m_openIf->m_false : m_openIf->m_true, node);
            return;
        }
        auto& state = m_script.m_states[m_script.m_currentState];
        switch (m_script.m_parsedList) {
        case 0: AppendList(state.m_onEntry, node); break;
        case 1: AppendList(state.m_onExit, node); break;
        case 2: AppendList(state.m_onUpdate, node); break;
        case 3:
        case 4:
            if (state.m_NumHandlers >= kWorldScriptHandlers) Fail("too many message handlers");
            AppendList(m_script.m_parsedList == 3 ? state.m_handlers[state.m_NumHandlers].m_onReceipt
                                                  : state.m_handlers[state.m_NumHandlers].m_onSend,
                    node);
            break;
        default: Fail("command added to a list that does not exist");
        }
    }
    void ParseState()
    {
        if (m_script.m_numStates >= kMaxWorldScriptStates) Fail("too many states");
        std::string name = Read();
        bool found = false;
        for (uint32_t i = 0; i < m_script.m_numStates; ++i)
            if (name == m_script.m_stateNames[i]) {
                found = true;
                break;
            }
        // RegisterState returns early on a duplicate without changing CurrentState;
        // ParseState still increments NumStates. Keep that quirk.
        if (!found) {
            m_script.m_currentState = m_script.m_numStates;
            m_script.m_stateNames[m_script.m_numStates] = name;
        }
        for (std::string token = Read(); token != "End"; token = Read()) {
            if (token == "OnEntry")
                Commands(0);
            else if (token == "OnExit")
                Commands(1);
            else if (token == "OnUpdate")
                Commands(2);
            else if (token == "Message")
                MessageBlock();
            else
                Fail("unrecognized command in a world script state: " + token);
        }
        ++m_script.m_numStates;
    }
    void MessageBlock()
    {
        auto& state = m_script.m_states[m_script.m_currentState];
        if (state.m_NumHandlers >= kWorldScriptHandlers) Fail("too many message handlers");
        std::string name = Read();
        int id = Find(m_vocabulary.messages, name, false);
        if (id < 0) Fail("unrecognized message name in the world script: " + name);
        state.m_handlers[state.m_NumHandlers].m_messageId = id;
        for (std::string token = Read(); token != "End"; token = Read()) {
            if (token == "OnReceipt")
                Commands(3);
            else if (token == "OnSend")
                Commands(4);
            else
                Fail("unrecognized command in a world script message handler: " + token);
        }
        // This increment indexes with NumStates, not CurrentState.
        ++m_script.m_states[m_script.m_numStates].m_NumHandlers;
    }
    void If()
    {
        while (m_pos < m_text.size() && m_text[m_pos] != '(') ++m_pos;
        size_t start = m_pos;
        while (m_pos < m_text.size() && m_text[m_pos] != ')') ++m_pos;
        if (m_pos == m_text.size()) Fail("unterminated If condition");
        std::string condition = m_text.substr(start + 1, m_pos - start - 1);
        ++m_pos;
        size_t at = 0;
        auto word = [&]() {
            while (at < condition.size() && condition[at] == ' ') ++at;
            size_t begin = at;
            while (at < condition.size() && condition[at] != ' ') ++at;
            return condition.substr(begin, at - begin);
        };
        auto* data = New<ScriptIfData>();
        auto left = Resolve(word());
        data->m_leftKey = left.m_kind;
        data->m_leftData = static_cast<int*>(left.m_data);
        std::string op = word();
        if (op.empty())
            data->m_rightKey = 0x110;
        else {
            constexpr const char* ops[] = {"&&", "||", ">", "<", "==", "<=", "=<", ">=", "=>", "!="};
            constexpr int numbers[] = {0, 1, 2, 3, 4, 5, 5, 6, 6, 6}; // != is a bug in the world parser: >=
            auto it = std::find(std::begin(ops), std::end(ops), op);
            if (it == std::end(ops)) Fail("unrecognized If operator in the world script: " + op);
            data->m_op = numbers[it - std::begin(ops)];
            auto right = Resolve(word());
            data->m_rightKey = right.m_kind;
            data->m_rightData = static_cast<int*>(right.m_data);
        }
        Append(4, data);
        auto* parent = m_openIf;
        m_openIf = data;
        Commands(m_script.m_parsedList);
        m_openIf = parent;
    }
    void Message()
    {
        auto* data = New<WorldScriptMessageData>();
        int id = Find(m_vocabulary.messages, Read(), false);
        data->m_messageId = id < 0 ? 0 : id; // unknown name retains the constructor's zero
        data->m_floatData.m_kind = data->m_state.m_kind = data->m_sendFrom.m_kind = 0x110;
        data->m_receiver = Resolve(Read());
        data->m_data = Resolve(Read());
        data->m_flags = Resolve(Read());
        for (std::string token = Read(); token != ";"; token = Read()) {
            if (token == "Time")
                data->m_time = Float();
            else if (token == "MaxTime")
                data->m_maxTime = Float();
        }
        Append(1, data);
    }
    void ChangeState()
    {
        auto* data = New<WorldScriptChangeStateData>();
        data->m_name = String(Read());
        std::string token = Read();
        if (token != ";") {
            if (token != "Time") Fail("ChangeState in the world script expects Time");
            data->m_time = Float();
            DiscardTerminator();
        }
        Append(2, data);
    }
    int Beer(const std::string& name) const { return Find(m_vocabulary.beers, name, true); }
    void Create()
    {
        auto* data = New<WorldScriptCreateData>();
        std::fill(std::begin(data->m_prices), std::end(data->m_prices), -1.0f);
        data->m_number = 1;
        data->m_beer = -1;
        std::string type = Read();
        if (type == "Building")
            data->m_class = 2;
        else if (type == "Guest")
            data->m_class = 3;
        else if (type == "Scenery")
            data->m_class = 1;
        else
            Fail("unrecognized Create type in the world script: " + type);
        data->m_type = String(Read());
        std::string token = Read();
        if (ascii::EqualsNoCase(token, "Random"))
            data->m_x = data->m_y = -1;
        else {
            data->m_x = std::atoi(token.c_str());
            data->m_y = Integer();
        }
        if (data->m_class == 2) data->m_layer = Integer();
        token = Read();
        if (data->m_class == 1) {
            if (data->m_x >= 0) {
                data->m_layer = std::atoi(token.c_str());
                token = Read();
            }
            for (; token != ";"; token = Read()) {
                if (token == "Number")
                    data->m_number = Integer();
                else
                    ReplaceString(data->m_sprite, token);
            }
        } else if (data->m_class == 3) {
            for (; token != ";"; token = Read()) {
                if (token != "Number") Fail("unrecognized Create option in the world script: " + token);
                data->m_number = Integer();
            }
        } else {
            for (; token != ";"; token = Read()) {
                if (token == "Prices") {
                    // Prices consumes the entire remainder; BeerType's price is also written
                    // into the next ordinary price slot. Age/Broken are no longer keywords.
                    size_t slot = 0;
                    for (token = Read(); token != ";"; token = Read()) {
                        if (token == "BeerType") {
                            int beer = Beer(Read());
                            if (beer >= 0) data->m_beer = beer;
                            token = Read();
                            data->m_beerPrice = static_cast<float>(std::atof(token.c_str()));
                        }
                        if (slot >= 5) Fail("too many Create Prices");
                        data->m_prices[slot++] = static_cast<float>(std::atof(token.c_str()));
                    }
                    continue;
                } else if (token == "BeerType") {
                    int beer = Beer(Read());
                    if (beer >= 0) data->m_beer = beer;
                    data->m_beerPrice = Float();
                } else if (token == "Age")
                    data->m_age = Integer();
                else if (token == "Broken")
                    data->m_broken = 1;
                else
                    Fail("unrecognized Create option in the world script: " + token);
            }
        }
        Append(12, data);
    }
    void Tile(int opcode)
    {
        auto* data = New<WorldScriptTileData>();
        data->m_x = Integer();
        data->m_y = Integer();
        data->m_layer = Integer();
        std::string direction = Read();
        constexpr const char* names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        int found = -1;
        for (int i = 0; i < 8; ++i)
            if (ascii::EqualsNoCase(direction, names[i])) found = i;
        if (found < 0) Fail("unrecognized direction: " + direction);
        data->m_direction = found;
        DiscardTerminator();
        Append(opcode, data);
    }
    void Area()
    {
        auto* data = New<WorldScriptAreaData>();
        std::string token = Read();
        if (ascii::EqualsNoCase(token, "Beach"))
            data->m_kind = 1;
        else if (ascii::EqualsNoCase(token, "Sea"))
            data->m_kind = 2;
        else
            Fail("unrecognized area type in the world script");
        int count = Integer();
        if (count < 0 || static_cast<size_t>(count) > m_text.size() / 2) Fail("invalid Area point count");
        data->m_count = count;
        data->m_points = static_cast<WorldScriptPoint*>(Allocate(count * sizeof(WorldScriptPoint)));
        for (int i = 0; i < count; ++i) {
            data->m_points[i].m_x = Integer();
            data->m_points[i].m_y = Integer();
        }
        for (auto& point : data->m_rectangle) {
            point.m_x = Integer();
            point.m_y = Integer();
        }
        DiscardTerminator();
        Append(16, data);
    }
    void BoatPoint()
    {
        auto* data = New<WorldScriptBoatPointData>();
        data->m_x = Float();
        data->m_y = Float();
        for (std::string token = Read(); token != ";"; token = Read()) {
            if (ascii::EqualsNoCase(token, "Unload"))
                data->m_flags = 1;
            else if (ascii::EqualsNoCase(token, "Destroy"))
                data->m_flags = 2;
        }
        Append(17, data);
    }
    void BoatEndpoint(int opcode)
    {
        auto* data = New<WorldScriptBoatEndpointData>();
        data->m_x = Float();
        data->m_y = Float();
        data->m_number = Integer();
        data->m_dx = Float();
        data->m_dy = Float();
        DiscardTerminator();
        Append(opcode, data);
    }
    void BoatEvent()
    {
        auto* data = New<WorldScriptBoatEventData>();
        uint32_t mask = 0;
        data->m_timeFlags = 1;
        for (std::string token = Read(); token != ";"; token = Read()) {
            if (token == "Now")
                data->m_timeFlags = 3;
            else if (token == "Day" || token == "Day+") {
                data->m_timeFlags = token == "Day" ? 0 : 2;
                data->m_day = Integer();
                mask |= 1;
            } else if (token == "Time") {
                data->m_hour = Float();
                mask |= 2;
            } else if (token == "Guests") {
                mask |= 4;
                data->m_guestInfo = 2;
                for (size_t i = 0; (token = Read()) != ";"; ++i) {
                    if (i >= 20 || token.size() >= 64) Fail("too many or oversized BoatEvent guest types");
                    std::memcpy(data->m_keywords[i], token.c_str(), token.size() + 1);
                    token = Read();
                    if (token == "Random") {
                        data->m_random[i] = 1;
                        token = Read();
                    }
                    data->m_counts[i] = std::atoi(token.c_str());
                }
                break;
            } else {
                mask |= 4;
                if (token == "Random" || token == "Performance") {
                    data->m_guestInfo = token == "Random" ? 0 : 3;
                    token = Read();
                } else
                    data->m_guestInfo = 1;
                data->m_number = std::atoi(token.c_str());
            }
        }
        if (m_outDetails) m_details.optionalFields[data] = mask;
        Append(20, data);
    }
    void Disaster()
    {
        auto* data = New<WorldScriptDisasterData>();
        uint32_t mask = 0;
        data->m_strength = 0.5f;
        constexpr const char* types[] = {"Sharks", "WaterContamination", "BeerShortage", "Lightning", "Strike"};
        std::string type = Read();
        auto it = std::find(std::begin(types), std::end(types), type);
        if (it == std::end(types)) Fail("unrecognized disaster type: " + type);
        data->m_kind = static_cast<int>(it - std::begin(types));
        for (std::string token = Read(); token != ";"; token = Read()) {
            if (token == "Day") {
                data->m_day = Integer();
                mask |= 1;
            } else if (token == "Time") {
                data->m_hour = Float();
                mask |= 2;
            } else if (token == "Duration") {
                data->m_duration = Float();
                mask |= 4;
            } else if (token == "Scale") {
                data->m_scale = Integer();
                mask |= 8;
            } else if (token == "Rect") {
                for (int& point : data->m_rectangle) point = Integer();
                mask |= 16;
            } else if (token == "DisasterValue") {
                data->m_value = Float();
                mask |= 32;
            } else if (token == "BeerType" || token == "ReplacementBeer") {
                if (!m_beer) Fail("Beer resolver unavailable");
                int id = m_beer(Read().c_str());
                if (token == "BeerType") {
                    data->m_beer = id;
                    mask |= 64;
                } else {
                    data->m_replacementBeer = id;
                    mask |= 128;
                }
            } else if (token == "ReplacementStrength")
                data->m_strength = Float();
            else if (token == "Target")
                ReplaceString(data->m_target, Read());
            else
                Fail("unrecognized disaster option: " + token);
        }
        if (m_outDetails) m_details.optionalFields[data] = mask;
        Append(22, data);
    }
    void Weather()
    {
        auto* data = New<WorldScriptWeatherData>();
        uint32_t mask = 0;
        constexpr const char* types[] = {"HeatWave", "Normal", "Overcast", "LightRain", "HeavyRain", "ThunderStorm"};
        std::string type = Read();
        auto it = std::find(std::begin(types), std::end(types), type);
        if (it == std::end(types)) Fail("unrecognized weather type: " + type);
        data->m_kind = static_cast<int>(it - std::begin(types));
        for (std::string token = Read(); token != ";"; token = Read()) {
            if (token == "Day") {
                data->m_day = Integer();
                mask |= 1;
            } else if (token == "Time") {
                data->m_hour = Float();
                mask |= 2;
            } else {
                const int slot = Find(m_vocabulary.modifiers, token, false);
                if (slot < 0) Fail("unrecognized weather option: " + token);
                data->m_modifiers[slot] = Float();
            }
        }
        if (m_outDetails) m_details.optionalFields[data] = mask;
        Append(23, data);
    }
    void Commands(int list)
    {
        m_script.m_parsedList = list;
        for (std::string token = Read(); token != "End"; token = Read()) {
            if (token == "If")
                If();
            else if (token == "Else") {
                if (!m_openIf) Fail("Else without If");
                m_openIf->m_unused = 1;
            } else if (token == "Function") {
                std::string name = Read();
                int id = Find(m_vocabulary.functions, name, true);
                if (id < 0) Fail("unrecognized function in the world script: " + name);
                auto* data = New<ScriptFunctionData>();
                data->m_function = id;
                DiscardTerminator();
                Append(0, data);
            } else if (token == "Message")
                Message();
            else if (token == "ChangeState")
                ChangeState();
            else if (token == "Create")
                Create();
            else if (token == "Path" || token == "Grass")
                Tile(token == "Path" ? 13 : 14);
            else if (token == "LoadScript" || token == "ShowMenu") {
                auto* data = New<WorldScriptTwoTextsData>();
                data->m_first = String(Read());
                std::string second = Read();
                if (token == "LoadScript" || second != ";") data->m_second = String(second);
                if (token == "LoadScript") DiscardTerminator();
                // ShowMenu consumes only its name and one further token: the trailing ;
                // after a text becomes an unknown command that Commands silently ignores.
                Append(token == "LoadScript" ? 15 : 25, data);
            } else if (ascii::EqualsNoCase(token, "Area"))
                Area();
            else if (token == "BoatPoint")
                BoatPoint();
            else if (token == "BoatEntry" || token == "BoatExit")
                BoatEndpoint(token == "BoatEntry" ? 18 : 19);
            else if (token == "BoatEvent")
                BoatEvent();
            else if (token == "CircuitPoint") {
                auto* data = New<WorldScriptCircuitData>();
                std::string kind = Read();
                data->m_kind = ascii::EqualsNoCase(kind, "PowerBoat") ? 1 : 0;
                data->m_x = Integer();
                data->m_y = Integer();
                DiscardTerminator();
                Append(21, data);
            } else if (token == "Disaster")
                Disaster();
            else if (token == "Weather")
                Weather();
            else if (token == "PlaySound") {
                auto* data = New<WorldScriptTextData>();
                data->m_text = String(Read());
                DiscardTerminator();
                Append(11, data);
            } else if (token == "MoveCamera") {
                auto* data = New<WorldScriptCameraData>();
                data->m_x = Float();
                data->m_y = Float();
                data->m_speed = Float();
                DiscardTerminator();
                Append(24, data);
            } else {
                constexpr const char* names[] = {"Add", "Minus", "Divide", "Multiply", "Set"};
                auto it = std::find(std::begin(names), std::end(names), token);
                if (it != std::end(names)) {
                    auto* data = New<ScriptAssignData>();
                    auto left = Resolve(Read());
                    auto right = Resolve(Read());
                    data->m_leftKey = left.m_kind;
                    data->m_leftData = left.m_data;
                    data->m_rightKey = right.m_kind;
                    data->m_rightData = right.m_data;
                    DiscardTerminator();
                    Append(6 + static_cast<int>(it - std::begin(names)), data);
                }
                // Unknown command tokens are skipped by the dispatcher.
            }
        }
    }
};

void FreeValue(int kind, void* data, const Memory& memory)
{
    if (OwnsData(kind) && data) memory.release(data);
}
void FreeList(ScriptCommand*& list, const Memory& memory)
{
    while (list) {
        auto* node = list;
        list = node->m_next;
        void* payload = node->m_data;
        if (payload) switch (node->m_opcode) {
            case 1: {
                auto& data = *static_cast<WorldScriptMessageData*>(payload);
                const WorldScriptOperand values[] = {
                        data.m_receiver, data.m_data, data.m_flags, data.m_floatData, data.m_state, data.m_sendFrom};
                for (auto value : values) FreeValue(value.m_kind, value.m_data, memory);
                break;
            }
            case 2: memory.release(static_cast<WorldScriptChangeStateData*>(payload)->m_name); break;
            case 4: {
                auto& data = *static_cast<ScriptIfData*>(payload);
                FreeList(data.m_true, memory);
                FreeList(data.m_false, memory);
                FreeValue(data.m_leftKey, data.m_leftData, memory);
                FreeValue(data.m_rightKey, data.m_rightData, memory);
                break;
            }
            case 6:
            case 7:
            case 8:
            case 9:
            case 10: {
                auto& data = *static_cast<ScriptAssignData*>(payload);
                FreeValue(data.m_leftKey, data.m_leftData, memory);
                FreeValue(data.m_rightKey, data.m_rightData, memory);
                break;
            }
            case 11: memory.release(static_cast<WorldScriptTextData*>(payload)->m_text); break;
            case 12: {
                auto& data = *static_cast<WorldScriptCreateData*>(payload);
                if (data.m_type) memory.release(data.m_type);
                if (data.m_sprite) memory.release(data.m_sprite);
                break;
            }
            case 15:
            case 25: {
                auto& data = *static_cast<WorldScriptTwoTextsData*>(payload);
                if (data.m_first) memory.release(data.m_first);
                if (data.m_second) memory.release(data.m_second);
                break;
            }
            case 16:
                if (auto* points = static_cast<WorldScriptAreaData*>(payload)->m_points) memory.release(points);
                break;
            case 22:
                if (auto* target = static_cast<WorldScriptDisasterData*>(payload)->m_target) memory.release(target);
                break;
            }
        if (payload) memory.release(payload);
        memory.release(node);
    }
}

// The command destructor and its payload helpers: the order of the delete calls and the deletes of null owned
// pointers are the contract the dry-run check compares, so they differ from FreeList (which also serves rollback).
void ReleaseValue(int kind, void* data, const Memory& memory)
{
    if (OwnsData(kind)) memory.release(data);
}
void ReleaseNodes(ScriptCommand*& head, const Memory& memory);
void ReleasePayload(const ScriptCommand& node, const Memory& memory)
{
    void* payload = node.m_data;
    if (!payload) return;
    auto text = [&](void* pointer) {
        if (pointer) memory.release(pointer);
    };
    switch (node.m_opcode) {
    case 0:
    case 20:
    case 23: break;
    case 1: {
        auto& data = *static_cast<WorldScriptMessageData*>(payload);
        for (const WorldScriptOperand* operand :
                {&data.m_receiver, &data.m_data, &data.m_flags, &data.m_floatData, &data.m_state, &data.m_sendFrom})
            ReleaseValue(operand->m_kind, operand->m_data, memory);
        break;
    }
    case 2: text(static_cast<WorldScriptChangeStateData*>(payload)->m_name); break;
    case 3:
    case 15:
    case 25: {
        auto& data = *static_cast<WorldScriptTwoTextsData*>(payload);
        text(data.m_first);
        text(data.m_second);
        break;
    }
    case 4: {
        auto& data = *static_cast<ScriptIfData*>(payload);
        ReleaseNodes(data.m_false, memory);
        ReleaseNodes(data.m_true, memory);
        ReleaseValue(data.m_leftKey, data.m_leftData, memory);
        ReleaseValue(data.m_rightKey, data.m_rightData, memory);
        break;
    }
    case 6:
    case 7:
    case 8:
    case 9:
    case 10: {
        auto& data = *static_cast<ScriptAssignData*>(payload);
        ReleaseValue(data.m_leftKey, data.m_leftData, memory);
        ReleaseValue(data.m_rightKey, data.m_rightData, memory);
        break;
    }
    case 11: text(static_cast<WorldScriptTextData*>(payload)->m_text); break;
    case 12: {
        auto& data = *static_cast<WorldScriptCreateData*>(payload);
        text(data.m_type);
        text(data.m_sprite);
        break;
    }
    case 16: text(static_cast<WorldScriptAreaData*>(payload)->m_points); break;
    case 22: text(static_cast<WorldScriptDisasterData*>(payload)->m_target); break;
    case 13:
    case 14:
    case 17:
    case 18:
    case 19:
    case 21:
    case 24: break;
    default: return; // opcode 5 and everything above 25: the node is freed, the payload is not
    }
    memory.release(payload);
}
void ReleaseNodes(ScriptCommand*& head, const Memory& memory)
{
    while (head) {
        auto* node = head;
        head = node->m_next;
        ReleasePayload(*node, memory);
        memory.release(node);
    }
}

bool SameText(const char* a, const char* b)
{
    return (!a || !b) ? a == b : std::strcmp(a, b) == 0;
}
bool SameBytes(const void* a, const void* b, size_t size)
{
    return std::memcmp(a, b, size) == 0;
}
bool SameValue(int aKind, const void* a, int bKind, const void* b)
{
    if (aKind != bKind) return false;
    if (!OwnsData(aKind)) return true;
    if (!a || !b) return a == b;
    return TextData(aKind) ? SameText(static_cast<const char*>(a), static_cast<const char*>(b)) : SameBytes(a, b, 4);
}
std::string DiffList(const ScriptCommand* a, const ScriptCommand* b, const std::string& path, ListStats* stats, const ParseDetails* details)
{
    for (int i = 0; a || b; ++i) {
        std::string here = path + "/" + std::to_string(i);
        if (!a || !b) return here + ": list length";
        if (a->m_opcode != b->m_opcode) return here + ": opcode";
        int op = a->m_opcode;
        if (stats) {
            ++stats->nodes;
            if (op >= 0 && op < 26) ++stats->opcodes[op];
        }
        if (!a->m_data || !b->m_data) return here + ": null payload";
        bool equal = true;
        auto optionalMask = [&](uint32_t fallback) {
            if (details) {
                auto found = details->optionalFields.find(b->m_data);
                if (found != details->optionalFields.end()) return found->second;
            }
            return fallback;
        };
        switch (op) {
        case 0: equal = SameBytes(a->m_data, b->m_data, 4); break;
        case 1: {
            auto& x = *static_cast<const WorldScriptMessageData*>(a->m_data);
            auto& y = *static_cast<const WorldScriptMessageData*>(b->m_data);
            equal = x.m_messageId == y.m_messageId && SameBytes(&x.m_time, &y.m_time, 8);
            const WorldScriptOperand xs[] = {x.m_receiver, x.m_data, x.m_flags, x.m_floatData, x.m_state, x.m_sendFrom};
            const WorldScriptOperand ys[] = {y.m_receiver, y.m_data, y.m_flags, y.m_floatData, y.m_state, y.m_sendFrom};
            for (int j = 0; j < 6; ++j) equal &= SameValue(xs[j].m_kind, xs[j].m_data, ys[j].m_kind, ys[j].m_data);
            break;
        }
        case 2: {
            auto& x = *static_cast<const WorldScriptChangeStateData*>(a->m_data);
            auto& y = *static_cast<const WorldScriptChangeStateData*>(b->m_data);
            equal = SameText(x.m_name, y.m_name) && SameBytes(&x.m_time, &y.m_time, 4);
            break;
        }
        case 4: {
            auto& x = *static_cast<const ScriptIfData*>(a->m_data);
            auto& y = *static_cast<const ScriptIfData*>(b->m_data);
            equal = SameValue(x.m_leftKey, x.m_leftData, y.m_leftKey, y.m_leftData) &&
                    SameValue(x.m_rightKey, x.m_rightData, y.m_rightKey, y.m_rightData) && (x.m_rightKey == 0x110 || x.m_op == y.m_op) &&
                    x.m_unused == y.m_unused;
            if (auto diff = DiffList(x.m_true, y.m_true, here + "/then", stats, details); !diff.empty()) return diff;
            if (auto diff = DiffList(x.m_false, y.m_false, here + "/else", stats, details); !diff.empty()) return diff;
            break;
        }
        case 6:
        case 7:
        case 8:
        case 9:
        case 10: {
            auto& x = *static_cast<const ScriptAssignData*>(a->m_data);
            auto& y = *static_cast<const ScriptAssignData*>(b->m_data);
            equal = SameValue(x.m_leftKey, x.m_leftData, y.m_leftKey, y.m_leftData) &&
                    SameValue(x.m_rightKey, x.m_rightData, y.m_rightKey, y.m_rightData);
            break;
        }
        case 11:
            equal = SameText(
                    static_cast<const WorldScriptTextData*>(a->m_data)->m_text, static_cast<const WorldScriptTextData*>(b->m_data)->m_text);
            break;
        case 12: {
            auto& x = *static_cast<const WorldScriptCreateData*>(a->m_data);
            auto& y = *static_cast<const WorldScriptCreateData*>(b->m_data);
            equal = x.m_class == y.m_class && SameText(x.m_type, y.m_type) && SameText(x.m_sprite, y.m_sprite) &&
                    SameBytes(&x.m_x, &y.m_x, 8) && (x.m_class == 3 || x.m_layer == y.m_layer) && x.m_number == y.m_number &&
                    SameBytes(x.m_prices, y.m_prices, 20) && x.m_beer == y.m_beer &&
                    (x.m_beer < 0 || SameBytes(&x.m_beerPrice, &y.m_beerPrice, 4)) && x.m_age == y.m_age && x.m_broken == y.m_broken;
            break;
        }
        case 13:
        case 14: equal = SameBytes(a->m_data, b->m_data, sizeof(WorldScriptTileData)); break;
        case 15:
        case 25: {
            auto& x = *static_cast<const WorldScriptTwoTextsData*>(a->m_data);
            auto& y = *static_cast<const WorldScriptTwoTextsData*>(b->m_data);
            equal = SameText(x.m_first, y.m_first) && SameText(x.m_second, y.m_second);
            break;
        }
        case 16: {
            auto& x = *static_cast<const WorldScriptAreaData*>(a->m_data);
            auto& y = *static_cast<const WorldScriptAreaData*>(b->m_data);
            equal = x.m_kind == y.m_kind && x.m_count == y.m_count && SameBytes(x.m_rectangle, y.m_rectangle, sizeof(x.m_rectangle));
            if (equal && x.m_count > 0) equal = SameBytes(x.m_points, y.m_points, x.m_count * sizeof(WorldScriptPoint));
            break;
        }
        case 17: equal = SameBytes(a->m_data, b->m_data, sizeof(WorldScriptBoatPointData)); break;
        case 18:
        case 19: equal = SameBytes(a->m_data, b->m_data, sizeof(WorldScriptBoatEndpointData)); break;
        case 20: {
            auto& x = *static_cast<const WorldScriptBoatEventData*>(a->m_data);
            auto& y = *static_cast<const WorldScriptBoatEventData*>(b->m_data);
            uint32_t mask = optionalMask(4 | (y.m_timeFlags != 3 ? 2 : 0) | (y.m_timeFlags == 0 || y.m_timeFlags == 2 ? 1 : 0));
            equal = x.m_timeFlags == y.m_timeFlags && x.m_number == y.m_number;
            if (mask & 1) equal &= x.m_day == y.m_day;
            if (mask & 2) equal &= SameBytes(&x.m_hour, &y.m_hour, 4);
            if (mask & 4) equal &= x.m_guestInfo == y.m_guestInfo;
            for (int j = 0; j < 20; ++j) {
                equal &= SameText(x.m_keywords[j], y.m_keywords[j]) && SameBytes(&x.m_counts[j], &y.m_counts[j], 4) &&
                        x.m_random[j] == y.m_random[j];
            }
            break;
        }
        case 21: equal = SameBytes(a->m_data, b->m_data, sizeof(WorldScriptCircuitData)); break;
        case 22: {
            auto& x = *static_cast<const WorldScriptDisasterData*>(a->m_data);
            auto& y = *static_cast<const WorldScriptDisasterData*>(b->m_data);
            uint32_t mask = optionalMask(3 | (y.m_kind != 4 ? 4 : 0) | (y.m_kind == 0 || y.m_kind == 3 ? 24 : 0) |
                    (y.m_kind == 1 ? 32 : 0) | (y.m_kind == 2 ? 192 : 0));
            equal = x.m_kind == y.m_kind && SameBytes(&x.m_strength, &y.m_strength, 4) && SameText(x.m_target, y.m_target);
            if (mask & 1) equal &= x.m_day == y.m_day;
            if (mask & 2) equal &= SameBytes(&x.m_hour, &y.m_hour, 4);
            if (mask & 4) equal &= SameBytes(&x.m_duration, &y.m_duration, 4);
            if (mask & 8) equal &= x.m_scale == y.m_scale;
            if (mask & 16) equal &= SameBytes(x.m_rectangle, y.m_rectangle, 16);
            if (mask & 32) equal &= SameBytes(&x.m_value, &y.m_value, 4);
            if (mask & 64) equal &= x.m_beer == y.m_beer;
            if (mask & 128) equal &= x.m_replacementBeer == y.m_replacementBeer;
            break;
        }
        case 23: {
            auto& x = *static_cast<const WorldScriptWeatherData*>(a->m_data);
            auto& y = *static_cast<const WorldScriptWeatherData*>(b->m_data);
            uint32_t mask = optionalMask(3);
            equal = x.m_kind == y.m_kind && SameBytes(x.m_modifiers, y.m_modifiers, sizeof(x.m_modifiers));
            if (mask & 1) equal &= x.m_day == y.m_day;
            if (mask & 2) equal &= SameBytes(&x.m_hour, &y.m_hour, 4);
            break;
        }
        case 24: equal = SameBytes(a->m_data, b->m_data, sizeof(WorldScriptCameraData)); break;
        default: return here + ": unsupported opcode";
        }
        if (!equal) return here + ": payload of opcode " + std::to_string(op);
        a = a->m_next;
        b = b->m_next;
    }
    return {};
}
std::string Compare(const WorldScript& a, const WorldScript& b, ListStats* stats, const ParseDetails* details)
{
    if (a.m_numStates != b.m_numStates) return "state count";
    // Load leaves the final parsed state and list active. Runtime may change
    // CurrentState, so only compare this bookkeeping on scratch parses, in the driver.
    for (uint32_t i = 0; i < a.m_numStates; ++i) {
        std::string path = "state " + std::to_string(i);
        if (a.m_stateNames[i] != b.m_stateNames[i]) return path + ": name";
        auto& x = a.m_states[i];
        auto& y = b.m_states[i];
        if (x.m_NumHandlers != y.m_NumHandlers || x.m_stateId != y.m_stateId) return path + ": handler count or state id";
        const ScriptCommand* xs[] = {x.m_onEntry, x.m_onExit, x.m_onUpdate};
        const ScriptCommand* ys[] = {y.m_onEntry, y.m_onExit, y.m_onUpdate};
        for (int j = 0; j < 3; ++j)
            if (auto diff = DiffList(xs[j], ys[j], path + "/list " + std::to_string(j), stats, details); !diff.empty()) return diff;
        for (uint32_t j = 0; j < x.m_NumHandlers; ++j) {
            auto& h = x.m_handlers[j];
            auto& k = y.m_handlers[j];
            if (h.m_messageId != k.m_messageId) return path + ": message id";
            if (auto diff = DiffList(h.m_onReceipt, k.m_onReceipt, path + "/receipt " + std::to_string(j), stats, details); !diff.empty())
                return diff;
            if (auto diff = DiffList(h.m_onSend, k.m_onSend, path + "/send " + std::to_string(j), stats, details); !diff.empty())
                return diff;
        }
    }
    return {};
}
} // namespace

void Parse(world_script::WorldScript& script, const char* text, size_t size, const Vocabulary& vocabulary, const Memory& memory,
        BeerId beerId, ParseDetails* details)
{
    if (details) *details = {};
    Parser parser(script, text, size, vocabulary, memory, beerId, details);
    parser.Load();
}
void Destroy(world_script::WorldScript& script, const Memory& memory)
{
    for (int i = 0; i < world_script::kMaxWorldScriptStates; ++i) {
        auto& state = script.m_states[i];
        FreeList(state.m_onEntry, memory);
        FreeList(state.m_onExit, memory);
        FreeList(state.m_onUpdate, memory);
        for (auto& handler : state.m_handlers) {
            FreeList(handler.m_onReceipt, memory);
            FreeList(handler.m_onSend, memory);
        }
        state.m_NumHandlers = 0;
        script.m_stateNames[i].clear();
    }
    script.m_numStates = script.m_currentState = 0;
    script.m_text = nullptr;
    script.m_openCommand = nullptr;
}
void Construct(world_script::WorldScript& script)
{
    for (auto& state : script.m_states) {
        state.m_stateId = 0x0fffffff;
        for (auto& handler : state.m_handlers) {
            handler.m_messageId = 0x0fffffff;
            handler.m_onReceipt = handler.m_onSend = nullptr;
        }
        state.m_NumHandlers = 0;
        state.m_onEntry = state.m_onExit = state.m_onUpdate = nullptr;
    }
    script.m_openCommand = nullptr;
    std::memset(script.m_reserved, 0, sizeof(script.m_reserved));
    script.m_numStates = 0;
    for (auto& name : script.m_stateNames) name.clear();
    script.m_currentState = 0;
}
void Destruct(world_script::WorldScript& script, const Memory& memory)
{
    for (int i = world_script::kMaxWorldScriptStates - 1; i >= 0; --i) {
        auto& state = script.m_states[i];
        ReleaseNodes(state.m_onEntry, memory);
        ReleaseNodes(state.m_onExit, memory);
        ReleaseNodes(state.m_onUpdate, memory);
        for (int j = world_script::kWorldScriptHandlers - 1; j >= 0; --j) {
            ReleaseNodes(state.m_handlers[j].m_onReceipt, memory);
            ReleaseNodes(state.m_handlers[j].m_onSend, memory);
        }
    }
}
std::string Diff(const world_script::WorldScript& a, const world_script::WorldScript& b, const ParseDetails* bDetails)
{
    return Compare(a, b, nullptr, bDetails);
}
ListStats Count(const world_script::WorldScript& script)
{
    ListStats stats;
    auto visit = [&](auto&& recurse, const ScriptCommand* list) -> void {
        for (auto* node = list; node; node = node->m_next) {
            ++stats.nodes;
            if (node->m_opcode >= 0 && node->m_opcode < 26) ++stats.opcodes[node->m_opcode];
            if (node->m_opcode == 4 && node->m_data) {
                auto& condition = *static_cast<const ScriptIfData*>(node->m_data);
                recurse(recurse, condition.m_true);
                recurse(recurse, condition.m_false);
            }
        }
    };
    for (uint32_t i = 0; i < script.m_numStates; ++i) {
        auto& state = script.m_states[i];
        visit(visit, state.m_onEntry);
        visit(visit, state.m_onExit);
        visit(visit, state.m_onUpdate);
        for (uint32_t j = 0; j < state.m_NumHandlers; ++j) {
            visit(visit, state.m_handlers[j].m_onReceipt);
            visit(visit, state.m_handlers[j].m_onSend);
        }
    }
    return stats;
}
} // namespace obl::world_script_parse
