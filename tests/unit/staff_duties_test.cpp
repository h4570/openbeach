#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/staff_duties.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using ui::Sprite;
using ui::SpriteAnim;
using ui::SpriteInstance;
using namespace obl::staff_duties;

namespace {

struct DayEnv {
    float hour;
    float DayTime() const { return hour; }
};

struct BinEnv {
    object::Base* bin = nullptr;
    std::vector<Message> posted;
    object::Base* Object(int32_t) { return bin; }
    void Post(Message& message) { posted.push_back(message); }
};

bool On(const object::Staff& member, float hour)
{
    DayEnv env{hour};
    return IsOnShift(member, env);
}

std::unique_ptr<uint8_t[]> Raw(size_t size)
{
    return std::make_unique<uint8_t[]>(size);
}

} // namespace

OBL_TEST_CASE("staff_duties.shift_is_stored_and_tested_as_unsigned_hours")
{
    auto raw = Raw(sizeof(object::Staff));
    auto* member = &obl::unit::StampVtable<object::Staff>(raw.get());
    SetShift(*member, 8, 18);
    OBL_CHECK_EQ(member->m_StartTime, 8);
    OBL_CHECK_EQ(member->m_EndTime, 18);
    OBL_CHECK(On(*member, 8.0f));
    OBL_CHECK(On(*member, 17.99f));
    OBL_CHECK(!On(*member, 18.0f));
    OBL_CHECK(!On(*member, 7.5f));
    SetShift(*member, 22, 6); // wraps past midnight
    OBL_CHECK(On(*member, 23.0f));
    OBL_CHECK(On(*member, 5.9f));
    OBL_CHECK(!On(*member, 6.0f));
    OBL_CHECK(!On(*member, 12.0f));
    SetShift(*member, 5, 5); // an empty shift is never on
    OBL_CHECK(!On(*member, 5.0f));
    SetShift(*member, -1, 3);
    OBL_CHECK(On(*member, 2.0f)); // start 0xffffffff > end: on only before the end
    OBL_CHECK(!On(*member, 3.0f));
}

OBL_TEST_CASE("staff_duties.builder_number_is_stored")
{
    auto raw = Raw(sizeof(Builder));
    auto* builder = &obl::unit::StampVtable<Builder>(raw.get());
    SetBuilderNumber(*builder, 7);
    OBL_CHECK_EQ(builder->m_BuilderNumber, 7);
}

OBL_TEST_CASE("staff_duties.bin_walk_moves_the_point_by_the_bins_direction_and_posts_the_target")
{
    auto binRaw = Raw(sizeof(object::Base));
    auto* bin = &obl::unit::StampVtable<object::Base>(binRaw.get());
    SpriteInstance sprite{};
    bin->m_sprite = &sprite;
    bin->m_WorldXYZ[0] = 100.0f; // x
    bin->m_WorldXYZ[1] = 200.0f; // y
    auto cleanerRaw = Raw(sizeof(object::Base));
    auto* cleaner = &obl::unit::StampVtable<object::Base>(cleanerRaw.get());
    cleaner->m_id = 77;
    struct Case {
        int direction;
        float first, second; // written to point[0], point[1]
    };
    const Case cases[] = {{0, 231, 100}, {1, 222, 78}, {2, 200, 69}, {3, 178, 122}, {4, 169, 100}, {5, 178, 122}, {6, 200, 131},
            {7, 222, 122}, {8, 200, 100}, {-1, 200, 100}};
    for (const Case& c : cases) {
        sprite.m_direction = c.direction;
        BinEnv env;
        env.bin = bin;
        float point[2] = {0, 0};
        MoveToBin(5, *cleaner, point, env);
        OBL_CHECK_EQ(point[0], c.first);
        OBL_CHECK_EQ(point[1], c.second);
        OBL_CHECK_EQ(env.posted.size(), size_t(1));
        const Message& m = env.posted[0];
        OBL_CHECK_EQ(m.sender, 77);
        OBL_CHECK_EQ(m.receiver, 77);
        OBL_CHECK_EQ(m.type, 8);
        OBL_CHECK_EQ(m.flags, int(c.second));
        OBL_CHECK_EQ(m.data, int(c.first));
    }
    BinEnv missing;
    float point[2] = {1, 2};
    MoveToBin(5, *cleaner, point, missing);
    OBL_CHECK_EQ(point[0], 1.0f);
    OBL_CHECK(missing.posted.empty());
}

OBL_TEST_CASE("staff_duties.direction_is_clamped_to_the_animations_last_one")
{
    using obl::ui::Sprite;
    using obl::ui::SpriteAnim;
    using obl::ui::SpriteInstance;
    auto rawAnim = Raw(sizeof(SpriteAnim));
    auto* anim = reinterpret_cast<SpriteAnim*>(rawAnim.get());
    anim->m_directions = 8;
    SpriteAnim* anims[1] = {anim};
    auto rawSprite = Raw(sizeof(Sprite));
    auto* sprite = reinterpret_cast<Sprite*>(rawSprite.get());
    sprite->m_anims = anims;
    auto rawInst = Raw(sizeof(SpriteInstance));
    auto* inst = reinterpret_cast<SpriteInstance*>(rawInst.get());
    inst->m_sprite = sprite;
    inst->m_animIndex = 0;

    SetDirection(*inst, 3);
    OBL_CHECK_EQ(inst->m_direction, 3);
    SetDirection(*inst, 7);
    OBL_CHECK_EQ(inst->m_direction, 7);
    SetDirection(*inst, 8); // first direction past the end
    OBL_CHECK_EQ(inst->m_direction, 7);
    SetDirection(*inst, 200);
    OBL_CHECK_EQ(inst->m_direction, 7);
}
