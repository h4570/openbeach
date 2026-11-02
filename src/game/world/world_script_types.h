#pragma once

#include "game/objects/script_types.h"

#include <cstdint>
#include <string>

// The scenario script's records: the WorldScript state machine the script host owns, the payloads
// of the commands its interpreter executes and the parser's payloads.
namespace obl::world_script {

using script::ScriptCommand;

// Payloads of the WorldScript commands the interpreter executes itself (ScriptCommand::m_data).
// `Function <name>;` (opcode 0): the function number world::CallScriptFunction takes (0 PauseGame ... 8 NoLitter, 7 Debug is skipped).
struct ScriptFunctionData {
    int m_function;
};

// `Add`/`Minus`/`Divide`/`Multiply`/`Set <variable> <value>;` (opcodes 6-9, 10): the parsed tokens of both sides, each a
// world::GetScriptValue key and its data word (an index, a number's bits, or a text pointer). The left side is the target.
struct ScriptAssignData {
    int m_leftKey;
    void* m_leftData;
    int m_rightKey;
    void* m_rightData;
};

// `If ( a <op> b ) ... [Else ...] End` (opcode 4): the two branch lists and the arguments of EvalCondition
// (right key 0x110 = bare `If ( a )`). `m_inElse` is set while the parser fills the `Else` branch.
struct ScriptIfData {
    ScriptCommand* m_true;
    ScriptCommand* m_false;
    int m_leftKey;
    int* m_leftData;
    int m_rightKey;
    int* m_rightData;
    int m_op;
    int m_inElse;
};

// `ChangeState <state> [Time t];` (opcode 2): the state's name (heap copy, compared with the script's state names) and the delay.
struct ScriptChangeStateData {
    const char* m_name;
    float m_time;
};

// `PlaySound <name>;` (opcode 11): the sound's name (heap copy), played through Sound::PlayScaled at volume 1.
struct ScriptSoundData {
    const char* m_name;
};

// `MoveCamera x y <seconds>;` (opcode 24): the world point (z is always 0, the parser stores it) and the scroll time in
// seconds (the third number of the command; 0 jumps at once).
struct ScriptCameraData {
    float m_x;
    float m_y;
    float m_z;
    float m_seconds;
};

// `ShowMenu <dialog> [text key];` (opcode 25): the dialog's name and the text key it shows (null without the second word).
struct ScriptMenuData {
    const char* m_name;
    const char* m_textKey;
};

// WorldScript command opcodes (the parser's numbers).
constexpr int kScriptOpFunction = 0, kScriptOpMessage = 1, kScriptOpChangeState = 2, kScriptOpIf = 4, kScriptOpAdd = 6, kScriptOpMinus = 7,
              kScriptOpDivide = 8, kScriptOpMultiply = 9, kScriptOpSet = 10, kScriptOpPlaySound = 11, kScriptOpCreate = 12,
              kScriptOpPath = 13, kScriptOpGrass = 14, kScriptOpLoadScript = 15, kScriptOpArea = 16, kScriptOpBoatPoint = 17,
              kScriptOpBoatEntry = 18, kScriptOpBoatExit = 19, kScriptOpBoatEvent = 20, kScriptOpCircuitPoint = 21, kScriptOpDisaster = 22,
              kScriptOpWeather = 23, kScriptOpMoveCamera = 24, kScriptOpShowMenu = 25, kScriptOpUndefined = 26;
constexpr int kScriptFunctionDebug = 7;

constexpr int kWorldScriptHandlers = 5;
using WorldScriptState = script::ScriptState<kWorldScriptHandlers>;
constexpr int kMaxWorldScriptStates = 10;

// The scenario script of the world (`<n>.ble`), a state machine started by StartNewGame/StartLoadedGame at
// `STATE_WORLD_STARTUP`. The state tables come from construct, the
// runners (run on update list, dispatch message, start) and the add-command helper.
struct WorldScript {
    const char* m_text; // parse cursor, only meaningful inside WorldScriptLoad
    ScriptCommand* m_openCommand; // If block being filled by the parser, null at state level
    WorldScriptState m_states[kMaxWorldScriptStates];
    uint32_t m_numStates;
    std::string m_stateNames[kMaxWorldScriptStates]; // state i is m_states[i]; empty = never registered (duplicate name)
    int m_parsedList; // list being parsed (0 OnEntry, 1 OnExit, 2 OnUpdate, 3 OnReceipt, 4 OnSend)
    uint32_t m_currentState; // while parsing: the state being filled; at run time: the active state
};
// The script host owns the parsed program; saves retain only its active state index.

// Parser payloads used by the executor and the destructor.

struct WorldScriptOperand {
    int32_t m_kind;
    void* m_data;
};

// `Message MT_x <receiver> <data> <flags> [Time t] [MaxTime t];` (opcode 1): the message type and three operands, each a
// world::GetScriptValue key and its data word, and the delay bounds (zero when absent). The interpreter executes it directly; the
// float-data, state and send-from operands are set to 0x110 by the parser and unused.
struct WorldScriptMessageData {
    int32_t m_messageId;
    WorldScriptOperand m_receiver, m_data, m_flags;
    WorldScriptOperand m_floatData, m_state, m_sendFrom;
    float m_time, m_maxTime;
};

struct WorldScriptChangeStateData {
    char* m_name;
    float m_time;
};
struct WorldScriptTextData {
    char* m_text;
};
struct WorldScriptTwoTextsData {
    char* m_first;
    char* m_second;
};

struct WorldScriptCreateData {
    int32_t m_class;
    char* m_type;
    char* m_sprite;
    int32_t m_x, m_y, m_layer, m_number;
    float m_prices[5];
    int32_t m_beer;
    float m_beerPrice;
    int32_t m_age;
    uint8_t m_broken;
};

// `Path|Grass x y n <direction>;`: `m_layer` is the third number, which the executor uses as the count of tiles in the run.
struct WorldScriptTileData {
    int32_t m_x, m_y, m_direction, m_layer;
};
struct WorldScriptPoint {
    int32_t m_x, m_y;
};
struct WorldScriptAreaData {
    int32_t m_kind, m_count;
    WorldScriptPoint* m_points;
    WorldScriptPoint m_rectangle[4];
};
struct WorldScriptBoatPointData {
    float m_x, m_y, m_z;
    int32_t m_flags;
};
struct WorldScriptBoatEndpointData {
    float m_x, m_y, m_z, m_dx, m_dy;
    int32_t m_number;
};
struct WorldScriptCircuitData {
    int32_t m_x, m_y, m_kind;
};

// The parser keeps keywords before counts.
// WorldScriptExecute translates it to the manager's record.
struct WorldScriptBoatEventData {
    float m_hour;
    int32_t m_day;
    int32_t m_timeFlags, m_guestInfo;
    char m_keywords[20][64];
    int32_t m_counts[20];
    int32_t m_random[20];
    int32_t m_number;
};

struct WorldScriptDisasterData {
    int32_t m_kind;
    int32_t m_rectangle[4];
    float m_duration, m_hour;
    int32_t m_day, m_scale;
    float m_value;
    int32_t m_beer, m_replacementBeer;
    float m_strength;
    char* m_target;
};
struct WorldScriptWeatherData {
    int32_t m_kind;
    float m_hour;
    int32_t m_day;
    float m_modifiers[26];
};
struct WorldScriptCameraData {
    float m_x, m_y, m_z, m_speed;
};

// The `name` argument of world::PostPlaceCommand when the scenario script's `Create` command (opcode 0xc) places an object: a stack
// buffer of the interpreter that the world's place handler reads.
struct ScriptPlacement {
    char m_name[32]; // building / scenery / guest type, strcpy'd (a longer name runs on into m_sprite)
    char m_sprite[32]; // scenery: the sprite name
    int32_t m_x; // scenery/building: tile column; guest: world x as a float's bits
    int32_t m_y;
    int32_t m_layer; // building: the layer; scenery: byte 1, then the layer's low byte (the upper two bytes are not written)
    float m_prices[5]; // building; scenery writes 0xfffffffe into the first word
    int32_t m_beer; // building: the beer type (-1 none)
    float m_beerPrice;
    int32_t m_age; // building: days
    uint8_t m_broken; // building
};

} // namespace obl::world_script
