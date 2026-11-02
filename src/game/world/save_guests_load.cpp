#include "core/char16.h"
#include "game/world/save_guests_load.h"

#include "core/code_page.h"
#include "game/world/save_json_values.h"

#include <bit>
#include <cstring>
#include <new>

namespace obl::world_save::guest_load {

using nlohmann::json;

constexpr size_t kNameCapacity = 128; // the guest load's wide name buffer with its terminator

const json* Find(const json& in, std::string_view key)
{
    const size_t dot = key.find('.');
    const auto section = in.find(std::string(key.substr(0, dot)));
    if (section == in.end() || dot == std::string_view::npos) return section == in.end() ? nullptr : &*section;
    if (!section->is_object()) return nullptr;
    const auto it = section->find(std::string(key.substr(dot + 1)));
    return it == section->end() ? nullptr : &*it;
}

bool IsBuildingType(std::string_view key)
{
    return key == "guest.best_type" || key == "guest.worst_type" || key == "guest.current_type";
}

bool BuildingType(const json* v, const ReadContext& context, int32_t& id, std::string& why)
{
    if (!v) {
        id = 0;
        return true;
    }
    if (v->is_null()) {
        id = -1;
        return true;
    }
    if (!v->is_string()) {
        why = "not a catalog building key";
        return false;
    }
    const std::string& key = v->get_ref<const std::string&>();
    id = context.buildingId ? context.buildingId(key) : -1;
    if (id >= 0) return true;
    why = "no catalog building " + key;
    return false;
}

namespace {
// A fixed-size list, element by element (null or missing: zeros).
template<class Element> bool List(const json* v, int count, std::string& why, Element element)
{
    if (v && !v->is_null() && (!v->is_array() || v->size() != static_cast<size_t>(count))) {
        why = "not a list of " + std::to_string(count);
        return false;
    }
    for (int i = 0; i < count; ++i)
        if (!element(i, v && v->is_array() ? &(*v)[static_cast<size_t>(i)] : nullptr)) return false;
    return true;
}
} // namespace

bool Floats(const json* v, float* values, int count, std::string& why)
{
    return List(v, count, why, [&](int i, const json* item) {
        const auto bits = item && !item->is_null() ? FloatBits(*item) : std::optional<uint32_t>(0);
        if (!bits) why = "not a float";
        values[i] = std::bit_cast<float>(bits.value_or(0));
        return bits.has_value();
    });
}

bool Bytes(const json* v, uint8_t* values, int count, std::string& why)
{
    return List(v, count, why, [&](int i, const json* item) {
        const auto bits = item && !item->is_null() ? IntegerBits(*item) : std::optional<uint32_t>(0);
        if (!bits || *bits > 0xff) {
            why = "not a byte";
            return false;
        }
        values[i] = static_cast<uint8_t>(*bits);
        return true;
    });
}

std::string Text(const json* v, const ReadContext& context, int capacity, std::string& why)
{
    std::string narrow;
    if (!v || v->is_null()) return narrow;
    if (!v->is_string() || !context.utf8ToNarrow(v->get_ref<const std::string&>(), narrow)) {
        why = "text outside the pack's code page";
        return {};
    }
    if (narrow.size() >= static_cast<size_t>(capacity)) {
        why = "text of " + std::to_string(narrow.size()) + " bytes";
        return {};
    }
    return narrow;
}

bool Text(const json* v, const ReadContext& context, char* text, int capacity, std::string& why)
{
    const std::string narrow = Text(v, context, capacity, why);
    if (!why.empty()) return false;
    std::memcpy(text, narrow.data(), narrow.size());
    text[narrow.size()] = 0;
    return true;
}

bool HeapName(const json* v, Char16*& name, std::string& why)
{
    std::u16string wide;
    if (v && !v->is_null()) {
        if (!v->is_string()) {
            why = "not a text";
            return false;
        }
        wide = code_page::Utf8ToUtf16(v->get_ref<const std::string&>());
    }
    if (wide.size() >= kNameCapacity) {
        why = "name of " + std::to_string(wide.size()) + " characters";
        return false;
    }
    ::operator delete(name);
    name = static_cast<Char16*>(::operator new((wide.size() + 1) * sizeof(Char16)));
    for (size_t i = 0; i < wide.size(); ++i) name[i] = static_cast<Char16>(wide[i]);
    name[wide.size()] = 0;
    return true;
}

} // namespace obl::world_save::guest_load
