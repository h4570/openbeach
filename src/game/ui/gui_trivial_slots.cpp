#include "game/ui/gui_trivial_slots.h"

#include "core/call_counter.h"

#include <array>
#include <cstdint>

namespace obl::gui {
void EmptyMethod(void*) {}
void EmptyDestructor(void*) {}
void EmptyCallFunction(void*, int) {}

namespace {
void DriveCheck(int rounds);
// `call-drive ui.trivial-slots <rounds>`: the empty methods leave every byte alone.
test::CallCounter g_check("ui.trivial-slots", &DriveCheck);

void DriveCheck(int rounds)
{
    const auto check = [](bool ok, const char* detail) { g_check.Record(ok, detail); };
    for (int i = 0; i < rounds * 128; ++i) {
        std::array<uint8_t, 64> a, b;
        for (size_t j = 0; j < a.size(); ++j) a[j] = static_cast<uint8_t>(i * 37 + j * 13);
        b = a;
        for (auto* empty : {&EmptyMethod, &EmptyDestructor}) {
            empty(b.data());
            check(a == b, "an empty method preserves all object bytes");
        }
    }
}
} // namespace
} // namespace obl::gui
