#pragma once

// Synthetic old (1.6) saves for the converter tests (convert_save_body_test.cpp, convert_save_writer_test.cpp).

#include "importer/src/readers/save/save_stream.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace obl::test {
using obl::convert::SaveTag;

// A synthetic old save built from tag codes: s string, w wide string, b bool, f float, i int, u uint, y byte, d dword,
// o word (spaces ignored); arrays and classes have their own calls. Values are 0 unless given.
struct Save {
    std::vector<uint8_t> data;
    void Bits(uint32_t bits, size_t width)
    {
        for (size_t i = 0; i < width; ++i) data.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    }
    Save& Scalar(SaveTag tag, uint32_t value)
    {
        data.push_back(static_cast<uint8_t>(tag));
        Bits(value, tag == SaveTag::Bool || tag == SaveTag::Byte ? 1 : tag == SaveTag::Word ? 2 : 4);
        return *this;
    }
    Save& Text(SaveTag tag, std::string_view text)
    {
        data.push_back(static_cast<uint8_t>(tag));
        Bits(static_cast<uint32_t>(text.size()), 4);
        for (char c : text) Bits(static_cast<uint8_t>(c), tag == SaveTag::WideString ? 2 : 1);
        return *this;
    }
    Save& Class(std::string_view name) { return Text(SaveTag::Class, "Class name : " + std::string(name)); }
    Save& Array(SaveTag tag, uint32_t count)
    {
        data.push_back(static_cast<uint8_t>(tag));
        Bits(count, 4);
        for (uint32_t i = 0; i < count; ++i) Bits(0, tag == SaveTag::ByteArray ? 1 : 4);
        return *this;
    }
    Save& Ints(uint32_t count) { return Array(SaveTag::IntArray, count); }
    Save& Floats(uint32_t count) { return Array(SaveTag::FloatArray, count); }
    Save& Codes(std::string_view codes)
    {
        for (char c : codes) {
            switch (c) {
            case 's': Text(SaveTag::String, "x"); break;
            case 'w': Text(SaveTag::WideString, "x"); break;
            case 'b': Scalar(SaveTag::Bool, 0); break;
            case 'f': Scalar(SaveTag::Float, 0); break;
            case 'i': Scalar(SaveTag::Int, 0); break;
            case 'u': Scalar(SaveTag::UInt, 0); break;
            case 'y': Scalar(SaveTag::Byte, 0); break;
            case 'd': Scalar(SaveTag::Dword, 0); break;
            case 'o': Scalar(SaveTag::Word, 0); break;
            default: break;
            }
        }
        return *this;
    }
    Save& Int(uint32_t value) { return Scalar(SaveTag::Int, value); }
    Save& UInt(uint32_t value) { return Scalar(SaveTag::UInt, value); }
    Save& Sprite() { return Codes("f uuuuuu bbb"); }

    void Header()
    {
        Text(SaveTag::Class, "Binary Serial Stream");
        Text(SaveTag::Class, "BeachLife Save");
        Scalar(SaveTag::Float, 0x3fcccccd);
        Codes("iii ff i s b");
        Class("World");
    }
    void World()
    {
        Codes("u")
                .Text(SaveTag::String, "data/Scenario/x/x.biz")
                .Codes("ffifffffifbi")
                .Ints(128)
                .Codes("bb")
                .Ints(5)
                .Codes("iffffi")
                .Ints(32)
                .Codes("bbiiiffuuuu");
        for (int i = 0; i < 6; ++i) Ints(2);
        Codes("fffffffffffffff").Floats(128).Floats(128).Floats(128).Codes("iiii fff iiiiiii");
    }
    // One scenery object with the given id (Prop and Entity after its own fields).
    void Scenery(uint32_t id, std::string_view name = "x")
    {
        Scalar(SaveTag::Byte, 1).Text(SaveTag::String, "BENCH");
        Ints(2).Codes("iiii bubu uuuu uuuu ff bib");
        Codes("yy bbbbbb");
        Codes("bb").Text(SaveTag::String, name).Int(id).Scalar(SaveTag::Byte, 1).Codes("y fff i b uu s").Sprite().Codes("bu");
    }
    // One ferry boat whose first keyword slot is `keyword` (the others "x").
    void Boat(std::string_view keyword)
    {
        Codes("i").Ints(20).Text(SaveTag::String, keyword);
        for (int i = 1; i < 20; ++i) Codes("s");
        Ints(20).Codes("ifii");
    }
    // Mover (not on a path) and Entity of an object of `type`.
    void Moving(uint32_t id, uint8_t type, std::string_view name)
    {
        Codes("fbf ff ff ff f f ff fff i i u i b i u b").UInt(0).Codes("b i b u u b b b b b u u f b f u f");
        Codes("bb").Text(SaveTag::String, name).Int(id).Scalar(SaveTag::Byte, type).Codes("y fff i b uu s").Sprite().Codes("bu");
    }
    // Prop and Entity of an object of `type`.
    void Static(uint32_t id, uint8_t type, std::string_view name)
    {
        Codes("yy bbbbbb bb").Text(SaveTag::String, name).Int(id).Scalar(SaveTag::Byte, type).Codes("y fff i b uu s").Sprite().Codes("bu");
    }
    // An object of a converted class (vehicle 4 `boat` with one sprite, table 5 `cafetable1` with one chair and plate,
    // lifeguard 7 with an attached sprite, mechanic 8, shark 13, performers 14..18 and 20) with the given id.
    void Other(uint8_t type, uint32_t id)
    {
        constexpr std::string_view kStaff = "iii ff ii uu ii b ii ff b i b fff i www bbb";
        Scalar(SaveTag::Byte, type).Text(SaveTag::String, "OBJECT");
        if (type == 4) {
            Ints(300).Codes("i i f i y y u uu");
            for (int i = 0; i < 64; ++i) Codes("fff");
            Codes("uu fff fff bbbb u ff uuu").Sprite().Codes("u bb fff fff ff ff uu b uu");
            Moving(id, type, "boat");
        } else if (type == 5) {
            Codes("b i u u b").Sprite().Codes("b i u i b u i b uu").Sprite().Codes("b");
            Static(id, type, "cafetable1");
        } else if (type >= 14 && type != 19) { // performers (save_body.cpp ReadPerformer)
            constexpr const char* kNames[] = {"cabaret", "singer", "weightlifter", "catwalk", "wettshirt", "", "aerobic"};
            Codes("iui");
            if (type == 18) {
                Codes("b i fff");
            } else {
                Codes("b");
                if (type != 14) Codes("fff");
                if (type == 15 || type == 16) UInt(1).Text(SaveTag::String, "Mic").Sprite();
            }
            Moving(id, type, kNames[type - 14]);
        } else if (type == 8) {
            Codes("i bbbbb f iii i fff iiii i").Codes(kStaff);
            Moving(id, type, "mechanic");
        } else if (type == 7) {
            Codes("bb i i fff s s i b i").UInt(1).Text(SaveTag::String, "Tower Flag").Sprite();
            Codes("ii iii ii b ii i bbbbb").Codes(kStaff);
            Moving(id, type, "lifeguard");
        } else {
            Codes("ii uuuu i i i i");
            Moving(id, type, "shark");
        }
    }
    // A guest `fsunbather` (ids are kGuestIds + i; current_type is building 0, best and worst none).
    static constexpr uint32_t kGuestIds = 1000;
    void Guest(uint32_t id)
    {
        Scalar(SaveTag::Byte, 3).Text(SaveTag::String, "OBJECT");
        Floats(14).Floats(13).Floats(26).Floats(7).Ints(5).Codes("ii").Ints(5).Codes("ii bb fff fff fff i f i f w f ss b ss i");
        UInt(0); // no attached sprite
        Codes("iii ii ii b f i ff i i ff").Ints(2).Array(SaveTag::ByteArray, 2).Codes("i f i i bb i").Int(0xffffffffu);
        Codes("f b").Int(0xffffffffu).Codes("f i").Int(0).Codes("f ffff b f bbbbb").Ints(3).Codes("u ff");
        Moving(id, 3, "fsunbather");
    }
    // A building `hut` (ids are kBuildingIds + i) with one sprite, a show schedule whose slots hold event 0 and one actor,
    // one thought "hi", a queue with one guest and the beer -1 (none).
    static constexpr uint32_t kBuildingIds = 3000;
    void Building(uint32_t id)
    {
        Scalar(SaveTag::Byte, 2).Text(SaveTag::String, "OBJECT");
        Ints(10).Codes("ii").Ints(10).Codes("i").Array(SaveTag::ByteArray, 10).Codes("bb fff fff fff fff fff f iii i i");
        Ints(6).Array(SaveTag::ByteArray, 6).Codes("fff b fff u i f");
        for (int i = 0; i < 5; ++i) Floats(5);
        Codes("f").Int(0xffffffffu).Codes("ffff i").Floats(64).Codes("f iii i u b i f i i i i u i bbbbb u").Ints(10);
        Codes("b uuuu uuuu f uuu b u").Sprite().Codes("bb").UInt(1).Codes("ib");
        for (int slot = 0; slot < 3; ++slot) Codes("uu").UInt(1).Int(id).Codes("u").UInt(0).Codes("fff");
        UInt(1).Int(id).UInt(3).Text(SaveTag::WideString, "hi");
        Scalar(SaveTag::Bool, 1).Codes("ii ff ff").UInt(1).Int(id).UInt(1).Int(0);
        Codes("u").Ints(20).Codes("i f");
        Static(id, 2, "hut");
    }
    // A whole save with `objects` scenery objects (ids 1..n, all named `name`), the repair jobs given by their RepairID and
    // `cells` grid cells, then objects of the `others` types (Other; 2 makes a Building and 3 a Guest in their groups). `managers`
    // fills the ferry, feedback, message and construction managers with one entry each (a construction job with two
    // hoardings and one builder).
    static Save Make(uint32_t objects = 1, std::vector<uint32_t> repairIds = {}, uint32_t cells = 2, std::string_view name = "x",
            bool managers = false, std::vector<uint8_t> others = {})
    {
        const auto guests = static_cast<uint32_t>(std::count(others.begin(), others.end(), 3));
        const auto buildings = static_cast<uint32_t>(std::count(others.begin(), others.end(), 2));
        const auto all = objects + static_cast<uint32_t>(others.size()) - guests - buildings; // the "other" group
        Save s;
        s.Header();
        s.World();
        s.Class("WorldMap").Codes("ui u");
        for (uint32_t i = 0; i < cells; ++i) s.Codes("do");
        s.Class("Path").Codes("u");
        s.Class("ObjectManager").Codes("u").Int(all + guests + buildings).Int(buildings).Int(guests).Int(all);
        s.data.push_back(static_cast<uint8_t>(SaveTag::IntArray)); // the id per slot: the others start at slot 2000
        s.Bits(18500, 4);
        for (uint32_t slot = 0; slot < 18500; ++slot)
            s.Bits(slot >= 2000 && slot < 2000 + all             ? slot - 1999
                            : slot >= 500 && slot < 500 + guests ? kGuestIds + slot - 500
                            : slot < buildings                   ? kBuildingIds + slot
                                                                 : 0,
                    4);
        s.Int(buildings ? kBuildingIds + buildings : guests ? kGuestIds + guests : all + 1).Codes("b");
        for (uint32_t i = 0; i < buildings; ++i) s.Building(kBuildingIds + i);
        for (uint32_t i = 0; i < guests; ++i) s.Guest(kGuestIds + i);
        for (uint32_t i = 1; i <= objects; ++i) s.Scenery(i, name);
        uint32_t id = objects + 1;
        for (uint8_t type : others)
            if (type != 2 && type != 3) s.Other(type, id++);
        s.Class("Player").Codes("uwf");
        if (managers) {
            s.Class("FerryManager").Codes("u").UInt(1);
            s.Boat("Party Animal");
            s.UInt(0).Codes("u").Floats(20).Codes("uu");
        } else {
            s.Class("FerryManager").Codes("uu").UInt(0xffffffffu).Codes("u").Floats(20).Codes("uu");
        }
        s.Codes("iii u ii"); // EventManager
        s.Class("RepairManager").UInt(0x18).UInt(static_cast<uint32_t>(repairIds.size()));
        for (uint32_t repairId : repairIds) s.Int(repairId).Codes("uffbi");
        s.Codes("uu iii");
        if (managers) {
            s.Class("FeedbackManager").Codes("u").UInt(1).Int(4).Codes("bs").UInt(1).Int(5).Codes("if");
            s.Scalar(SaveTag::Bool, 1).Text(SaveTag::WideString, "hi");
            s.Codes("iiiiiiii bb").Ints(100).Codes("f");
            s.Class("MessageHandler").Codes("u").UInt(1).Codes("iiiii ff i");
        } else {
            s.Class("FeedbackManager").Codes("uuu iiiiiiii bb").Ints(100).Codes("f");
            s.Class("MessageHandler").Codes("uu");
        }
        s.Class("ZEngine").Codes("uuuffffu bbffuuuufi");
        s.Class("CleaningManager").Codes("uuuii");
        if (managers) {
            s.Class("ConstructionManager").Codes("u").UInt(1).Int(objects).Codes("i").Int(2).Codes("iiii").Int(0).Int(7).Int(9).Int(0);
            s.Codes("ffffff").Scalar(SaveTag::Bool, 1).Codes("b").Scalar(SaveTag::Bool, 1).Codes("b");
            for (int i = 0; i < 10; ++i) s.Codes("uui");
            s.Codes("i").Int(1).Int(objects).Codes("iu").Int(objects).Codes("b");
        } else {
            s.Class("ConstructionManager").Codes("uuii");
        }
        s.Codes("uuf").Floats(26).Codes("iibifbufiib uuuu fff bbuu dii"); // Weather
        return s;
    }
};

} // namespace obl::test
