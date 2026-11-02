#include "unit_test.h"
#include "core/rand_tape.h"

OBL_TEST_CASE("rand_tape.replay_preserves_source_and_counter")
{
    obl::RandTape t;
    int state = 40;
    auto source = [&] { return ++state; };
    OBL_CHECK_EQ(t.Draw(source), 41);
    OBL_CHECK(t.BeginRecord());
    OBL_CHECK(t.BeginRecord()); // a nested frame, closed right away
    OBL_CHECK_EQ(t.Depth(), size_t(2));
    t.Cancel();
    OBL_CHECK_EQ(t.Depth(), size_t(1));
    OBL_CHECK(!t.ResetCalls());
    OBL_CHECK_EQ(t.Draw(source), 42);
    OBL_CHECK_EQ(t.Draw(source), 43);
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK_EQ(t.Calls(), uint64_t(1));
    OBL_CHECK_EQ(t.Draw(source), 42);
    OBL_CHECK_EQ(t.Draw(source), 43);
    OBL_CHECK(t.End());
    OBL_CHECK_EQ(state, 43);
    OBL_CHECK_EQ(t.Calls(), uint64_t(3));
    OBL_CHECK_EQ(t.Draw(source), 44);
    OBL_CHECK(t.ResetCalls());
    OBL_CHECK_EQ(t.Calls(), uint64_t(0));
}

OBL_TEST_CASE("rand_tape.rejects_under_overdraw_and_invalid_transitions")
{
    for (int draws : {0, 2}) {
        obl::RandTape t;
        int state = 0;
        auto source = [&] { return ++state; };
        OBL_CHECK(!t.End());
        OBL_CHECK(!t.BeginReplay());
        OBL_CHECK(t.BeginRecord());
        t.Draw(source);
        OBL_CHECK(t.BeginReplay());
        OBL_CHECK(!t.BeginReplay());
        for (int i = 0; i < draws; ++i) t.Draw(source);
        OBL_CHECK(!t.End());
        OBL_CHECK_EQ(state, 1);
        OBL_CHECK_EQ(t.Calls(), uint64_t(1));
        OBL_CHECK_EQ(t.Draw(source), 2);
    }
}

OBL_TEST_CASE("rand_tape.empty_and_cancelled_recordings")
{
    obl::RandTape t;
    OBL_CHECK(t.BeginRecord());
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK(t.End());
    OBL_CHECK(t.BeginRecord());
    t.Draw([] { return 17; });
    t.Cancel();
    OBL_CHECK_EQ(t.Calls(), uint64_t(1));
    OBL_CHECK(t.BeginRecord());
    t.Draw([] { return 18; });
    OBL_CHECK(t.BeginReplay());
    t.Cancel();
    OBL_CHECK_EQ(t.Calls(), uint64_t(2));
    OBL_CHECK(t.BeginRecord());
    t.Cancel();
}

OBL_TEST_CASE("rand_tape.paused_draws_are_not_recorded_but_still_counted")
{
    obl::RandTape t;
    int state = 10;
    auto source = [&] { return ++state; };
    OBL_CHECK(t.BeginRecord());
    OBL_CHECK_EQ(t.Draw(source), 11);
    t.Pause();
    t.Pause(); // nests
    OBL_CHECK_EQ(t.Draw(source), 12);
    t.Resume();
    OBL_CHECK_EQ(t.Draw(source), 13);
    t.Resume();
    OBL_CHECK_EQ(t.Draw(source), 14);
    OBL_CHECK_EQ(t.Calls(), uint64_t(4)); // the logical counter sees every draw
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK_EQ(t.Draw(source), 11);
    OBL_CHECK_EQ(t.Draw(source), 14); // the two paused draws are skipped in the replay
    OBL_CHECK(t.End());
    OBL_CHECK_EQ(state, 14);
    OBL_CHECK_EQ(t.Calls(), uint64_t(4));
    t.Resume(); // an unbalanced Resume does not go negative
    OBL_CHECK(t.BeginRecord());
    OBL_CHECK_EQ(t.Draw(source), 15);
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK_EQ(t.Draw(source), 15);
    OBL_CHECK(t.End());
}

OBL_TEST_CASE("rand_tape.nested_frames")
{
    obl::RandTape t;
    int state = 0;
    auto source = [&] { return ++state; };
    OBL_CHECK(t.BeginRecord());
    OBL_CHECK_EQ(t.Draw(source), 1);
    OBL_CHECK(t.BeginRecord());
    OBL_CHECK_EQ(t.Draw(source), 2);
    OBL_CHECK_EQ(t.Draw(source), 3);
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK(t.Replaying());
    OBL_CHECK(!t.BeginRecord()); // nothing starts on top of a replay
    OBL_CHECK_EQ(t.Draw(source), 2);
    OBL_CHECK_EQ(t.Draw(source), 3);
    OBL_CHECK(t.End()); // the inner replay reached neither the source nor the outer frame
    OBL_CHECK_EQ(state, 3);
    OBL_CHECK_EQ(t.Calls(), uint64_t(3));
    OBL_CHECK(!t.Replaying());
    OBL_CHECK_EQ(t.Draw(source), 4);
    OBL_CHECK(t.BeginReplay());
    for (int expected : {1, 2, 3, 4}) OBL_CHECK_EQ(t.Draw(source), expected);
    OBL_CHECK(t.End());
    OBL_CHECK_EQ(t.Depth(), size_t(0));
    OBL_CHECK_EQ(t.Calls(), uint64_t(4));
    OBL_CHECK_EQ(t.Draw(source), 5);
}

OBL_TEST_CASE("rand_tape.nested_pauses")
{
    obl::RandTape t;
    int state = 0;
    auto source = [&] { return ++state; };
    // a pause of the inner frame (a delivered message's cascade) hides the draw from the outer frame too
    OBL_CHECK(t.BeginRecord());
    OBL_CHECK(t.BeginRecord());
    t.Draw(source); // 1
    t.Pause();
    t.Draw(source); // 2
    t.Resume();
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK_EQ(t.Draw(source), 1);
    OBL_CHECK(t.End());
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK_EQ(t.Draw(source), 1);
    OBL_CHECK(t.End());
    // a pause of the outer frame (a delegated helper) does not stop an inner recording that starts inside it
    OBL_CHECK(t.BeginRecord());
    t.Draw(source); // 3
    t.Pause();
    OBL_CHECK(t.BeginRecord());
    t.Draw(source); // 4
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK_EQ(t.Draw(source), 4);
    OBL_CHECK(t.End());
    t.Resume();
    t.Draw(source); // 5
    OBL_CHECK(t.BeginReplay());
    OBL_CHECK_EQ(t.Draw(source), 3);
    OBL_CHECK_EQ(t.Draw(source), 5);
    OBL_CHECK(t.End());
    OBL_CHECK_EQ(state, 5);
}
