// The description of a setting (src/core/settings_schema.h): the canonical spelling of values, stepping and the engine's own settings
// on it (src/game/game/start_settings.h).

#include "unit_test.h"

#include "core/settings_schema.h"
#include "game/game/start_settings.h"

using namespace obl;
namespace schema = obl::settings_schema;

namespace {

schema::Field Zoom()
{
    schema::Field field;
    field.id = "zoom";
    field.kind = schema::Kind::Number;
    field.min = 1;
    field.max = 3;
    field.step = 0.5;
    field.defaultValue = "1";
    return field;
}

} // namespace

OBL_TEST_CASE("settings_schema.values_have_one_canonical_spelling")
{
    schema::Field toggle;
    OBL_CHECK_EQ(schema::Canonical(toggle, "TRUE").value_or("?"), std::string("1"));
    OBL_CHECK_EQ(schema::Canonical(toggle, "off").value_or("?"), std::string("0"));
    OBL_CHECK(!schema::Canonical(toggle, "2"));

    schema::Field choice;
    choice.kind = schema::Kind::Choice;
    choice.options = {{"a", "A"}, {"b", "B"}};
    choice.defaultValue = "a";
    OBL_CHECK(schema::Canonical(choice, "b"));
    OBL_CHECK(!schema::Canonical(choice, "B")); // choices compare exactly
    OBL_CHECK_EQ(schema::ValueOrDefault(choice, std::string("c")), std::string("a"));
    OBL_CHECK_EQ(schema::ValueOrDefault(choice, std::nullopt), std::string("a"));
    OBL_CHECK_EQ(schema::Display(choice, "b"), std::string("B"));

    const schema::Field zoom = Zoom();
    OBL_CHECK_EQ(schema::Canonical(zoom, "2.50").value_or("?"), std::string("2.5"));
    OBL_CHECK(!schema::Canonical(zoom, "3.5"));
    OBL_CHECK(!schema::Canonical(zoom, "two"));
    OBL_CHECK(!schema::Canonical(zoom, "2x"));
    OBL_CHECK(!schema::IsWhole(zoom));

    OBL_CHECK(schema::IsValidId("a-b_9"));
    OBL_CHECK(!schema::IsValidId("A"));
    OBL_CHECK(!schema::IsValidId(""));
}

OBL_TEST_CASE("settings_schema.stepping_wraps_a_choice_and_stops_a_number")
{
    schema::Field choice;
    choice.kind = schema::Kind::Choice;
    choice.options = {{"1", "1"}, {"2", "2"}, {"0", "none"}};
    choice.zeroLast = true;
    OBL_CHECK_EQ(schema::Step(choice, "0", 1), std::string("1"));
    OBL_CHECK_EQ(schema::Step(choice, "1", -1), std::string("0"));
    OBL_CHECK_EQ(schema::Step(choice, "1", 1), std::string("2"));
    // A value of the file that is not an option joins in numeric order, "0" staying last.
    OBL_CHECK_EQ(schema::Step(choice, "1.5", 1), std::string("2"));
    OBL_CHECK_EQ(schema::Step(choice, "5", 1), std::string("0"));
    OBL_CHECK_EQ(schema::Step(choice, "5", -1), std::string("2"));

    const schema::Field zoom = Zoom();
    OBL_CHECK_EQ(schema::Step(zoom, "1", 1), std::string("1.5"));
    OBL_CHECK_EQ(schema::Step(zoom, "1", -1), std::string("1"));
    OBL_CHECK_EQ(schema::Step(zoom, "3", 1), std::string("3"));

    schema::Field toggle;
    OBL_CHECK_EQ(schema::Step(toggle, "0", 1), std::string("1"));
}

// The guard of docs/build/settings.md: the engine's settings are described once. The defaults the game uses without a file, the
// window's defaults and the commented default file all agree with the schema, so a new setting cannot be added in one place only.
OBL_TEST_CASE("settings_schema.the_engine_settings_agree_with_their_description")
{
    namespace s = game::start_settings;
    const s::Values defaults;
    for (int i = 0; i < s::kSettingCount; ++i) {
        const auto setting = static_cast<s::Setting>(i);
        const schema::Field& field = s::EngineField(setting);
        OBL_CHECK(schema::Canonical(field, field.defaultValue).has_value());
        OBL_CHECK_EQ(s::Get(defaults, setting), field.defaultValue);
        // The default file names the key in its section at the default value.
        const std::string line = field.id + "=" + field.defaultValue + "\r\n";
        OBL_CHECK(std::string(s::kDefaultFile).find(line) != std::string::npos);
        OBL_CHECK(std::string(s::kDefaultFile).find(std::string("[") + s::EngineSection(setting) + "]") != std::string::npos);
        // Stepping from the default reaches a different value.
        OBL_CHECK(schema::Step(field, field.defaultValue, 1) != field.defaultValue);
    }
    s::Values changed = defaults;
    s::Set(changed, s::Scale, "1.75"); // a value of the file the window does not offer stays a value
    OBL_CHECK_EQ(changed.scale, 1.75);
    s::Set(changed, s::MaxFps, "0");
    OBL_CHECK_EQ(changed.maxFps, 0);
}
