#include "game/objects/furniture_draw.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
using obl::object::Furniture;
using obl::object::TableChair;
using obl::tables::TableDef;
using obl::ui::SpriteInstance;
namespace table = obl::table;

// Records every renderer call as text: the order and the arguments are the whole behavior of Draw.
struct RecordEnv {
    std::map<int32_t, obl::object::Base*> objects;
    std::map<const SpriteInstance*, std::string> names;
    std::vector<std::string> calls;
    std::vector<uint32_t> depths;
    int16_t depthWord = 0x1234;
    int outX = 40, outY = 50;
    float Time() { return 7.5f; }
    obl::object::Base* Object(int32_t id) { return objects.at(id); }
    std::string Name(const SpriteInstance* s) { return names.at(s); }
    static std::string Pos(const float* w) { return std::to_string(w[0]) + "," + std::to_string(w[1]) + "," + std::to_string(w[2]); }
    void WorldDrawPos(SpriteInstance* s, float* w, float, int& x, int& y, int16_t& depth)
    {
        calls.push_back("pos " + Name(s) + " " + Pos(w));
        x = outX;
        y = outY;
        depth = depthWord;
    }
    void DrawInWorld(SpriteInstance* s, float* w, float t) { calls.push_back("world " + Name(s) + " " + Pos(w) + " " + std::to_string(t)); }
    void DrawClipped(SpriteInstance* s, int x, int y, uint32_t depth, float)
    {
        calls.push_back("clipped " + Name(s) + " " + std::to_string(x) + "," + std::to_string(y));
        depths.push_back(depth);
    }
    float GuestIllness(obl::object::Base&)
    {
        calls.push_back("illness");
        return 0.25f;
    }
    float GuestTone(obl::object::Base&)
    {
        calls.push_back("tone");
        return 1.5f;
    }
    void DrawInWorldToned(SpriteInstance* s, float* w, float, float tone, float illness)
    {
        calls.push_back("toned " + Name(s) + " " + Pos(w) + " " + std::to_string(tone) + " " + std::to_string(illness));
    }
    void DrawClippedToned(SpriteInstance* s, int x, int y, uint32_t depth, float, float tone, float illness)
    {
        calls.push_back("clippedToned " + Name(s) + " " + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(tone) + " " +
                std::to_string(illness));
        depths.push_back(depth);
    }
};

struct DrawFixture {
    RecordEnv env;
    TableDef definition{};
    Furniture tableObject{};
    SpriteInstance tableSprite{}, chairA{}, chairB{}, plateA{}, plateB{}, waiterSprite{}, guestSprite{};
    char chairNameA[24] = "chairA", chairNameB[24] = "chairB";
    SpriteInstance* chairSprites[2] = {&chairA, &chairB};
    SpriteInstance* plateSprites[2] = {&plateA, &plateB};
    uint8_t drawPlate[2] = {0, 1};
    TableChair chairs[2]{};
    obl::object::Base owner{}, waiter{}, guest{};

    DrawFixture()
    {
        definition.m_TableSprite = "table";
        definition.m_NumChairs = 2;
        definition.m_XOffsetTable = 3;
        definition.m_YOffsetTable = -2;
        tableObject.m_definition = &definition;
        tableObject.m_sprite = &tableSprite;
        tableObject.m_WorldXYZ[0] = 10;
        tableObject.m_WorldXYZ[1] = 20;
        tableObject.m_WorldXYZ[2] = 30;
        tableObject.m_ownerId = 900;
        tableObject.m_WaiterID = 901;
        tableObject.m_chairSprites = chairSprites;
        tableObject.m_plateSprites = plateSprites;
        tableObject.m_pbDrawPlate = drawPlate;
        tableObject.m_chairs = chairs;
        chairs[0].m_ChairOccupier = -1;
        chairs[1].m_ChairOccupier = 902;
        chairs[1].m_ChairSeated = 1;
        chairA.m_name = chairNameA;
        chairB.m_name = chairNameB;
        owner.m_WorldXYZ[0] = 100;
        owner.m_WorldXYZ[1] = 200;
        owner.m_WorldXYZ[2] = 300;
        waiter.m_WorldXYZ[0] = 1;
        waiter.m_WorldXYZ[1] = 2;
        waiter.m_WorldXYZ[2] = 3;
        guest.m_WorldXYZ[0] = 4;
        guest.m_WorldXYZ[1] = 5;
        guest.m_WorldXYZ[2] = 6;
        waiter.m_sprite = &waiterSprite;
        guest.m_sprite = &guestSprite;
        env.objects = {{900, &owner}, {901, &waiter}, {902, &guest}};
        env.names = {{&tableSprite, "table"}, {&chairA, "chairA"}, {&chairB, "chairB"}, {&plateA, "plateA"}, {&plateB, "plateB"},
                {&waiterSprite, "waiter"}, {&guestSprite, "guest"}};
    }
    void Run() { table::Draw(tableObject, env); }
};

bool Contains(const std::vector<std::string>& calls, const std::string& wanted)
{
    for (const auto& call : calls)
        if (call.find(wanted) == 0) return true;
    return false;
}
} // namespace

OBL_TEST_CASE("A table draws itself offset by its definition, then its chairs and the seated guest in chair order")
{
    DrawFixture f;
    f.Run();
    const std::vector<std::string> expected = {
            "pos table 10.000000,20.000000,30.000000",
            "world table 13.000000,18.000000,30.000000 7.500000",
            "world chairA 10.000000,20.000000,30.000000 7.500000",
            "world chairB 10.000000,20.000000,30.000000 7.500000",
            "pos guest 4.000000,5.000000,6.000000",
            "illness",
            "tone",
            "clippedToned guest 40,50 1.500000 0.250000",
    };
    // The plate flags are only read with HAS_WAITER set, so no plate here.
    OBL_CHECK(f.env.calls == expected);
    OBL_CHECK(f.env.depths == std::vector<uint32_t>{0x1234});
}

OBL_TEST_CASE("A serve table draws only its own sprite")
{
    DrawFixture f;
    f.tableObject.m_serveTable = 1;
    f.Run();
    OBL_CHECK(f.env.calls.size() == 2);
}

OBL_TEST_CASE("A table whose sprite is NONE skips its own sprite and tones the guest in the world")
{
    DrawFixture f;
    f.definition.m_TableSprite = "NONE";
    f.Run();
    OBL_CHECK(!Contains(f.env.calls, "pos"));
    OBL_CHECK(Contains(f.env.calls, "toned guest 4.000000,5.000000,6.000000 1.500000 0.250000"));
    OBL_CHECK(!Contains(f.env.calls, "clippedToned"));
}

OBL_TEST_CASE("A plated table draws the waiter clipped above the table's depth and the plates the flags allow")
{
    DrawFixture f;
    f.definition.m_hasPlates = 1;
    f.tableObject.m_drawWaiter = 1;
    f.Run();
    OBL_CHECK(Contains(f.env.calls, "pos waiter 1.000000,2.000000,3.000000"));
    OBL_CHECK(Contains(f.env.calls, "clipped waiter 40,50"));
    OBL_CHECK(f.env.depths.front() == 0x1234 + table::kWaiterDepthBias);
    OBL_CHECK(!Contains(f.env.calls, "world plateA"));
    OBL_CHECK(Contains(f.env.calls, "world plateB"));
}

OBL_TEST_CASE("A waiter that is not shown and a guest that is not seated are not drawn")
{
    DrawFixture f;
    f.definition.m_hasPlates = 1;
    f.tableObject.m_drawWaiter = 0;
    f.chairs[1].m_ChairSeated = 0;
    f.Run();
    OBL_CHECK(!Contains(f.env.calls, "clipped"));
    OBL_CHECK(!Contains(f.env.calls, "pos guest"));
}

OBL_TEST_CASE("The jacuzzi bubbles and the shower water follow the owning building")
{
    DrawFixture f;
    f.definition.m_TableSprite = "Jacuzzi_bubble";
    std::strcpy(f.chairNameB, "Beach_shower_water");
    f.Run();
    OBL_CHECK(Contains(f.env.calls, "world table 103.000000,198.000000,300.000000"));
    OBL_CHECK(Contains(f.env.calls, "world chairA 10.000000,20.000000,30.000000"));
    OBL_CHECK(Contains(f.env.calls, "world chairB 100.000000,200.000000,300.000000"));
}

OBL_TEST_CASE("A table without chair sprites draws no chairs")
{
    DrawFixture f;
    f.tableObject.m_chairSprites = nullptr;
    f.Run();
    OBL_CHECK(!Contains(f.env.calls, "world chair"));
    OBL_CHECK(Contains(f.env.calls, "clippedToned guest"));
}
