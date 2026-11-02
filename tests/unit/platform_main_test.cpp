#include "core/platform_main.h"
#include "unit_test.h"

#include <bit>
#include <cstring>
#include <vector>

using namespace obl::platform_main;

OBL_TEST_CASE("entry skips the program name like the MSVC 6 WinMainCRTStartup")
{
    OBL_CHECK(std::strcmp(SkipProgramName("\"C:\\Beach Life\\BeachLife.exe\" -a b"), "-a b") == 0);
    OBL_CHECK(std::strcmp(SkipProgramName("BeachLife.exe  \t-x"), "-x") == 0);
    OBL_CHECK(std::strcmp(SkipProgramName("BeachLife.exe"), "") == 0);
    OBL_CHECK(std::strcmp(SkipProgramName("\"unterminated name"), "") == 0);
    OBL_CHECK(std::strcmp(SkipProgramName("\"a\"tail"), "tail") == 0); // no blank needed after the closing quote
    OBL_CHECK(std::strcmp(SkipProgramName("\"\"   "), "") == 0);
    // Bytes >= 0x80 belong to the name (unsigned compare), control bytes end it.
    OBL_CHECK(std::strcmp(SkipProgramName("pl\xb3\x61\x01x"), "x") == 0);
}

OBL_TEST_CASE("entry passes wShowWindow only with STARTF_USESHOWWINDOW")
{
    OBL_CHECK_EQ(ShowCommand(0, 3), 10);
    OBL_CHECK_EQ(ShowCommand(1, 0), 0);
    OBL_CHECK_EQ(ShowCommand(0x81, 7), 7);
}

OBL_TEST_CASE("WinMain show command on a window SDL created")
{
    OBL_CHECK(SdlShowAction(0) == ShowAction::Hide);
    for (int command : {2, 6, 7, 11}) OBL_CHECK(SdlShowAction(command) == ShowAction::ShowMinimized);
    for (int command : {1, 3, 4, 5, 8, 9, 10}) OBL_CHECK(SdlShowAction(command) == ShowAction::Show);
}

OBL_TEST_CASE("WinMain main window geometry for full screen and windowed")
{
    const MainWindowGeometry full = MainWindow(false, 4, 4, 23);
    OBL_CHECK_EQ(full.exStyle, 8u);
    OBL_CHECK_EQ(full.style, 0x80000000u);
    OBL_CHECK_EQ(full.x, 0);
    OBL_CHECK_EQ(full.width, 800);
    OBL_CHECK_EQ(full.height, 600);
    const MainWindowGeometry windowed = MainWindow(true, 4, 5, 23);
    OBL_CHECK_EQ(windowed.exStyle, 0u);
    OBL_CHECK_EQ(windowed.style, 0u);
    OBL_CHECK_EQ(windowed.x, static_cast<int>(0x80000000u));
    OBL_CHECK_EQ(windowed.y, static_cast<int>(0x80000000u));
    OBL_CHECK_EQ(windowed.width, 808);
    OBL_CHECK_EQ(windowed.height, 633);
}

OBL_TEST_CASE("the static sine table holds sin(i/32 degrees) as floats within one ulp of the CRT sine")
{
    std::vector<float> table(kFastSinTableSize);
    BuildFastSinTable(table.data());
    OBL_CHECK_EQ(table[0], 0.0f);
    OBL_CHECK_EQ(table[90 * 32], 1.0f);
    OBL_CHECK_EQ(table[270 * 32], -1.0f);
    int far = 0;
    for (int i = 0; i < kFastSinTableSize; ++i) {
        const float crt = static_cast<float>(std::sin(i * 0.03125 * static_cast<double>(0.017453292f)));
        const int32_t a = std::bit_cast<int32_t>(table[i]), b = std::bit_cast<int32_t>(crt);
        if (a - b > 1 || b - a > 1) ++far;
    }
    OBL_CHECK_EQ(far, 0);
}
