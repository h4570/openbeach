#include "core/text16.h"
#include "unit_test.h"

#include "game/objects/building_thoughts.h"

#include <cwchar>
#include <vector>

using namespace obl;
using object::ThoughtEntry;
using object::ThoughtList;

namespace {

// A thought list that deletes what is left at the end, like the building teardown.
struct Thoughts {
    ThoughtList list;
    ~Thoughts() { thought_ops::Clear(list); }

    std::vector<int32_t> Ids() const
    {
        std::vector<int32_t> ids;
        for (const ThoughtEntry* entry : list) ids.push_back(entry->m_objectId);
        return ids;
    }
};

} // namespace

OBL_TEST_CASE("building_thoughts.add_copies_text_newest_first")
{
    Thoughts t;
    Char16 text[] = u"hello";
    AddBuildingThought(t.list, text, 7);
    text[0] = u'J'; // the entry has its own copy
    AddBuildingThought(t.list, u"second", 8);
    OBL_CHECK_EQ(t.list.size(), 2u);
    OBL_CHECK_EQ(t.list[0]->m_objectId, 8);
    OBL_CHECK(obl::text16::Compare(t.list[0]->m_text, u"second") == 0);
    OBL_CHECK_EQ(t.list[1]->m_objectId, 7);
    OBL_CHECK(obl::text16::Compare(t.list[1]->m_text, u"hello") == 0);
}

OBL_TEST_CASE("building_thoughts.list_keeps_the_newest_eight")
{
    Thoughts t;
    for (int i = 1; i <= 40; ++i) AddBuildingThought(t.list, u"thought", i);
    OBL_CHECK_EQ(t.list.size(), 8u);
    for (size_t row = 0; row < 8; ++row) OBL_CHECK_EQ(t.list[row]->m_objectId, 40 - static_cast<int32_t>(row));
}

OBL_TEST_CASE("building_thoughts.remove_takes_every_thought_of_the_object")
{
    Thoughts t;
    for (int32_t id : {1, 2, 1, 3, 1}) AddBuildingThought(t.list, u"t", id);
    OBL_CHECK(t.Ids() == (std::vector<int32_t>{1, 3, 1, 2, 1}));
    RemoveBuildingThoughts(t.list, 1);
    OBL_CHECK(t.Ids() == (std::vector<int32_t>{3, 2}));
}

OBL_TEST_CASE("building_thoughts.remove_of_a_stranger_changes_nothing")
{
    Thoughts t;
    AddBuildingThought(t.list, u"t", 5);
    RemoveBuildingThoughts(t.list, 6);
    OBL_CHECK(t.Ids() == (std::vector<int32_t>{5}));
    RemoveBuildingThoughts(t.list, 5);
    OBL_CHECK(t.list.empty());
    RemoveBuildingThoughts(t.list, 5); // empty list
    OBL_CHECK(t.list.empty());
}

OBL_TEST_CASE("building_thoughts.remove_skips_a_null_entry")
{
    Thoughts t;
    AddBuildingThought(t.list, u"t", 5);
    t.list.push_back(nullptr);
    RemoveBuildingThoughts(t.list, 5);
    OBL_CHECK_EQ(t.list.size(), 1u);
    OBL_CHECK(t.list[0] == nullptr);
}

OBL_TEST_CASE("building_thoughts.at_answers_null_past_either_end")
{
    Thoughts t;
    AddBuildingThought(t.list, u"a", 1);
    AddBuildingThought(t.list, u"b", 2);
    OBL_CHECK_EQ(thought_ops::At(t.list, 0)->m_objectId, 2);
    OBL_CHECK_EQ(thought_ops::At(t.list, 1)->m_objectId, 1);
    OBL_CHECK(thought_ops::At(t.list, 2) == nullptr);
    OBL_CHECK(thought_ops::At(t.list, -1) == nullptr);
}
