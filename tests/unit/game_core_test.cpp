#include "game/game/game_core.h"
#include "game/game/progress_file.h"
#include "unit_test.h"

OBL_TEST_CASE("Game progress preserves completion bits and unlocks only the next scenario")
{
    int32_t progress[12], selected;
    obl::game_core::Reset(progress, selected);
    OBL_CHECK_EQ(selected, 0);
    OBL_CHECK_EQ(progress[0], 4);
    OBL_CHECK_EQ(progress[1], 4);
    OBL_CHECK_EQ(progress[2], 2);
    OBL_CHECK_EQ(progress[11], 1);
    for (int nextFlag = 0; nextFlag < 16; ++nextFlag) {
        obl::game_core::Reset(progress, selected);
        progress[3] = nextFlag;
        obl::game_core::Complete(progress, 2);
        OBL_CHECK_EQ(progress[2], 10);
        OBL_CHECK_EQ(progress[3], (nextFlag & 12) ? nextFlag : 2);
        OBL_CHECK_EQ(obl::game_core::SelectAfterLoad(progress, false), (nextFlag & 8) ? 4 : 3);
        OBL_CHECK_EQ(obl::game_core::SelectAfterLoad(progress, true), 0);
    }
    obl::game_core::Complete(progress, 11);
    OBL_CHECK_EQ(obl::game_core::SelectAfterLoad(progress, false), 11);
}

OBL_TEST_CASE("Game unlock cheat opens the campaign or completes every sandbox scenario")
{
    int32_t progress[12], selected;
    obl::game_core::Reset(progress, selected);
    progress[5] = 8;
    obl::game_core::UnlockAll(progress, false);
    OBL_CHECK_EQ(progress[0], 4);
    OBL_CHECK_EQ(progress[1], 4);
    for (int i = 2; i < 12; ++i) OBL_CHECK_EQ(progress[i], 2);
    obl::game_core::UnlockAll(progress, true);
    for (int i = 0; i < 12; ++i) OBL_CHECK_EQ(progress[i], 8);
}

OBL_TEST_CASE("Game scenario GUI distinguishes sandbox availability and locked campaign entries")
{
    for (int flag = 0; flag < 16; ++flag) {
        OBL_CHECK_EQ(obl::game_core::Selectable(flag, false), (flag & 1) == 0);
        OBL_CHECK_EQ(obl::game_core::Selectable(flag, true), (flag & 12) != 0);
    }
}

OBL_TEST_CASE("Game scenario list names come from the campaign sizes in directory order")
{
    const auto names = obl::game_core::ScenarioListNames(12, 12);
    OBL_CHECK_EQ(names.size(), size_t(24));
    OBL_CHECK_EQ(names[0], std::string("SandScenario1"));
    OBL_CHECK_EQ(names[1], std::string("SandScenario10"));
    OBL_CHECK_EQ(names[4], std::string("SandScenario2"));
    OBL_CHECK_EQ(names[12], std::string("Scenario1"));
    OBL_CHECK_EQ(names[13], std::string("Scenario10"));
    OBL_CHECK_EQ(names[23], std::string("Scenario9"));
    OBL_CHECK_EQ(obl::game_core::ScenarioListNames(2, 0).size(), size_t(2));
}

OBL_TEST_CASE("Progress file round-trips the flags and migrates the old 48-byte file")
{
    int32_t progress[12], selected, loaded[12];
    obl::game_core::Reset(progress, selected);
    progress[2] = 10;
    OBL_CHECK(obl::progress_file::FromJson(obl::progress_file::ToJson(progress), loaded));
    for (int i = 0; i < 12; ++i) OBL_CHECK_EQ(loaded[i], progress[i]);

    const std::string oldFile(reinterpret_cast<const char*>(progress), 48);
    int32_t migrated[12] = {};
    OBL_CHECK(obl::progress_file::FromSeedFile(oldFile, migrated));
    for (int i = 0; i < 12; ++i) OBL_CHECK_EQ(migrated[i], progress[i]);

    int32_t untouched[12];
    obl::game_core::Reset(untouched, selected);
    OBL_CHECK(!obl::progress_file::FromSeedFile(oldFile.substr(1), untouched));
    OBL_CHECK(!obl::progress_file::FromJson("{\"format\":\"obl.progress\",\"version\":1,\"scenarios\":[1]}", untouched));
    OBL_CHECK(!obl::progress_file::FromJson("not json", untouched));
    OBL_CHECK_EQ(untouched[0], 4);
}
