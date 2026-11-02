#pragma once

#include <cstdint>
#include <list>

namespace obl::messages {

enum MessageType : int {
    MT_KILLOBJECT = 0,
    MT_OBJECTCREATED = 1,
    MT_DISASTER_FINISHED = 2,
    MT_STATECHANGE = 3,
    MT_SETOWNER = 4,
    MT_MOVE2POINT = 5,
    MT_MOVE2OBJECT = 6,
    MT_CANT_MOVE = 7,
    MT_PAVEMENT2POINT = 8,
    MT_PAVEMENT2OBJECT = 9,
    MT_MOVE2SEAPOINT = 10,
    MT_SWIM2SEAPOINT = 11,
    MT_SEAVEHICLE_MOVE2POINT = 12,
    MT_PATH_THROUGH_SCENERY = 13,
    MT_PATH_THROUGH_SCENERY2OBJECT = 14,
    MT_MOVE_DIRECT_2POINT = 15,
    MT_MOVE_DIRECT_2OBJECT = 16,
    MT_PATHBLOCKED = 17,
    MT_VEHICLE_STOP_TURN = 18,
    MT_VEHICLE_DEFAULT_ROTATE = 19,
    MT_VEHICLE_SMOOTH_TURN = 20,
    MT_MOVESTOP = 21,
    MT_MOVEIMHERE = 22,
    MT_ENTERED_SEA = 23,
    MT_MOVE_CARRY_ON = 24,
    MT_LAST_POINT = 25,
    MT_FOLLOWOBJECT = 26,
    MT_REACHED_FOLLOW_DISTANCE = 27,
    MT_MOVE_INSIDE_BUILDING = 28,
    MT_STOP_AND_FACE_ME = 29,
    MT_IM_READY = 30,
    MT_MOVE2QUEUEPOINT = 31,
    MT_QUEUE_FULL = 32,
    MT_JOIN_QUEUE = 33,
    MT_BUILDING_FULL = 34,
    MT_REQUEST_COMMOD = 35,
    MT_SERVE_GUEST = 36,
    MT_STAFF_CREATED = 37,
    MT_NEW_DAY = 38,
    MT_REPAIR = 39,
    MT_CONSTRUCT = 40,
    MT_START_EVENT = 41,
    MT_NEED_REPAIR = 42,
    MT_NEED_CLEANING = 43,
    MT_BUILDING_BEING_REMOVED = 44,
    MT_BUILDING_CLOSING = 45,
    MT_DEMOLISH = 46,
    MT_RE_POLL_FLAG_STATE = 47,
    MT_SERVED_COMMOD = 48,
    MT_COMMOD_PRICE = 49,
    MT_COMMOD_QUALITY = 50,
    MT_GUEST_ENTER_VEHICLE = 51,
    MT_GUEST_LEFT_VEHICLE = 52,
    MT_DROWNING = 53,
    MT_ANNOY_GUEST = 54,
    MT_THROWUP = 55,
    MT_FINISH_ACTION = 56,
    MT_DO_NEXT_ACTION = 57,
    MT_EVENT_STARTING = 58,
    MT_EVENT_FINISHED = 59,
    MT_PLAYANIMATION = 60,
    MT_FIGHT_TIME = 61,
    MT_IMDEAD = 62,
    MT_UPDATEOPINIONS = 63,
    MT_MY_WORST_OPINION = 64,
    MT_MY_BEST_OPINION = 65,
    MT_IMPROVE_OPINIONS = 66,
    MT_CHAT = 67,
    MT_STOPCHAT = 68,
    MT_WAIT_FOR_BOAT = 69,
    MT_PRICE_OK = 70,
    MT_PRICE_NOT_OK = 71,
    MT_BAD_WEATHER = 72,
    MT_SPOT_OPINION = 73,
    MT_NO_STOCK = 74,
    MT_SEA_STATE_CHANGE = 75,
    MT_VEHICLE_CREATED = 76,
    MT_VEHICLE_FULL = 77,
    MT_BOARD_VEHICLE = 78,
    MT_LEAVE_VEHICLE = 79,
    MT_TABLE_CREATED = 80,
    MT_LOUNGER_CREATED = 81,
    MT_STAFF_BUSY = 82,
    MT_GUEST_SERVED = 83,
    MT_LOOK_AROUND = 84,
    MT_NEW_HOUR = 85,
    MT_DIRECT_TO_POINT = 86,
    MT_PAY_A_FINE = 87,
    MT_DEC_STAFF_IDLE_TIME = 88,
    MT_INCREASE_ENERGY = 89,
    MT_HEAD_HOME = 90,
    MT_MORE_REPAIRS = 91,
    MT_REPAIRS_FINISHED = 92,
    MT_LOOK_FOR_BETTER_JOB = 93,
    MT_MORE_CONSTRUCTION = 94,
    MT_CONSTRUCTION_FINISHED = 95,
    MT_START_BUILDING = 96,
    MT_CONSTRUCTION_HALFDONE = 97,
    MT_BUILDERS_LEFT_BASEPLATE = 98,
    MT_REMOVE_COST_POINT = 99,
    MT_NEXT_MOVE_POINT = 100,
    MT_RECEIVED_ATTENTION = 101,
    MT_NO_ATTENTION = 102,
    MT_DIALOG_CLOSED = 103,
    MT_RESTORE_ANIMATION = 104,
    MT_WAKEUP = 105,
    MT_GOTO_BED = 106,
    MT_LEAVE_BUILDING = 107,
};
constexpr int kMessageTypeCount = 108;

// Special `sender`/`receiver` values (signed, they must stay ints).
constexpr int kMessageSenderSilent = -3; // Post delivers it at once even while the game is paused
constexpr int kMessageReceiverWorld = -2; // routed to world::ReceiveMessage

// A message record (0x20 B; objects get it by pointer, so the size and the field order stay). `sender` and `receiver` are object
// ids (or the special values above); `data`/`floatData` carry the type-specific payload (script `MessData`/`MessFloatData`) and
// `extra` a second integer whose meaning depends on the type; `time` is the delivery time in game seconds (`World::m_gameTime` for
// an immediate post, `now + delay` for a delayed one). The save labels are in message_lifecycle.h.
struct Message {
    int sender;
    int receiver;
    int type;
    int flags;
    int data;
    int extra; // saved last, after `time`
    float floatData;
    float time;
    bool operator==(const Message&) const = default;
};

using MessageList = std::list<Message>;

// `delayed` waits for its time (the only saved list); `working` is the batch Update is delivering, empty between frames.
struct Handler {
    MessageList delayed;
    MessageList working;
};

} // namespace obl::messages
