#pragma once

#include "core/char16.h"
#include "game/objects/object_guest.h"
#include "game/world/save_objects_load.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

// A guest's `objects` entry read into a constructed Guest (save-switch.md): the guest's records over the field
// table (object_guest.h VisitFields), then the human layer's load, which is the
// moving layer and the base. The scalar records go to the shared object reader; the guest-only kinds (Floats, Bytes, the heap name,
// the animation texts, the attached sprite, the catalog building types) are read here.
namespace obl::world_save::guest_load {

// The value at a guest key ("guest.x" -> in.guest.x), null when missing.
const nlohmann::json* Find(const nlohmann::json& in, std::string_view key);
// Whether the key is one of the three building types written as catalog building keys.
bool IsBuildingType(std::string_view key);

// Each reads the value `v` (null or missing: the bridge's default) into the field; false with `why` on a value the field cannot hold.
// A catalog building key back to its position, null -1, missing 0.
bool BuildingType(const nlohmann::json* v, const ReadContext& context, int32_t& id, std::string& why);
bool Floats(const nlohmann::json* v, float* values, int count, std::string& why);
bool Bytes(const nlohmann::json* v, uint8_t* values, int count, std::string& why);
// Narrow text through the pack's code page into a `capacity` buffer with its terminator (ReadString).
bool Text(const nlohmann::json* v, const ReadContext& context, char* text, int capacity, std::string& why);
std::string Text(const nlohmann::json* v, const ReadContext& context, int capacity, std::string& why);
// The heap name of the guest's Name field: the previous one goes back to the heap, a heap copy of at most 127 characters.
bool HeapName(const nlohmann::json* v, Char16*& name, std::string& why);

// The visitor VisitFields(Guest) calls: the guest-only record kinds here, the rest forwarded to the object reader.
template<class Reader> class GuestFields {
public:
    GuestFields(Reader& reader, const nlohmann::json& in, const ReadContext& context, const ObjectLoadCallbacks& callbacks,
            const std::string& where, std::string& error)
        : m_reader(reader), m_in(in), m_context(context), m_callbacks(callbacks), m_where(where), m_error(error)
    {}

    void Int(const char* key, int32_t& field, object::Hashed hashed)
    {
        if (!IsBuildingType(key)) return m_reader.Int(key, field, hashed);
        std::string why;
        if (!BuildingType(Find(m_in, key), m_context, field, why)) Fail(key, why);
    }
    void UInt(const char* key, uint32_t& field, object::Hashed hashed) { m_reader.UInt(key, field, hashed); }
    void Float(const char* key, float& field, object::Hashed hashed) { m_reader.Float(key, field, hashed); }
    void Bool(const char* key, uint8_t& field, object::Hashed hashed) { m_reader.Bool(key, field, hashed); }
    void Ints(const char* key, int32_t* values, int count, object::Hashed hashed) { m_reader.Ints(key, values, count, hashed); }
    void Floats(const char* key, float* values, int count, object::Hashed)
    {
        Check(key, guest_load::Floats(Find(m_in, key), values, count, m_why));
    }
    void Bytes(const char* key, uint8_t* values, int count, object::Hashed)
    {
        Check(key, guest_load::Bytes(Find(m_in, key), values, count, m_why));
    }
    void Text(const char* key, char* text, int capacity, object::Hashed)
    {
        Check(key, guest_load::Text(Find(m_in, key), m_context, text, capacity, m_why));
    }
    void Name(const char* key, Char16*& name, object::Hashed) { Check(key, HeapName(Find(m_in, key), name, m_why)); }
    // Absent keeps the constructor's pointer; present, a new sprite by name, then its records (the guest's attached sprite).
    void Attached(const char* key, void*& sprite, object::Hashed)
    {
        const nlohmann::json* v = Find(m_in, key);
        if (!v || v->is_null()) return;
        if (!v->is_object()) return Fail(key, "not an object");
        const auto name = v->find("sprite");
        std::string why;
        const std::string text = guest_load::Text(name == v->end() ? nullptr : &*name, m_context, 128, why);
        if (!why.empty()) return Fail(key, why);
        void* created = m_callbacks.createSprite(text.c_str());
        if (!created) return Fail(key, "no sprite " + text);
        sprite = created;
        m_reader.Sprite(key, sprite);
    }

private:
    void Check(const char* key, bool read)
    {
        if (!read) Fail(key, m_why);
    }
    void Fail(const char* key, const std::string& why)
    {
        if (m_error.empty()) m_error = m_where + "." + key + ": " + why;
    }

    Reader& m_reader;
    const nlohmann::json& m_in;
    const ReadContext& m_context;
    const ObjectLoadCallbacks& m_callbacks;
    const std::string& m_where;
    std::string& m_error;
    std::string m_why;
};

// The guest load from the entry: the guest's records, then (Person saving nothing of its own) the moving layer and the base.
template<class Reader>
void ReadGuest(object::Guest& guest, Reader& reader, const nlohmann::json& in, const ReadContext& context,
        const ObjectLoadCallbacks& callbacks, const std::string& where, std::string& error)
{
    GuestFields<Reader> fields(reader, in, context, callbacks, where, error);
    object::VisitFields(guest, fields);
    if (error.empty()) reader.Base(static_cast<object::Mover&>(guest));
}

} // namespace obl::world_save::guest_load
