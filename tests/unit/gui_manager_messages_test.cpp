#include "core/char16.h"
#include "unit_test.h"
#include "game/ui/gui_manager_messages.h"
#include <string>
#include <vector>
using namespace obl::gui_manager_messages;
namespace ui = obl::ui;
namespace {
struct Env {
    std::vector<std::string> events;
    Rect rect{}; // the one balloon
    int screenWidth = 1024;
    bool paused = false;
    float speed = 1.0f;
    int measured = 100;
    int balloon = 0; // address token for the fake balloon
    std::vector<std::pair<obl::Text16, uint32_t>> history; // text, object id
    size_t cursor = 0;
    std::vector<obl::Text16> rows;
    int pressedRow = -1;
    void DeleteBalloon(void*) { events.push_back("delete"); }
    int MeasureText(const void*, const obl::Char16*) { return measured; }
    int ScreenWidth() { return screenWidth; }
    void* CreateBalloon(int x0, int y0, int x1, int y1, const obl::Char16*, int font, uint32_t color, int flags)
    {
        rect = {x0, y0, x1, y1};
        events.push_back("create font=" + std::to_string(font) + " color=" + std::to_string(color) + " flags=" + std::to_string(flags));
        return &balloon;
    }
    void PlayNewMessage() { events.push_back("sound"); }
    bool GamePaused() { return paused; }
    float GameSpeed() { return speed; }
    void Translate(void*, int dx, int dy)
    {
        events.push_back("move " + std::to_string(dx) + "," + std::to_string(dy));
        rect.x0 += dx;
        rect.x1 += dx;
        rect.y0 += dy;
        rect.y1 += dy;
    }
    Rect Bounds(void*) { return rect; }
    const obl::Char16* DayLabel() { return u"Day"; }
    const obl::Char16* Current() { return cursor < history.size() ? history[cursor].first.c_str() : nullptr; }
    const obl::Char16* BeginHistory(float* hour, uint32_t* day, uint32_t* objectId)
    {
        cursor = 0;
        return Next(hour, day, objectId);
    }
    const obl::Char16* NextHistory(float* hour, uint32_t* day, uint32_t* objectId)
    {
        ++cursor;
        return Next(hour, day, objectId);
    }
    const obl::Char16* Next(float* hour, uint32_t* day, uint32_t* objectId)
    {
        if (cursor >= history.size()) return nullptr;
        *hour = 13.5f;
        *day = 2;
        *objectId = history[cursor].second;
        return history[cursor].first.c_str();
    }
    int WholeHour(float hour) { return static_cast<int>(hour); }
    int QuarterMinutes(float) { return 30; }
    void ClearRows(void*)
    {
        rows.clear();
        events.push_back("clear");
    }
    void AddText(void*, int row, int offset, int width, uint32_t, uint32_t flags, int font, uint32_t, const obl::Char16* text, uint32_t)
    {
        events.push_back("row " + std::to_string(row) + " " + std::to_string(offset) + " " + std::to_string(width) + " " +
                std::to_string(flags) + " " + std::to_string(font));
        rows.push_back(text);
    }
    void SetRowButton(void*, int row, int offset, const char* graphic, int value)
    {
        events.push_back("button " + std::to_string(row) + " " + std::to_string(offset) + " " + graphic + " " + std::to_string(value));
    }
    bool PollRowButton(void*, uint32_t row, int* value)
    {
        events.push_back("poll " + std::to_string(row));
        if (static_cast<int>(row) != pressedRow) return false;
        *value = 77;
        return true;
    }
    void* FindListBox(const char* name)
    {
        events.push_back(std::string("find ") + name);
        return &balloon;
    }
    void DisableListBox(void*) { events.push_back("disable"); }
    void SelectObject(int id) { events.push_back("select " + std::to_string(id)); }
};
Manager MakeManager()
{
    Manager manager{};
    manager.m_tickerLeft = 10;
    manager.m_tickerTop = 400;
    manager.m_tickerRight = 640;
    manager.m_tickerBottom = 420;
    return manager;
}
} // namespace
OBL_TEST_CASE("ShowMessage replaces the balloon at the right edge of the strip")
{
    Manager manager = MakeManager();
    Env env;
    int old = 0;
    manager.m_balloon = reinterpret_cast<obl::ui::GuiWidget*>(&old);
    ShowMessage(env, &manager, u"hi", 2);
    OBL_CHECK_EQ(env.events[0], "delete");
    OBL_CHECK(env.rect.x0 == 639 && env.rect.x1 == 740 && env.rect.y0 == 400 && env.rect.y1 == 420);
    OBL_CHECK_EQ(env.events[1], "create font=0 color=2756530 flags=2");
    OBL_CHECK_EQ(env.events[2], "sound");
    OBL_CHECK_EQ(manager.m_messageMode, 2);
    OBL_CHECK(!CanShowMessage(&manager));
    env.screenWidth = 640;
    ShowMessage(env, &manager, u"hi", 1);
    OBL_CHECK_EQ(env.events[4], "create font=3 color=2756530 flags=2");
}
OBL_TEST_CASE("The ticker scrolls, stops a short message and wraps a long one")
{
    Manager manager = MakeManager();
    Env env;
    ShowMessage(env, &manager, u"x", 1);
    env.events.clear();
    UpdateTickerAnimation(env, &manager, 0.1f); // -80 px/s * 0.1
    OBL_CHECK_EQ(env.events[0], "move -8,0");
    env.rect.x0 = 14;
    env.rect.x1 = 300;
    env.events.clear();
    UpdateTickerAnimation(env, &manager, 0.0f);
    OBL_CHECK_EQ(env.rect.x0, 10);
    OBL_CHECK(CanShowMessage(&manager)); // snapped to m_left and stopped
    manager.m_balloonActive = 1;
    manager.m_messageMode = 2;
    env.rect = {-300, 400, 5, 420};
    UpdateTickerAnimation(env, &manager, 0.0f);
    OBL_CHECK_EQ(manager.m_messageMode, 1);
    OBL_CHECK_EQ(env.rect.x0, 640);
    env.paused = true;
    env.events.clear();
    UpdateTickerAnimation(env, &manager, 0.1f);
    OBL_CHECK(env.events.empty());
}
OBL_TEST_CASE("DropMessage deletes a scrolling balloon so the next world can show a notice at once")
{
    Manager manager = MakeManager();
    Env env;
    ShowMessage(env, &manager, u"x", 2);
    OBL_CHECK(!CanShowMessage(&manager));
    env.events.clear();
    DropMessage(env, &manager);
    OBL_CHECK((env.events == std::vector<std::string>{"delete"}));
    OBL_CHECK(manager.m_balloon == nullptr);
    OBL_CHECK(CanShowMessage(&manager));
    env.events.clear();
    UpdateTickerAnimation(env, &manager, 0.1f); // nothing left to scroll
    DropMessage(env, &manager); // and nothing to delete
    OBL_CHECK(env.events.empty());
}
OBL_TEST_CASE("The ticker clamps a long frame and truncates like the float-to-int conversion")
{
    Manager manager = MakeManager();
    Env env;
    env.speed = 2.0f;
    ShowMessage(env, &manager, u"x", 2);
    env.events.clear();
    UpdateTickerAnimation(env, &manager, 5.0f);
    OBL_CHECK_EQ(env.events[0], "move -80,0"); // 2 * 0.5 * 80
    OBL_CHECK_EQ(Truncate(-0.9), 0);
    OBL_CHECK_EQ(Truncate(-1.5), -1);
    OBL_CHECK_EQ(Truncate(std::nan("")), INT32_MIN);
}
OBL_TEST_CASE("SetTickerBounds moves an active balloon to the new top")
{
    Manager manager = MakeManager();
    Env env;
    SetTickerBounds(env, &manager, 1, 2, 3, 4);
    OBL_CHECK(env.events.empty());
    OBL_CHECK_EQ(manager.m_tickerBottom, 4);
    ShowMessage(env, &manager, u"x", 1);
    env.events.clear();
    SetTickerBounds(env, &manager, 1, 410, 3, 430);
    OBL_CHECK_EQ(env.events[0], "move 0,408");
}
OBL_TEST_CASE("TickerRebuild lists the history with a button for object rows")
{
    ui::TickerList list{};
    Env env;
    int box = 0;
    list.m_listBox = &box;
    env.history = {{u"one", 5}, {u"two", 0xffffffffu}, {u"three", 0xfffffffeu}};
    TickerRebuild(env, &list);
    OBL_CHECK_EQ(list.m_rows, 3u);
    OBL_CHECK(env.rows[0] == u"Day 3: 13:30 - one");
    OBL_CHECK_EQ(env.events[1], "row 0 5 400 2 0");
    OBL_CHECK_EQ(env.events[2], "button 0 410 UI3_bar_message_goto 5");
    OBL_CHECK_EQ(env.events.size(), 5u);
    env.screenWidth = 640;
    env.events.clear();
    TickerRebuild(env, &list);
    OBL_CHECK_EQ(env.events[1], "row 0 5 314 2 3");
    OBL_CHECK_EQ(env.events[2], "button 0 324 UI2_bar_message_goto 5");
    list.m_listBox = nullptr;
    env.events.clear();
    TickerRebuild(env, &list);
    OBL_CHECK_EQ(list.m_rows, 0u);
    OBL_CHECK(env.events.empty());
}
OBL_TEST_CASE("TickerSetVisible finds the list box of the screen width and TickerUpdate stops at the first pressed row")
{
    ui::TickerList list{};
    Env env;
    env.history = {{u"a", 1}, {u"b", 2}, {u"c", 3}};
    TickerSetVisible(env, &list, 1);
    OBL_CHECK_EQ(env.events[0], "find TickerListBox800");
    OBL_CHECK_EQ(env.events[1], "disable");
    OBL_CHECK_EQ(list.m_rows, 3u);
    env.events.clear();
    env.pressedRow = 1;
    TickerUpdate(env, &list);
    OBL_CHECK_EQ(env.events.back(), "select 77");
    size_t polls = 0;
    for (auto& e : env.events) polls += e.rfind("poll", 0) == 0;
    OBL_CHECK_EQ(polls, 2u);
    list.m_visible = 0;
    env.events.clear();
    TickerUpdate(env, &list);
    OBL_CHECK(env.events.empty());
    env.screenWidth = 640;
    TickerSetVisible(env, &list, 1);
    OBL_CHECK_EQ(env.events[0], "find TickerListBox640");
    env.events.clear();
    TickerSetVisible(env, &list, 0);
    OBL_CHECK(env.events.empty());
    OBL_CHECK_EQ(list.m_visible, 0);
}
