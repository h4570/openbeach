#pragma once

#include "game/feedback/feedback_state.h"
#include "game/ferry/ferry_lifecycle.h"
#include "game/world/save_json_values.h"
#include "game/world/world_load_json.h"
#include "formats/save_states.h"
#include "core/code_page.h"

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>

// The reader of a manager section of obl.save (docs/architecture/data-formats/save-switch.md): the inverse of
// TableWriter (save_sections_json.cpp) over the module's VisitFields, whose record names are the keys, so a module's LoadFields fills its
// state from the document as the binary block load does from the stream. The key forms are the writer's: a list record is the
// array at its key, "a.b" nests objects, "list[]" is one value per occurrence of the name, "list[].field" one element per
// occurrence; an Int/UInt whose key holds an array reads the array's size (the builder count). A missing key reads as the
// bridge's default (0, false, empty text and lists; the ferry cursor kNoCursor), a value of the wrong kind fails the section.
//
// `Derived` is the reader itself (CRTP), so the list callbacks get it and a derived reader can replace the game-side steps:
// Define(job) (construction: make the job from its building's definition) and CreateLightningBolt(weather) (weather: the bitmap
// a saved bolt needs). The base does nothing for either, which is what a check on scratch state needs.
namespace obl::world_save {

template<class Derived> class TableReader {
public:
    TableReader(const char* section, const ReadContext* context, const nlohmann::json& in, std::string& error)
        : m_section(section), m_context(context), m_scope(&in), m_error(error)
    {
        m_error.clear();
        if (!in.is_object()) Fail("section is not an object");
    }
    // The reader keeps a reference to the section.
    TableReader(const char*, const ReadContext*, const nlohmann::json&&, std::string&) = delete;

    bool Ok() const { return m_error.empty(); }

    template<class Seq, class Apply> void List(const char* name, Seq& list, Apply apply)
    {
        Elements(name, At(name), [&](const nlohmann::json&) {
            list.emplace_back();
            apply(Self(), list.back());
        });
    }
    // Save-file integers are 32-bit words; the assert guards that on-disk width.
    template<class T> void Int(const char* name, T& field)
    {
        static_assert(sizeof(T) == 4);
        field = static_cast<T>(Integer(name));
    }
    template<class T> void UInt(const char* name, T& field)
    {
        static_assert(sizeof(T) == 4);
        field = static_cast<T>(Integer(name));
    }
    // A raw byte, as the bridge's ReadBool keeps it.
    void Bool(const char* name, uint8_t& field) { field = static_cast<uint8_t>(Integer(name)); }
    void Float(const char* name, float& field) { field = FloatValue(name, At(name)); }
    void Scratch(const char* name, float& field) { Float(name, field); }
    void Payload(const char* name, int32_t& field) { Int(name, field); }
    void Payload(const char* name, float& field) { Float(name, field); }
    template<size_t N> void Ints(const char* name, std::array<uint32_t, N>& values) { Ints(name, values.data(), static_cast<int>(N)); }
    template<class T> void Ints(const char* name, T* values, int count)
    {
        static_assert(sizeof(T) == 4);
        Values(name, count, [&](int i, const nlohmann::json* v) { values[i] = static_cast<T>(v ? IntegerOf(name, *v) : 0); });
    }
    void Floats(const char* name, float* values, int count)
    {
        Values(name, count, [&](int i, const nlohmann::json* v) { values[i] = FloatValue(name, v); });
    }
    // A feedback text: `wide`, then the text read like LoadText (a 512-unit wide buffer, a 128-byte key buffer).
    void Text(feedback::Text& text)
    {
        text = {};
        text.wide = (Integer("wide") & 0xff) != 0;
        const char* label = "text";
        const std::string utf8 = String(label, At(label));
        if (text.wide) {
            std::u16string wide = code_page::Utf8ToUtf16(utf8);
            if (wide.size() >= 512) return Fail(std::string(label) + ": text of " + std::to_string(wide.size()) + " units");
            text.wideText = wide.substr(0, wide.find(u'\0'));
        } else {
            std::string narrow;
            if (!Narrow(label, utf8, narrow)) return;
            if (narrow.size() >= 128) return Fail(std::string(label) + ": text of " + std::to_string(narrow.size()) + " bytes");
            text.key = narrow.substr(0, narrow.find('\0'));
        }
    }

    // Construction: the jobs (owned pointers), their hoardings, the first `count` builders (the count is the builders' array size).
    template<class Seq, class Apply> void Jobs(const char* name, Seq& jobs, Apply apply)
    {
        Elements(name, At(name), [&](const nlohmann::json&) {
            auto& job = *jobs.emplace_back(std::make_unique<typename Seq::value_type::element_type>());
            apply(Self(), job);
        });
    }
    template<class Job> void Define(Job&) {}
    template<class Seq, class Apply> void Hoardings(const char* name, Seq& hoardings, Apply apply) { List(name, hoardings, apply); }
    template<class Table, class Apply> void Builders(int32_t count, Table& builders, Apply apply)
    {
        if (static_cast<uint32_t>(count) > builders.size())
            return Fail("builders: " + std::to_string(static_cast<uint32_t>(count)) + " over " + std::to_string(builders.size()));
        const auto it = m_scope->find("builders"); // the count's record is this array's size
        if (it == m_scope->end()) return;
        size_t i = 0;
        Elements("builders", &*it, [&](const nlohmann::json&) { apply(Self(), builders[i++]); });
    }

    // Ferry: the boat lists, the schedule cursor from its index (kNoCursor when missing), the keywords in the pack's code page.
    template<class Seq> void Boats(const char* name, Seq& boats)
    {
        Elements(name, At(name), [&](const nlohmann::json&) {
            boats.emplace_back();
            ferry::VisitBoat(boats.back(), Self());
        });
    }
    template<class M> void Cursor(const char* name, M& m)
    {
        const nlohmann::json* v = At(name);
        ferry::SetCursor(m, v ? IntegerOf(name, *v) : ferry::kNoCursor);
    }
    template<size_t N> void Keyword(const char* name, char (&text)[N])
    {
        std::string narrow;
        if (!Narrow(name, String(name, At(name)), narrow)) return;
        if (narrow.size() >= N) return Fail(std::string(name) + ": text of " + std::to_string(narrow.size()) + " bytes");
        std::memcpy(text, narrow.data(), narrow.size());
        text[narrow.size()] = 0;
    }

    // Events: the running events' schedule indices, one per element of the count's list.
    template<class Seq> void Active(const char* countName, const char* idName, Seq& ids)
    {
        Elements(countName, At(countName),
                [&](const nlohmann::json& element) { ids.push_back(static_cast<int>(IntegerOf(idName, element))); });
    }

    // Weather: the strike area's unsigned records, the cached tint, the lightning bitmap made again when the save had one.
    void Area(const char* name, int& value) { value = static_cast<int>(Integer(name)); }
    void Dword(const char* name, uint32_t& value) { value = Integer(name); }
    template<class W> void Bitmap(const char* name, W& weather)
    {
        if ((Integer(name) & 0xff) != 0) Self().CreateLightningBolt(weather);
    }
    template<class W> void CreateLightningBolt(W&) {}

protected:
    void Fail(const std::string& message)
    {
        if (m_error.empty()) m_error = std::string(m_section) + "." + message;
    }

private:
    Derived& Self() { return static_cast<Derived&>(*this); }

    // Calls `each` per element of the array at `list` (null: none), each element an object scope.
    template<class Apply> void Elements(const char* name, const nlohmann::json* list, Apply each)
    {
        if (!list) return;
        if (!list->is_array()) return Fail(std::string(name) + ": not a list");
        const nlohmann::json* parentScope = m_scope;
        auto parentSeen = std::move(m_seen);
        for (const nlohmann::json& element : *list) {
            if (!Ok()) break;
            m_scope = &element;
            m_seen.clear();
            each(element);
        }
        m_scope = parentScope;
        m_seen = std::move(parentSeen);
    }
    // The value of a record (its name is its key), or null for a missing one; a "[]" key reads the element of its occurrence.
    const nlohmann::json* At(const char* name)
    {
        const uint32_t occurrence = m_seen[name]++;
        return Place(name, std::string_view(name).find("[]") != std::string_view::npos ? occurrence : 0);
    }
    const nlohmann::json* Place(std::string_view key, uint32_t index) const
    {
        const nlohmann::json* target = m_scope;
        const auto child = [&](std::string_view part) -> const nlohmann::json* {
            if (!target || !target->is_object()) return nullptr;
            const auto it = target->find(std::string(part));
            return it == target->end() ? nullptr : &*it;
        };
        for (size_t dot = key.find('.'); dot < key.find("[]"); dot = key.find('.')) { // the objects before the first list
            target = child(key.substr(0, dot));
            key.remove_prefix(dot + 1);
        }
        const size_t at = key.find("[]");
        if (at == std::string_view::npos) return child(key);
        target = child(key.substr(0, at));
        if (!target || !target->is_array() || index >= target->size()) return nullptr;
        target = &(*target)[index];
        return key.size() == at + 2 ? target : child(key.substr(at + 3));
    }
    uint32_t IntegerOf(const char* name, const nlohmann::json& value)
    {
        const auto bits = IntegerBits(value);
        if (!bits) Fail(std::string(name) + ": not an integer");
        return bits.value_or(0);
    }
    // An integer record; a list at its key gives the list's size.
    uint32_t Integer(const char* name)
    {
        const nlohmann::json* v = At(name);
        if (!v) return 0;
        if (v->is_string()) {
            const auto number = formats::SaveStateNumber(formats::SaveStateSetOf(std::string(m_section) + "." + name), *v);
            if (!number) Fail(std::string(name) + ": no state " + v->get<std::string>());
            return static_cast<uint32_t>(number.value_or(0));
        }
        return v->is_array() ? static_cast<uint32_t>(v->size()) : IntegerOf(name, *v);
    }
    float FloatValue(const char* name, const nlohmann::json* v)
    {
        if (!v) return 0.0f;
        const auto bits = FloatBits(*v);
        if (!bits) Fail(std::string(name) + ": not a float");
        return std::bit_cast<float>(bits.value_or(0));
    }
    // A record of `count` values: a list of that length, or zeros when missing.
    template<class Apply> void Values(const char* name, int count, Apply set)
    {
        const nlohmann::json* v = At(name);
        if (v && (!v->is_array() || v->size() != static_cast<size_t>(count)))
            return Fail(std::string(name) + ": not a list of " + std::to_string(count));
        for (int i = 0; i < count; ++i) set(i, v ? &(*v)[static_cast<size_t>(i)] : nullptr);
    }
    std::string String(const char* name, const nlohmann::json* v)
    {
        if (!v) return {};
        if (!v->is_string()) {
            Fail(std::string(name) + ": not a text");
            return {};
        }
        return v->get<std::string>();
    }
    bool Narrow(const char* name, const std::string& utf8, std::string& narrow)
    {
        if (m_context && m_context->utf8ToNarrow(utf8, narrow)) return true;
        Fail(std::string(name) + ": text outside the pack's code page");
        return false;
    }

    const char* m_section;
    const ReadContext* m_context;
    const nlohmann::json* m_scope;
    std::map<std::string, uint32_t> m_seen; // occurrences of each "[]" name in the current scope
    std::string& m_error;
};

// The reader without game-side steps (no job definitions, no lightning bitmap).
class PlainTableReader : public TableReader<PlainTableReader> {
public:
    using TableReader::TableReader;
};

} // namespace obl::world_save
