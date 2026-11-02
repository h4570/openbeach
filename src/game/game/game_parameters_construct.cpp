#include "game/game/game_parameters_construct.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

namespace obl::game_parameters_construct {

void Add(game::GameParameters& list, const char* name, uint8_t boolean, int integer, uint32_t floatBits, const char* string, const Env& env)
{
    auto* node = static_cast<game::GameParameterNode*>(env.allocate(sizeof(game::GameParameterNode)));
    if (!list.m_first) {
        list.m_first = node;
    } else {
        game::GameParameterNode* tail = list.m_first;
        while (tail->m_next) tail = tail->m_next;
        tail->m_next = node;
    }
    node->m_next = nullptr;
    std::memcpy(node->m_name, name, std::strlen(name) + 1);
    node->m_boolValue = boolean;
    node->m_intValue = integer;
    node->m_floatBits = floatBits;
    if (!string) {
        node->m_string = nullptr;
        return;
    }
    node->m_string = static_cast<char*>(env.allocate(0x100));
    std::memcpy(node->m_string, string, std::strlen(string) + 1);
}

bool FromJson(game::GameParameters& list, const nlohmann::ordered_json& document, const Env& env, std::string& error)
{
    list.m_first = nullptr;
    if (!document.is_object() || document.value("format", "") != "obl.game_parameters" || document.value("version", 0) != 1 ||
            !document.contains("parameters") || !document["parameters"].is_object()) {
        error = "not an obl.game_parameters version 1 document";
        return false;
    }
    // Checked whole before the first node, so a refused document leaves the list empty rather than half built.
    struct Node {
        std::string key;
        uint8_t boolean = 0;
        int integer = 0;
        uint32_t floatBits = 0;
        const std::string* string = nullptr;
    };
    std::vector<Node> nodes;
    for (const auto& [name, value] : document["parameters"].items()) {
        Node node;
        if (value.is_boolean()) {
            node.key = "b";
            node.boolean = value.get<bool>() ? 1 : 0;
        } else if (value.is_number_integer()) {
            node.key = "i";
            if (value.is_number_unsigned() ? value.get<uint64_t>() > 0x7fffffffu
                                           : value.get<int64_t>() < INT32_MIN || value.get<int64_t>() > INT32_MAX) {
                error = "parameter '" + name + "' does not fit an integer";
                return false;
            }
            node.integer = value.get<int>();
        } else if (value.is_number_float()) {
            // The converter writes the shortest decimal of the engine's float, which reads back to the same bits.
            node.key = "f";
            node.floatBits = std::bit_cast<uint32_t>(static_cast<float>(value.get<double>()));
        } else if (value.is_string()) {
            node.key = "s";
            node.string = value.get_ptr<const std::string*>();
            if (node.string->size() > 0xff) {
                error = "string of parameter '" + name + "' does not fit the engine's value slot";
                return false;
            }
        } else {
            error = "parameter '" + name + "' is not a boolean, integer, float or string";
            return false;
        }
        for (char c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
                error = "parameter name '" + name + "' is not lowercase snake_case";
                return false;
            }
            node.key.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        }
        if (name.empty() || node.key.size() > 0xff) {
            error = "parameter name '" + name + "' does not fit the engine's name slot";
            return false;
        }
        nodes.push_back(std::move(node));
    }
    for (const Node& node : nodes)
        Add(list, node.key.c_str(), node.boolean, node.integer, node.floatBits, node.string ? node.string->c_str() : nullptr, env);
    return true;
}

} // namespace obl::game_parameters_construct
