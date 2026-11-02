#include "formats/gui_content.h"
#include "gui_content_schemas.h"
#include "core/json_schema.h"
#include "core/pack_path.h"

#include <fstream>

namespace obl::gui_content {
namespace {
constexpr std::string_view kFilters[] = {"", "guest_happiness", "guest_cash", "guest_male", "guest_female", "guest_sunburn",
        "guest_headache", "guest_drunk", "guest_bladder", "guest_illness", "guest_price", "guest_safety", "guest_crowding", "guest_beauty",
        "guest_cleanliness", "guest_attractiveness", "guest_variety", "building_all", "building_repair", "building_income", "building_cost",
        "building_profit", "building_average_profit", "building_power", "building_open", "building_closed", "building_age",
        "building_visits", "building_faults"};
constexpr const char* kLists[] = {"guest", "building", "cell", "staff", "hire", "event"};
} // namespace

std::string_view FilterName(int id)
{
    return id > 0 && id < static_cast<int>(std::size(kFilters)) ? kFilters[id] : "";
}
int FilterId(std::string_view name)
{
    for (int id = 1; id < static_cast<int>(std::size(kFilters)); ++id)
        if (kFilters[id] == name) return id;
    return -1;
}

bool Parse(const nlohmann::json& browser, const nlohmann::json& credits, Content& out, std::string& error)
{
    error.clear();
    json_schema::Registry registry;
    for (const char* schema : {kBrowserSchema, kCreditsSchema})
        if (!registry.Add(nlohmann::json::parse(schema), error)) return false;
    for (const auto& pair : {std::pair{"obl.object_browser", &browser}, std::pair{"obl.credits", &credits}}) {
        const auto errors = registry.Validate(pair.first, *pair.second);
        if (!errors.empty()) {
            error = std::string(pair.first) + errors.front();
            return false;
        }
    }
    Content result;
    for (size_t i = 0; i < std::size(kLists); ++i) {
        for (const auto& row : browser["lists"][kLists[i]]) {
            if (row.contains("separator")) {
                result.lists[i].push_back({"", -1});
                continue;
            }
            const bool filterList = i < 2;
            if (filterList != row.contains("filter")) {
                error = std::string(kLists[i]) + ": expected " + (filterList ? "filter kind" : "value");
                return false;
            }
            const int value = filterList ? FilterId(row["filter"].get<std::string>()) : row["value"].get<int>();
            if (filterList && (value < 1 || (i == 0 ? value > 16 : value < 17))) {
                error = std::string(kLists[i]) + ": unrecognized or inappropriate filter kind";
                return false;
            }
            if ((i == 3 && (value < 0 || value > 6)) || (i == 4 && (value < 0 || value > 5)) || (i == 5 && (value < 0 || value > 5))) {
                error = std::string(kLists[i]) + ": unsupported value";
                return false;
            }
            result.lists[i].push_back({row["key"].get<std::string>(), value});
        }
    }
    for (const auto& row : browser["headings"]) {
        const int id = FilterId(row["filter"].get<std::string>());
        if (id < 1) {
            error = "headings: unrecognized filter kind";
            return false;
        }
        result.headings.push_back({row["key"].get<std::string>(), id});
    }
    for (size_t i = 0; i < result.buttons.size(); ++i) result.buttons[i] = browser["buttons"][i].get<std::string>();
    for (const auto& row : credits["rows"])
        result.credits.push_back({row["font"].get<int>(), row["line"].get<int>(), row["color"].get<uint32_t>(),
                row["long_spacing"].get<bool>(), row["kind"].get<int>()});
    // The last line leaving the screen starts the credits end timer.
    for (size_t i = 0; i < result.credits.size(); ++i) {
        if (result.credits[i].kind != (i + 1 == result.credits.size() ? 1 : 0)) {
            error = "credits: only the final row must have kind 1";
            return false;
        }
    }
    out = std::move(result);
    return true;
}

bool LoadPack(const std::filesystem::path& packDir, Content& out, std::string& error)
{
    const auto read = [&](const char* name, nlohmann::json& document) {
        const auto path = pack_path::Resolve(packDir, std::filesystem::path("gui") / name);
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            error = "cannot open " + path.string();
            return false;
        }
        document = nlohmann::json::parse(file, nullptr, false);
        if (document.is_discarded()) {
            error = "invalid JSON in " + path.string();
            return false;
        }
        return true;
    };
    nlohmann::json browser, credits;
    if (!read("object_browser.json", browser) || !read("credits.json", credits)) return false;
    return Parse(browser, credits, out, error);
}
} // namespace obl::gui_content
