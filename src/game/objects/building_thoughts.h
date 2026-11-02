#pragma once

#include "core/text16.h"
#include "game/objects/object_building.h"

#include <cstddef>
#include <cstdint>
#include <cwchar>

// AddBuildingThought and RemoveBuildingThoughts on the thought list m_ThoughtList (a std::deque of ThoughtEntry*, the newest
// at the front; building thoughts, docs/architecture/objects.md). Entries and their texts are blocks of `operator new`, owned by the list.
namespace obl {

namespace thought_ops {

using object::ThoughtList;

inline constexpr size_t kMaxThoughts = 8; // a new thought makes room first when the list already holds this many

// One entry with its text back to the heap (a null entry or text is skipped).
inline void DeleteEntry(object::ThoughtEntry* entry)
{
    if (!entry) return;
    ::operator delete(entry->m_text);
    ::operator delete(entry);
}

// Every entry deleted; the list is left empty.
inline void Clear(ThoughtList& list)
{
    for (object::ThoughtEntry* entry : list) DeleteEntry(entry);
    list.clear();
}

// Thought `index` from the front, or null past either end (an index out of range is tolerated).
inline const object::ThoughtEntry* At(const ThoughtList& list, int64_t index)
{
    if (index < 0 || static_cast<uint64_t>(index) >= list.size()) return nullptr;
    return list[static_cast<size_t>(index)];
}

} // namespace thought_ops

// AddThought: a building remembers the last eight thoughts. At eight the oldest (the back) is dropped first; the new thought is an
// entry {text copy, guest id} pushed to the front.
inline void AddBuildingThought(object::ThoughtList& list, const Char16* text, int32_t guestId)
{
    if (list.size() >= thought_ops::kMaxThoughts) {
        thought_ops::DeleteEntry(list.back());
        list.pop_back();
    }
    auto* entry = static_cast<object::ThoughtEntry*>(::operator new(sizeof(object::ThoughtEntry)));
    entry->m_objectId = guestId;
    const size_t length = text16::Length(text);
    entry->m_text = static_cast<Char16*>(::operator new((length + 1) * sizeof(Char16)));
    text16::Copy(entry->m_text, text);
    list.push_front(entry);
}

// RemoveBuildingThoughts: every thought of one object leaves; the order of the others is kept. A null entry in the list is
// skipped.
inline void RemoveBuildingThoughts(object::ThoughtList& list, int32_t objectId)
{
    for (auto it = list.begin(); it != list.end();) {
        if (*it && (*it)->m_objectId == objectId) {
            thought_ops::DeleteEntry(*it);
            it = list.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace obl
