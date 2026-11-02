#include "unit_test.h"

#include "game/feedback/feedback_vocabulary.h"
#include "importer/src/writers/script_convert.h"

#include <nlohmann/json.hpp>

#include <string>

using obl::feedback::ParseVocabulary;
using obl::feedback::Vocabulary;

namespace {

// The converter's file with one edit applied, parsed again by the engine's reader.
bool ParsesAfter(const char* path, const nlohmann::json& value, std::string& error)
{
    nlohmann::json doc = nlohmann::json::parse(obl::convert::FeedbackVocabularyJson());
    doc[nlohmann::json::json_pointer(path)] = value;
    Vocabulary vocabulary;
    return ParseVocabulary(doc.dump(), vocabulary, error);
}

} // namespace

OBL_TEST_CASE("feedback vocabulary: the converter's file loads with every table")
{
    Vocabulary vocabulary;
    std::string error;
    OBL_CHECK(ParseVocabulary(obl::convert::FeedbackVocabularyJson(), vocabulary, error));
    OBL_CHECK_EQ(vocabulary.commodityKeys.size(), size_t(26));
    OBL_CHECK_EQ(vocabulary.messageTypes.size(), size_t(13));
    OBL_CHECK(vocabulary.messageTypes[0].suppressWorld && vocabulary.messageTypes[1].suppressObject);
    OBL_CHECK_EQ(vocabulary.missingStaff.size(), size_t(5));
    OBL_CHECK_EQ(vocabulary.missingStaff[0].key, std::string("FEEDBACK_NO_CLEANERS"));
    OBL_CHECK_EQ(vocabulary.missingStaff[0].objectClass, 11); // the cleaner class
    OBL_CHECK_EQ(vocabulary.missingStaff[0].type, 12u); // MESS_TYPE_CLEANER
    OBL_CHECK_EQ(vocabulary.missingStaff[4].objectClass, 7);
    OBL_CHECK_EQ(vocabulary.professions.size(), size_t(6));
    OBL_CHECK_EQ(vocabulary.professions[5].name, std::string("SecurityGuard"));
    OBL_CHECK_EQ(vocabulary.professions[5].hireType, 5);
}

OBL_TEST_CASE("feedback vocabulary: broken tables are refused and leave nothing behind")
{
    std::string error;
    OBL_CHECK(!ParsesAfter("/commodity_keys", nlohmann::json::array({"FEEDBACK_ONE"}), error)); // not one key per family
    OBL_CHECK(!ParsesAfter("/missing_staff/0/class", "Janitor", error));
    OBL_CHECK(!ParsesAfter("/missing_staff/0/type", "MESS_TYPE_WORLD", error)); // a silencing word has no type number
    OBL_CHECK(!ParsesAfter("/professions/0/hire_type", 6, error));
    OBL_CHECK(!ParsesAfter("/version", 2, error));
    Vocabulary vocabulary;
    OBL_CHECK(!ParseVocabulary("{", vocabulary, error));
    OBL_CHECK(vocabulary.commodityKeys.empty() && vocabulary.professions.empty());
}
