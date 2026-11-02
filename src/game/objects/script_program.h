#pragma once

#include "game/objects/script_program_types.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

// Parser of the object scripts (`Data/Script/Object/*.ble`: the script load and the ~14 parse
// helpers, `ReadToken`, `AppendCommand`, `ResolveValue`; docs/architecture/objects.md). Pure logic: no handlers, no game
// memory (the blocks of the command lists come from an injected allocator), so it is unit-tested.
//
// The text is a decoded `.ble`: tokens split at whitespace and `;`, `/` starting a comment line (the tokenizer is the one of
// src/game/map/path_tile_script.h). The grammar keeps its quirks (the data relies on them): `If`/`Switch` conditions are cut out of the
// text by their parentheses and split at single spaces, `Else` flips the open `If`, commands inside a `Switch` go to its last
// `Case` unless an `If` is open, and a `Switch` nested in a `Case` forgets its parent (only one switch is open at a time).
namespace obl::script_program {

// `{id, name}` row of a vocabulary table, in the order they are compared (the first match wins).
struct NamedId {
    int id;
    std::string name;
};

// The identifier tables of the parser: states and messages from scripts/object_vocabulary.json (script_vocabulary.cpp), functions
// and values from the engine's script API (script_program_api.h); the unit tests pass their own.
struct Vocabulary {
    std::vector<NamedId> states; // STATE_*, compared exactly
    std::vector<NamedId> messages; // MT_*, compared exactly
    std::vector<NamedId> functions; // `Function <name>;`, compared ignoring ASCII case
    std::vector<NamedId> values; // Variable and constant names accepted as script operands, compared ignoring ASCII case
};

// The allocator owns command list blocks. Parsing allocates through this interface and teardown releases through it.
// The game supplies operator new and operator delete; tests may use a recording allocator.
struct Memory {
    void* (*allocate)(size_t size);
    void (*release)(void* block);
};

// A script the parser rejects (unknown name or command, malformed condition, truncated text).
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The script load after the file is read: `text` = the decoded file (`size` bytes) followed by the terminator
// byte the file reader appends (`size + 1` readable bytes). Appends the states to `script` (a constructed ScriptProgram,
// normally with no state yet) and leaves its `m_text` null. Throws ParseError; the states parsed before the error stay in
// `script` for Destroy.
void Parse(script::ScriptProgram& script, const char* text, size_t size, const Vocabulary& vocabulary, const Memory& memory);

// Destroys the states and their command lists: frees every command list, payload
// and value of the states and clears them (and the state count), so a second call does nothing. Accepts what any parser of the
// format built, and the layout Parse builds is the one every destructor of the format accepts.
void Destroy(script::ScriptProgram& script, const Memory& memory);

// Exact lifecycle contracts the tests pin. Construct leaves m_text untouched; DestructStates leaves
// state heads/counts dangling. FreeCommand destroys successors and payload, but never the head itself.
void Construct(script::ScriptProgram& script);
void DestructStates(script::ScriptProgram& script, const Memory& memory);
void FreeCommand(script::ScriptCommand& head, const Memory& memory);

// First difference between two parsed scripts ("" when equal): state count, ids, message handlers, and every command list
// down to the payloads. Pointers are compared by what they point to; fields the parser leaves unset (the operator of an `If`
// without right value, the data of a value that has none) are ignored.
std::string Diff(const script::ScriptProgram& a, const script::ScriptProgram& b);

// Counts the nodes of a command list, recursing into `If` branches and `Switch` cases (statistics for tests).
struct ListStats {
    int nodes = 0;
    int opcodes[16] = {};
};
ListStats Count(const script::ScriptProgram& script);

} // namespace obl::script_program
