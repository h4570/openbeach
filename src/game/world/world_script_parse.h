#pragma once

#include "game/world/world_script_types.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace obl::world_script_parse {

struct NamedId {
    int id;
    std::string name;
};
struct Vocabulary {
    std::vector<NamedId> messages, functions, values, beers, modifiers;
};
struct Memory {
    void* (*allocate)(size_t);
    void (*release)(void*);
};
// Beer names are resolved by the owner of WorldParams.
using BeerId = int (*)(const char*);
// Optional fields the source actually set (BoatEvent/Disaster/Weather leave others unset).
// This lets the oracle compare every explicit option even when its type ignores it.
struct ParseDetails {
    std::unordered_map<const void*, uint32_t> optionalFields;
};
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Appends to a constructed WorldScript. Text is decoded XOR-FF data, with `size`
// readable bytes. No globals or game heap; all owned blocks use Memory.
// On failure every allocation from this invocation is rolled back.
void Parse(world_script::WorldScript& script, const char* text, size_t size, const Vocabulary& vocabulary, const Memory& memory,
        BeerId beerId, ParseDetails* details = nullptr);
void Destroy(world_script::WorldScript& script, const Memory& memory);

// Lifecycle contracts of construct and destruct.
// Construct writes every field but the parse cursor and the parsed list; the parser's If stack (a game global)
// is the caller's. Destruct releases the ten state names, then the states from the last one down, each OnEntry/OnExit/OnUpdate list and
// then the handlers from the last one down (OnReceipt, OnSend), calling `release` exactly where the deletes happen,
// null owned pointers included. It leaves names and counters dangling. It also frees OnSend lists (a handler destructor without
// that step never terminates on a non-empty one).
void Construct(world_script::WorldScript& script);
void Destruct(world_script::WorldScript& script, const Memory& memory);
// Semantic comparison: ignores unset words, compares owned data by value.
std::string Diff(const world_script::WorldScript& a, const world_script::WorldScript& b, const ParseDetails* bDetails = nullptr);
struct ListStats {
    int nodes = 0;
    int opcodes[26] = {};
};
ListStats Count(const world_script::WorldScript& script);

} // namespace obl::world_script_parse
