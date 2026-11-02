// Command layer (src/game/game/command.h): the text form round-trips every command exactly, the parser rejects drift, and the queue keeps
// submission order (docs/architecture/commands.md).

#include "unit_test.h"

#include "game/game/command.h"
#include "game/game/command_controls.h"
#include "game/game/command_ranges.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>

using namespace obl::command;

OBL_TEST_CASE("command.GUI_ranges_reject_invalid_floats_and_keep_price_units")
{
    for (float value : {-0.01f, 1000.0f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::quiet_NaN()}) {
        OBL_CHECK(!InGuiRange(SetPrice{1, 0, value}));
        OBL_CHECK(!InGuiRange(SetEventPrice{1, 0, value}));
        OBL_CHECK(!InGuiRange(SetBeerPrice{1, value}));
        OBL_CHECK(!InGuiRange(SetBeerStrength{1, value}));
        OBL_CHECK(!InGuiRange(SetSecurityHarshness{1, value}));
        OBL_CHECK(!InGuiRange(SetRepGuestTime{1, value}));
    }
    for (float value : {0.0f, 0.5f, 1.0f}) {
        OBL_CHECK(InGuiRange(SetBeerStrength{1, value}));
        OBL_CHECK(InGuiRange(SetSecurityHarshness{1, value}));
        OBL_CHECK(InGuiRange(SetRepGuestTime{1, value}));
    }
    for (float value : {0.0f, 0.5f, 2.0f, 220.0f, 999.99f}) {
        OBL_CHECK(InGuiRange(SetPrice{1, 0, value}));
        OBL_CHECK(InGuiRange(SetEventPrice{1, 0, value}));
        OBL_CHECK(InGuiRange(SetBeerPrice{1, value}));
    }
    OBL_CHECK(!InGuiRange(SetBeerStrength{1, 1.01f}));
    OBL_CHECK(!InGuiRange(SetSecurityHarshness{1, 1.01f}));
    OBL_CHECK(!InGuiRange(SetRepGuestTime{1, 1.01f}));
}

OBL_TEST_CASE("command.GUI_number_bounds_preserve_minimum_clamps_without_copy_bypass")
{
    for (int value : {-1, 1000}) {
        OBL_CHECK(!InGuiRange(SetStaffWage{1, value}));
        OBL_CHECK(!InGuiRange(CopyStaffWage{1, value}));
        OBL_CHECK(!InGuiRange(SetRepairInterval{1, value}));
    }
    for (int value : {0, 5, 999}) {
        OBL_CHECK(InGuiRange(SetStaffWage{1, value}));
        OBL_CHECK(InGuiRange(SetRepairInterval{1, value}));
    }
    OBL_CHECK(!InGuiRange(CopyStaffWage{1, 0}));
    OBL_CHECK(!InGuiRange(CopyStaffWage{1, 4}));
    OBL_CHECK(InGuiRange(CopyStaffWage{1, 5}));
    OBL_CHECK(InGuiRange(CopyStaffWage{1, 999}));
}

namespace {

// One sample per alternative, so the round-trip test covers every command (a new alternative without a sample fails below).
std::vector<Command> Samples()
{
    return {
            PlaceBuilding{"BeachBar", 40, 52, 2},
            PlaceScenery{"PalmTree", -1, 7, 3},
            PaintSurface{"GRASS1", 3, -4},
            Dozer{-1, 60},
            Demolish{123},
            HireStaff{"Lifeguard"},
            DismissStaff{77},
            SetStaffWage{77, -12},
            CopyStaffWage{78, 3},
            SetStaffShift{77, 22, 6},
            SendStaff{77, 10.5f, -11.25f, 0.0f},
            SendStaffTo{77, 4294967295u},
            SetBuildingOpen{123, true},
            CallRepair{123},
            CallCleaner{124},
            SetPrice{123, 2, 1.1f},
            SetEventPrice{125, 1, 0.3f},
            SetBeer{123, "BEER_LAGER"},
            SetShow{125, 2, "SINGING"},
            ResumeGame{},
            BreakObject{123},
            SetGameSpeed{2},
            SetPaused{false},
            SetRepairInterval{9, 7},
            SetBeerStrength{9, 0.3f},
            SetBeerPrice{9, 0.3f},
            SetBuildingOpenPolicy{9, 7},
            SetBeachOpenPolicy{9, 7},
            SetCleanerToilets{9, 7},
            SetCleanerBins{9, 7},
            SetCleanerLitter{9, 7},
            SetMechanicRepair{9, 7},
            SetMechanicVandals{9, 7},
            SetMechanicWater{9, 7},
            SetRepGuestTime{9, 0.3f},
            SetSecurityHarshness{9, 0.3f},
    };
}

bool SameBits(float a, float b)
{
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

} // namespace

OBL_TEST_CASE("command.object_controls_decode_every_keyword")
{
    const auto samples = Samples();
    for (const auto& sample : samples) {
        std::visit(
                [](const auto& control) {
                    using T = std::decay_t<decltype(control)>;
                    if constexpr (requires { ObjectControlTraits<T>::kKeyword; }) {
                        const auto decoded =
                                MakeObjectControl(control.object, ObjectControlTraits<T>::kKeyword, static_cast<float>(control.value));
                        OBL_REQUIRE(decoded.has_value());
                        OBL_CHECK(std::get<T>(*decoded) == control);
                    }
                },
                sample);
    }
    OBL_CHECK(!MakeObjectControl(1, "NO_SUCH_KEYWORD", 0));
    OBL_CHECK(!MakeObjectControl(1, "STAFF_WAGES", 10));
    OBL_CHECK(!MakeObjectControl(1, "COMMOD_PRICE_0", 1));
    OBL_CHECK(!MakeObjectControl(1, "EVENT0_PRICE", 1));
}

OBL_TEST_CASE("command.object_controls_preserve_word_types")
{
    OBL_CHECK(ObjectControlWord(SetRepairInterval{1, -1}) == 0xffffffffu);
    OBL_CHECK(ObjectControlWord(SetBuildingOpenPolicy{1, 256}) == 256u);
    OBL_CHECK(ObjectControlWord(SetBeachOpenPolicy{1, -1}) == 0xffffffffu);
    OBL_CHECK(ObjectControlWord(SetCleanerBins{1, 255}) == 255u);
    OBL_CHECK(ObjectControlWord(SetCleanerBins{1, 0}) == 0u);
    OBL_CHECK(!ObjectControlWord(SetCleanerBins{1, -1}));
    OBL_CHECK(!ObjectControlWord(SetMechanicRepair{1, 256}));
    const float values[] = {-0.0f, 0.3f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    for (float value : values) {
        const auto decoded = MakeObjectControl(1, "REP_GUEST_TIME", value);
        OBL_REQUIRE(decoded.has_value());
        OBL_CHECK(SameBits(std::get<SetRepGuestTime>(*decoded).value, value));
        OBL_CHECK(ObjectControlWord(std::get<SetRepGuestTime>(*decoded)) == std::bit_cast<uint32_t>(value));
        OBL_CHECK(MakeObjectControl(1, "BUILDING_REPAIRINTERVAL", value).has_value() == (value == 0.0f));
    }
    OBL_CHECK(MakeObjectControl(1, "BUILDING_REPAIRINTERVAL", -2147483648.0f).has_value());
    OBL_CHECK(!MakeObjectControl(1, "BUILDING_REPAIRINTERVAL", 2147483648.0f));
    OBL_CHECK(!MakeObjectControl(1, "BUILDING_REPAIRINTERVAL", 2.5f));
    OBL_CHECK(!MakeObjectControl(1, "CLEANER_BINS", 0.5f));
    OBL_CHECK(!MakeObjectControl(1, "CLEANER_BINS", -1));
    OBL_CHECK(!MakeObjectControl(1, "CLEANER_BINS", 256));
    OBL_CHECK(!Parse("set-repair-interval object=1 value=2.5"));
    OBL_CHECK(!Parse("set-repair-interval object=1 value=2147483648"));
    OBL_CHECK(Parse("set-repair-interval object=1 value=2147483647").has_value());
}

OBL_TEST_CASE("command.samples_cover_every_alternative_once")
{
    std::set<size_t> seen;
    for (const Command& command : Samples()) seen.insert(command.index());
    OBL_CHECK_EQ(seen.size(), std::variant_size_v<Command>);
}

OBL_TEST_CASE("command.names_are_unique")
{
    std::set<std::string_view> names;
    for (const Command& command : Samples()) names.insert(Name(command));
    OBL_CHECK_EQ(names.size(), std::variant_size_v<Command>);
}

OBL_TEST_CASE("command.format_uses_declared_order")
{
    OBL_CHECK_EQ(Format(PlaceBuilding{"BeachBar", 40, 52, 2}), std::string("place-building key=BeachBar x=40 y=52 dir=2"));
    OBL_CHECK_EQ(Format(SetBuildingOpen{9, true}), std::string("set-building-open building=9 open=1"));
    OBL_CHECK_EQ(Format(SetPrice{9, 0, 0.1f}), std::string("set-price building=9 slot=0 price=0.1"));
    OBL_CHECK_EQ(Format(SendStaff{4, 1.5f, 2.0f, 0.0f}), std::string("send-staff staff=4 x=1.5 y=2 z=0"));
}

OBL_TEST_CASE("command.round_trips_every_sample")
{
    for (const Command& command : Samples()) {
        std::string error;
        const auto parsed = Parse(Format(command), &error);
        OBL_REQUIRE(parsed.has_value());
        OBL_CHECK(*parsed == command);
        OBL_CHECK(error.empty());
    }
}

OBL_TEST_CASE("command.fields_in_any_order_and_extra_spaces")
{
    const auto parsed = Parse("  place-building dir=1   y=2 key=Hut\tx=3  ");
    OBL_REQUIRE(parsed.has_value());
    OBL_CHECK((*parsed == Command{PlaceBuilding{"Hut", 3, 2, 1}}));
}

OBL_TEST_CASE("command.parse_rejects_drift")
{
    struct Case {
        const char* text;
        const char* error;
    };
    const Case cases[] = {
            {"", "empty command"},
            {"teleport-guest id=1", "no such command 'teleport-guest'"},
            {"demolish", "demolish: missing field object"},
            {"demolish object=1 extra=2", "demolish: no such field extra"},
            {"demolish object=1 object=2", "duplicate field object"},
            {"demolish object", "expected field=value, got 'object'"},
            {"demolish =1", "expected field=value, got '=1'"},
            {"demolish object=-1", "demolish: bad value '-1' for object"},
            {"demolish object=4294967296", "demolish: bad value '4294967296' for object"},
            {"demolish object=12x", "demolish: bad value '12x' for object"},
            {"set-paused paused=true", "set-paused: bad value 'true' for paused"},
            {"set-game-speed level=", "set-game-speed: bad value '' for level"},
            {"set-game-speed level=+1", "set-game-speed: bad value '+1' for level"},
            {"set-game-speed level=1.5", "set-game-speed: bad value '1.5' for level"},
            {"place-building key=a%2 x=1 y=2 dir=0", "place-building: bad value 'a%2' for key"},
    };
    for (const Case& c : cases) {
        std::string error;
        OBL_CHECK(!Parse(c.text, &error).has_value());
        OBL_CHECK_EQ(error, std::string(c.error));
    }
}

OBL_TEST_CASE("command.floats_round_trip_bit_exactly")
{
    const float values[] = {0.0f, -0.0f, 0.1f, 1.0f / 3.0f, 12345.678f, std::numeric_limits<float>::denorm_min(),
            std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::infinity()};
    for (const float value : values) {
        const auto parsed = ParseFloat(FormatFloat(value));
        OBL_REQUIRE(parsed.has_value());
        OBL_CHECK(SameBits(*parsed, value));
    }
    OBL_CHECK_EQ(FormatFloat(-0.0f), std::string("-0"));
    const auto nan = ParseFloat(FormatFloat(std::numeric_limits<float>::quiet_NaN()));
    OBL_REQUIRE(nan.has_value());
    OBL_CHECK(std::isnan(*nan));
}

OBL_TEST_CASE("command.strings_escape_separators_and_non_ascii")
{
    OBL_CHECK_EQ(EncodeString(""), std::string(""));
    OBL_CHECK_EQ(EncodeString("a b=c%d"), std::string("a%20b%3Dc%25d"));
    OBL_CHECK_EQ(EncodeString("\xE9\n"), std::string("%E9%0A"));
    const std::string all = [] {
        std::string s;
        for (int c = 0; c < 256; ++c) s += static_cast<char>(c);
        return s;
    }();
    const auto decoded = DecodeString(EncodeString(all));
    OBL_REQUIRE(decoded.has_value());
    OBL_CHECK(*decoded == all);
    OBL_CHECK(DecodeString("%e9").value_or("") == "\xE9"); // lower-case hex is read too
    OBL_CHECK(!DecodeString("%").has_value());
    OBL_CHECK(!DecodeString("%4").has_value());
    OBL_CHECK(!DecodeString("%zz").has_value());
    OBL_CHECK(!DecodeString("a=b").has_value()); // '=' only escaped, so a field value has one spelling
    // An empty string is a present, empty field.
    const auto parsed = Parse("place-building key= x=1 y=2 dir=0");
    OBL_REQUIRE(parsed.has_value());
    OBL_CHECK(std::get<PlaceBuilding>(*parsed).key.empty());
}

OBL_TEST_CASE("command.queue_keeps_submission_order")
{
    CommandQueue queue;
    OBL_CHECK(queue.Empty());
    queue.Submit(SetPaused{true});
    queue.Submit(Demolish{5});
    queue.Submit(SetPaused{false});
    const auto first = queue.TakePending();
    OBL_REQUIRE(first.size() == 3);
    OBL_CHECK(first[0] == Command{SetPaused{true}});
    OBL_CHECK(first[1] == Command{Demolish{5}});
    OBL_CHECK(first[2] == Command{SetPaused{false}});
    OBL_CHECK(queue.Empty());
    OBL_CHECK(queue.TakePending().empty());
    queue.Submit(HireStaff{"Rep"});
    queue.Clear();
    OBL_CHECK(queue.TakePending().empty());
}
