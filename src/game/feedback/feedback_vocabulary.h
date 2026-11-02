#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The words of the feedback system that a pack defines (`scripts/feedback_vocabulary.json`, format `obl.feedback_vocabulary`): the text
// key of each commodity family's complaint, the message-type words of the scenario script, the notice shown when a staff class is
// missing, and the staff professions the player can hire. The engine keeps the mechanism and reads the rows from here.
namespace obl::feedback {

struct MessageType {
    std::string word;
    uint32_t type = 0;
    bool suppressWorld = false; // the word silences the world notices instead of one type
    bool suppressObject = false; // the word silences the object notices instead of one type
};

struct MissingStaff {
    int objectClass = 0; // object::ObjectType of the counted class
    std::string key;
    uint32_t type = 0; // message type of the notice
};

struct Profession {
    std::string name;
    int hireType = 0; // gui_actions::StaffType of the hire button
};

struct Vocabulary {
    std::vector<std::string> commodityKeys; // indexed by commodity family
    std::vector<MessageType> messageTypes;
    std::vector<MissingStaff> missingStaff; // in the order the check counts them
    std::vector<Profession> professions;
};

// The vocabulary of the running game: empty until a pack's file is loaded (LoadVocabulary, or ParseVocabulary in a test).
const Vocabulary& CurrentVocabulary();
void SetVocabulary(Vocabulary vocabulary);

// Parses and validates a vocabulary file; `out` is left empty and `error` says why when it is refused.
bool ParseVocabulary(std::string_view json, Vocabulary& out, std::string& error);

} // namespace obl::feedback
