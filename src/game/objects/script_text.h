#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

// Text values of the script interpreters. The 32-bit `bits` of a ScriptValue cannot hold a 64-bit pointer, so a text value carries an id handed out here on first sight of the address. The table only grows: the texts are script
// literals, animation names and static buffers, a small fixed set, and an address seen again gets its old id, so a reused buffer reads as
// whatever it holds now, as a pointer would. Ids are tagged so a plain number taken as a text (the setters read the bits as a name whatever
// the type says) misses the table and reads as no text instead of a stray pointer. Nothing deterministic sees the numbers: the addresses are
// not stable across runs either.
namespace obl::script::text {

constexpr uint32_t kIdTag = 0x7e000000u, kIdTagMask = 0xff000000u;

namespace detail {
struct Table {
    std::vector<const char*> texts;
    std::unordered_map<const char*, int32_t> ids;
};
inline Table& Get()
{
    static Table table;
    return table;
}
} // namespace detail

// The id of a text; a null text is 0.
inline int32_t Id(const char* text)
{
    if (text == nullptr) return 0;
    auto& table = detail::Get();
    const auto [it, added] = table.ids.try_emplace(text, 0);
    if (added) {
        table.texts.push_back(text);
        it->second = static_cast<int32_t>(kIdTag | static_cast<uint32_t>(table.texts.size()));
    }
    return it->second;
}

// The text of an id; 0 and anything that is not an id of this table are null.
inline const char* Of(int32_t id)
{
    const uint32_t bits = static_cast<uint32_t>(id);
    if ((bits & kIdTagMask) != kIdTag) return nullptr;
    const auto& texts = detail::Get().texts;
    const uint32_t index = bits & ~kIdTagMask;
    return index >= 1 && index <= texts.size() ? texts[index - 1] : nullptr;
}

} // namespace obl::script::text
