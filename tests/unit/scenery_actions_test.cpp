#include "core/text16.h"
#include "unit_test.h"
#include "game/objects/scenery_actions.h"
#include <string>
#include <vector>
#include <cctype>

using namespace obl;
namespace {
struct Env {
    std::vector<std::string> calls;
    std::vector<messages::Message> messages;
    unsigned repair = 0, clean = 0;
    int cleaners = 0, mechanics = 0;
    float zoom = 1;
    std::string textKey;
    bool fatal = false;
    void ShowDialog(const char* name, object::Scenery*) { calls.push_back(name); }
    void ForgetSelection() { calls.push_back("forget"); }
    void WorldToInt(const float*, int& x, int& y)
    {
        calls.push_back("project");
        x = 1000;
        y = -200;
    }
    uint32_t Width() { return 801; }
    uint32_t Height() { return 601; }
    float Zoom() { return zoom; }
    void SetCamera(int x, int y) { calls.push_back(std::to_string(x) + "," + std::to_string(y)); }
    void StopCamera() { calls.push_back("stop"); }
    void PrintInt(Char16* out, int value) { obl::text16::Copy(out, obl::text16::Decimal(value).c_str()); }
    int Count(int type) { return type == 11 ? cleaners : mechanics; }
    unsigned RepairStatus(int) { return repair; }
    unsigned CleaningStatus(int) { return clean; }
    const Char16* Lookup(const char* key)
    {
        textKey = key;
        return u"translated";
    }
    void Fatal(const char*) { fatal = true; }
    void Post(const messages::Message& message) { messages.push_back(message); }
    bool EqualName(const char* a, const char* b)
    {
        while (*a && *b) {
            if (std::tolower(static_cast<unsigned char>(*a++)) != std::tolower(static_cast<unsigned char>(*b++))) return false;
        }
        return *a == *b;
    }
    float Time() { return 12.5f; }
    void Animate(void*, const char* anim, float time)
    {
        OBL_CHECK_EQ(time, 12.5f);
        calls.push_back(anim);
    }
};
} // namespace

OBL_TEST_CASE("scenery_actions.selection_flags_order_and_zoom")
{
    tables::SceneryDef def{};
    def.m_Menu = "bin_panel";
    object::Scenery s{};
    s.m_definition = &def;
    for (int flags = 0; flags < 4; ++flags) {
        Env env;
        scenery::OnSelect(s, flags & 1, flags & 2, env);
        std::vector<std::string> expected;
        if (flags & 1) expected.push_back("bin_panel");
        if (flags & 2)
            for (const char* call : {"forget", "project", "600,-500", "stop"}) expected.push_back(call);
        OBL_CHECK(env.calls == expected);
    }
    Env env;
    env.zoom = 0.5f;
    scenery::OnSelect(s, false, true, env);
    OBL_CHECK_EQ(env.calls[2], "199,-801");
}

OBL_TEST_CASE("scenery_actions.text_statuses_flags_and_unrecognized_key")
{
    object::Scenery s{};
    Env env;
    Char16 out[128] = u"sentinel";
    s.m_NumItems = -17;
    scenery::GetVariableText(s, "BIN_LITTER", out, env);
    OBL_CHECK(obl::text16::Compare(out, u"-17") == 0);
    for (int staff : {0, 1, 5})
        for (int flags = 0; flags < 4; ++flags) {
            env.cleaners = env.mechanics = staff;
            s.m_urgentCleaningCalled = flags & 1;
            s.m_urgentRepairCalled = static_cast<uint8_t>(flags >> 1);
            scenery::GetVariableText(s, "BI_CALL_CLEANER_STATE", out, env);
            OBL_CHECK(obl::text16::Compare(out, staff && !(flags & 1) ? u"1" : u"0") == 0);
            scenery::GetVariableText(s, "BI_CALL_MECHANIC_STATE", out, env);
            OBL_CHECK(obl::text16::Compare(out, staff && !(flags >> 1) ? u"1" : u"0") == 0);
        }
    const char* repair[] = {"BI_REPAIR_NO_MECHANICS", "BI_REPAIR_CANT_REACH", "BI_REPAIR_NOT_NEEDED", "BI_REPAIR_REQUIRED",
            "BI_INSPECT_REQUIRED", "BI_REPAIR_MECHANIC_COMING", "BI_REPAIR_REPAIRING"};
    s.m_calledRepairing = s.m_urgentRepairCalled = 0;
    for (unsigned i = 0; i < 7; ++i) {
        env.repair = i;
        scenery::GetVariableText(s, "SCENERY_REPAIRSTATUS", out, env);
        OBL_CHECK_EQ(env.textKey, repair[i]);
    }
    env.repair = 2;
    for (int flags = 1; flags < 4; ++flags) {
        s.m_calledRepairing = flags & 1;
        s.m_urgentRepairCalled = static_cast<uint8_t>(flags >> 1);
        scenery::GetVariableText(s, "SCENERY_REPAIRSTATUS", out, env);
        OBL_CHECK_EQ(env.textKey, "BI_REPAIR_WAITING");
    }
    env.repair = 255;
    obl::text16::Copy(out, u"sentinel");
    scenery::GetVariableText(s, "SCENERY_REPAIRSTATUS", out, env);
    OBL_CHECK(obl::text16::Compare(out, u"sentinel") == 0);
    const char* clean[] = {"BI_CLEAN_NOT_NEEDED", "BI_CLEAN_NO_CLEANERS", "BI_CLEAN_NOT_NEEDED", "BI_CLEAN_REQUIRED", "BI_CLEAN_ONROUTE",
            "BI_CLEAN_CLEANING", "BI_CLEAN_NOT_NEEDED"};
    for (unsigned i = 0; i < 7; ++i) {
        env.clean = i;
        scenery::GetVariableText(s, "SCENERY_CLEANINGSTATUS", out, env);
        OBL_CHECK_EQ(env.textKey, clean[i]);
    }
    scenery::GetVariableText(s, "bin_litter", out, env);
    OBL_CHECK(env.fatal);
}

OBL_TEST_CASE("scenery_actions.vandalism_message_animation_and_repeat")
{
    tables::SceneryDef def{};
    def.m_Capacity = 10;
    ui::SpriteInstance sprite{};
    sprite.m_animIndex = 9;
    object::Scenery s{};
    s.m_id = 123;
    s.m_sprite = &sprite;
    s.m_definition = &def;
    const char* names[] = {"STREET_LAMP", "BIN", "bench", "other"};
    for (int type = 0; type < 4; ++type)
        for (int fill : {0, 10, 11})
            for (int flag : {0, 1, 2}) {
                Env env;
                sprite.m_name = const_cast<char*>(names[type]);
                s.m_NumItems = fill;
                s.m_vandalised = static_cast<uint8_t>(flag);
                s.m_PreVandlismAnim = 42;
                s.m_RepairState = 7;
                scenery::Vandalise(s, env);
                OBL_CHECK_EQ(s.m_vandalised, 1);
                OBL_CHECK_EQ(s.m_RepairState, flag == 1 ? 7u : 1u);
                OBL_CHECK_EQ(s.m_PreVandlismAnim, flag == 1 ? 42u : 9u);
                OBL_CHECK_EQ(env.messages.size(), flag == 1 ? 0u : 1u);
                if (!env.messages.empty()) {
                    OBL_CHECK_EQ(env.messages[0].sender, 123);
                    OBL_CHECK_EQ(env.messages[0].receiver, 123);
                    OBL_CHECK_EQ(env.messages[0].type, 84);
                    OBL_CHECK_EQ(env.messages[0].flags, 0);
                    OBL_CHECK_EQ(env.messages[0].data, 0);
                }
                OBL_CHECK_EQ(env.calls.size(), type == 3 ? 0u : 1u);
                if (type < 3)
                    OBL_CHECK_EQ(env.calls[0],
                            type == 0            ? "StreetLamp_Van_2Static"
                                    : type == 2  ? "bench_vandalise_static"
                                    : fill == 10 ? "fullbin_vandalise_static"
                                                 : "emptybin_vandalise_static");
                scenery::Vandalise(s, env);
                OBL_CHECK_EQ(env.messages.size(), flag == 1 ? 0u : 1u);
            }
}
