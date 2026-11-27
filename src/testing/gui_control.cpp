// Dialog control and state for GUI tests (docs/architecture/data-formats/converter/gui-runtime.md): opens and closes dialogs through the game's
// GuiManager::ShowDialog / GuiManager::CloseDialog, finds widgets in a dialog's tree and moves the synthetic cursor to their
// centre, and summarises the dialog list, mode, selection and context for `expect`. No game function is replaced.

#include "gui_control.h"

#include "game/game/game_accessors.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_modes_tools.h"
#include "game/ui/gui_patch_runtime.h"
#include "game/ui/input.h"
#include "game/ui/extra_scenarios_screen.h"
#include "game/ui/mods_screen.h"
#include "game/ui/own_layer.h"
#include "gui_tree.h"
#include "game/ui/gui_modes_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/ascii_case.h"
#include "core/log.h"
#include "core/sha256.h"
#include "game/mods/mod_catalog.h"
#include "test_util.h"
#include "platform/sdl_present.h"

#include <nlohmann/json.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <bit>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace obl::test {
namespace {

using namespace world_data;
using namespace ui;
using namespace gui;

// Script command names of the widget classes (docs/architecture/data-formats/converter/gui-layouts.md), used by `gui-move <dialog> <Class> <n>`.
struct ClassName {
    const char* name;
    GuiType type;
};
constexpr ClassName kClasses[] = {
        {"Button", GuiType::Button},
        {"CheckBox", GuiType::CheckBox},
        {"ComboBox", GuiType::List},
        {"Define", GuiType::Define},
        {"Dialog", GuiType::Dialog},
        {"ScrollBar", GuiType::ScrollBar},
        {"Slider", GuiType::Slider},
        {"Table", GuiType::Table},
        {"TabWindow", GuiType::TabWindow},
        {"Tab", GuiType::Tab},
        {"Image", GuiType::Image},
        {"MainItem", GuiType::MainItem},
        {"Text", GuiType::Text},
        {"Variable", GuiType::Variable},
        {"VariableImage", GuiType::VariableImage},
        {"Column", GuiType::TableColumn},
        {"TGA", GuiType::Tga},
        {"ImageList", GuiType::ImageList},
        {"Bar", GuiType::Bar},
        {"NumberAdjust", GuiType::NumberAdjust},
        {"RadioGroup", GuiType::RadioGroup},
        {"RadioButton", GuiType::RadioButton},
        {"ListBox", GuiType::ListBox},
};

bool ClassByName(const std::string& name, int32_t& type)
{
    for (const auto& c : kClasses)
        if (ascii::EqualsNoCase(c.name, name)) {
            type = static_cast<int32_t>(c.type);
            return true;
        }
    return false;
}

const GuiWidget* FindDialog(const GuiManager& gui, const std::string& name)
{
    std::vector<const GuiWidget*> dialogs;
    if (!ChainWidgets(reinterpret_cast<const GuiManagerWidgetNode*>(gui.m_definitions), dialogs)) return nullptr;
    for (const auto* widget : dialogs) {
        std::string text;
        if (SafeString(reinterpret_cast<const GuiDialogData*>(widget)->m_name, 128, text) && ascii::EqualsNoCase(text, name)) return widget;
    }
    return nullptr;
}

// The open-dialogs entry of a dialog widget (null when it is not in the list).
const GuiManagerDialogEntry* FindEntry(const GuiManager& gui, const GuiWidget* dialog)
{
    const GuiManagerDialogNode* sentinel = gui.m_dialogs;
    if (!Readable(sentinel, sizeof(*sentinel))) return nullptr;
    size_t count = 0;
    for (const auto* node = sentinel->m_next; node != sentinel; node = node->m_next) {
        if (!Readable(node, sizeof(*node)) || ++count > kMaxWidgets) return nullptr;
        const auto* entry = View<GuiManagerDialogEntry>(node->m_entry);
        if (entry && entry->m_dialog == reinterpret_cast<const GuiWidget*>(dialog)) return entry;
    }
    return nullptr;
}

// Pre-order walk of a dialog's tree (shared template widgets are visited once); `visit` returns true to stop.
template<class Callback> bool Walk(const GuiWidget& root, Callback&& visit)
{
    std::set<const void*> seen;
    std::vector<const GuiWidget*> stack{&root};
    while (!stack.empty()) {
        const GuiWidget* w = stack.back();
        stack.pop_back();
        if (!seen.insert(w).second) continue;
        if (visit(*w)) return true;
        Children children;
        if (!CollectChildren(*w, children)) return false;
        for (auto it = children.widgets.rbegin(); it != children.widgets.rend(); ++it) stack.push_back(*it);
    }
    return true;
}

// A dialog's widget by `<Class> <n>` (n-th of that class, 0-based, pre-order) or `tip <TOOLTIP_KEY>`.
const GuiWidget* FindWidget(const GuiWidget& dialog, const std::vector<std::string>& selector, std::string& why)
{
    const GuiWidget* found = nullptr;
    if (selector.size() == 2 && ascii::EqualsNoCase(selector[0], "id")) {
        found = gui_patch_runtime::FindControl(selector[1]);
        if (!found) why = "no widget with id " + selector[1];
        return found;
    }
    if (selector.size() == 2 && ascii::EqualsNoCase(selector[0], "tip")) {
        Walk(dialog, [&](const GuiWidget& w) {
            std::string tip;
            if (!found && w.m_tooltip && SafeString(w.m_tooltip, 256, tip) && tip == selector[1]) found = &w;
            return found != nullptr;
        });
        if (!found) why = "no widget with tooltip " + selector[1];
        return found;
    }
    int32_t type;
    if (selector.size() != 2 || !ClassByName(selector[0], type)) {
        why = "selector must be `<Class> <n>` or `tip <KEY>`";
        return nullptr;
    }
    int remaining = std::atoi(selector[1].c_str());
    Walk(dialog, [&](const GuiWidget& w) {
        if (w.m_type == type && remaining-- == 0) found = &w;
        return found != nullptr;
    });
    if (!found) why = "dialog has no " + selector[0] + " number " + selector[1];
    return found;
}

uint32_t Fnv(uint32_t hash, const void* data, size_t size)
{
    for (size_t i = 0; i < size; ++i) hash = (hash ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    return hash;
}

struct State {
    int entries = 0, open = 0, animating = 0;
    uint32_t hash = 2166136261u;
};

// One pass over the open-dialogs list: counts and a hash of what the list says (names, states, provider presence), the
// modes, the selection and the context slots. Pointers never enter the hash, so two runs agree.
bool Summarise(const GuiManager& gui, State& out)
{
    const GuiManagerDialogNode* sentinel = gui.m_dialogs;
    if (!Readable(sentinel, sizeof(*sentinel))) return false;
    size_t count = 0;
    for (const auto* node = sentinel->m_next; node != sentinel; node = node->m_next) {
        if (!Readable(node, sizeof(*node)) || ++count > kMaxWidgets) return false;
        const auto* entry = View<GuiManagerDialogEntry>(node->m_entry);
        const auto* dialog = entry ? View<GuiDialogData>(entry->m_dialog) : nullptr;
        std::string name;
        if (!dialog || !SafeString(dialog->m_name, 128, name)) return false;
        ++out.entries;
        out.open += entry->m_state == 1;
        out.animating += dialog->m_animating != 0;
        const int32_t state = entry->m_state;
        const uint8_t provider = entry->m_provider != nullptr;
        out.hash = Fnv(out.hash, name.c_str(), name.size() + 1);
        out.hash = Fnv(out.hash, &state, sizeof(state));
        out.hash = Fnv(out.hash, &provider, 1);
    }
    for (int32_t v : {gui.m_mode, gui.m_requestedMode, gui.m_selection}) out.hash = Fnv(out.hash, &v, sizeof(v));
    const auto* context = reinterpret_cast<const GuiManager*>(&gui)->m_context;
    out.hash = Fnv(out.hash, context, sizeof(int32_t) * 13);
    return true;
}

} // namespace

bool ReadGuiProbe(const std::string& name, double& out)
{
    if (name.compare(0, 4, "gui-") != 0) return false;
    const GuiManager* gui = Manager();
    State state;
    if (!gui || !Summarise(*gui, state)) return false;
    if (name == "gui-entries")
        out = state.entries;
    else if (name == "gui-open-dialogs")
        out = state.open;
    else if (name == "gui-animating")
        out = state.animating;
    else if (name == "gui-mode")
        out = gui->m_mode;
    else if (name == "gui-photos-composed")
        out = obl::gui::AlbumComposeCount();
    else if (name == "gui-camera-photos") {
        // Thumbnails on the photo mode's strip (GuiModeCamera::m_thumbs); -1 outside that mode.
        const auto* camera = static_cast<const GuiModeCamera*>(gui->m_modeObject);
        out = -1;
        if (gui->m_mode == 2 && camera && Readable(camera, sizeof(*camera)) && Readable(camera->m_thumbs, sizeof(GuiModeCameraThumb))) {
            int count = 0;
            const GuiModeCameraThumb* head = camera->m_thumbs;
            for (const GuiModeCameraThumb* node = head->m_next; node != head; node = node->m_next) {
                if (!Readable(node, sizeof(*node)) || ++count > 1000) return false;
            }
            out = count;
        }
    } else if (name == "gui-mode-requested")
        out = gui->m_requestedMode;
    else if (name == "gui-selection")
        out = gui->m_selection;
    else if (name == "gui-hash")
        out = state.hash;
    else if (name == "gui-mod-action-count")
        out = gui_patch_runtime::ModActionCount();
    else if (name.compare(0, 12, "gui-context-") == 0) {
        const int index = std::atoi(name.c_str() + 12);
        if (index < 0 || index >= 13) return false;
        out = reinterpret_cast<const GuiManager*>(gui)->m_context[index];
    } else
        return false;
    return true;
}

bool ExecuteGuiControlCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "gui-stage-catalog") {
        if (args.size() != 2 || args[0].empty() || args[1].empty() || args[1].size() % 2 != 0) {
            OBL_LOG("autotest: FAILED gui-stage-catalog needs <mod-id> <hex-zip>");
            return true;
        }
        const std::string& modId = args[0];
        const std::string& hex = args[1];
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex.size(); i += 2) {
            unsigned value = 0;
            const auto result = std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
            if (result.ec != std::errc{} || result.ptr != hex.data() + i + 2) {
                OBL_LOG("autotest: FAILED gui-stage-catalog malformed hex");
                return true;
            }
            bytes.push_back(static_cast<uint8_t>(value));
        }
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::path("testlogs") / "local-catalog";
        std::filesystem::create_directories(dir, ec);
        const std::string zipName = modId + ".zip";
        std::ofstream zipOut(dir / zipName, std::ios::binary);
        zipOut.write(std::bit_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        zipOut.close();

        nlohmann::json doc = {
                {"format", "obl.mod_catalog"},
                {"version", 1},
                {"title", "Local Test Catalog"},
                {"mods",
                        nlohmann::json::array({{
                                {"id", modId},
                                {"title", "Catalog Test Mod"},
                                {"mod_version", "1.0.0"},
                                {"packs", {"l0"}},
                                {"scope", "presentation"},
                                {"api", 1},
                                {"url", zipName},
                                {"sha256", sha256::Hex(bytes)},
                                {"size", bytes.size()},
                                {"license", "MIT"},
                        }})},
        };
        static constexpr ed25519::Seed kTestSeed = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec,
                0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
        std::string signedText;
        std::string error;
        if (!mods::SignCatalogJson(doc.dump(), kTestSeed, signedText, error)) {
            OBL_LOG("autotest: FAILED gui-stage-catalog sign failed: %s", error.c_str());
            return true;
        }
        const std::filesystem::path catPath = dir / "catalog.json";
        std::ofstream catOut(catPath, std::ios::binary);
        catOut << signedText;
        catOut.close();
        const std::string abs = std::filesystem::absolute(catPath, ec).generic_string();
        const std::string url = abs.starts_with('/') ? ("file://" + abs) : ("file:///" + abs);
        if (!obl::gui::mods_screen::QueueCatalogUrl(url, true)) {
            OBL_LOG("autotest: FAILED gui-stage-catalog could not queue %s", url.c_str());
        } else {
            OBL_LOG("autotest: gui-stage-catalog %s", url.c_str());
        }
        return true;
    }
    if (name == "gui-add-catalog") {
        if (args.size() != 1 || args[0].empty()) {
            OBL_LOG("autotest: FAILED gui-add-catalog needs a catalog URL or path");
            return true;
        }
        std::string url = args[0];
        if (!url.starts_with("file://") && !url.starts_with("https://")) {
            std::error_code ec;
            const auto abs = std::filesystem::absolute(url, ec).generic_string();
            url = abs.starts_with('/') ? ("file://" + abs) : ("file:///" + abs);
        }
        if (!obl::gui::mods_screen::QueueCatalogUrl(url, true)) {
            OBL_LOG("autotest: FAILED gui-add-catalog could not queue %s", url.c_str());
        } else {
            OBL_LOG("autotest: gui-add-catalog %s", url.c_str());
        }
        return true;
    }
    if (name == "gui-drop-mod") {
        if (args.size() != 1 || args[0].empty() || args[0].size() % 2 != 0) {
            OBL_LOG("autotest: FAILED gui-drop-mod needs a ZIP in hex");
            return true;
        }
        const std::string& hex = args[0];
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex.size(); i += 2) {
            unsigned value = 0;
            const auto result = std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
            if (result.ec != std::errc{} || result.ptr != hex.data() + i + 2) {
                OBL_LOG("autotest: FAILED gui-drop-mod malformed hex");
                return true;
            }
            bytes.push_back(static_cast<uint8_t>(value));
        }
        // Keep the fixture in the test slot and feed it through the same SDL event and file read as a desktop drop.
        static constexpr char kArchivePath[] = "testlogs/gui-drop-mod.zip";
        std::error_code ec;
        std::filesystem::create_directory("testlogs", ec);
        std::ofstream file(kArchivePath, std::ios::binary);
        file.write(std::bit_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        const auto window = platform::SdlGameWindow(game::GetGameWindow());
        if (!file || !window) {
            OBL_LOG("autotest: FAILED gui-drop-mod cannot stage archive or find window");
            return true;
        }
        for (const auto type : {SDL_EVENT_DROP_BEGIN, SDL_EVENT_DROP_FILE, SDL_EVENT_DROP_COMPLETE}) {
            SDL_Event event{};
            event.type = type;
            event.drop.windowID = SDL_GetWindowID(window);
            if (type == SDL_EVENT_DROP_FILE) event.drop.data = kArchivePath;
            if (!SDL_PushEvent(&event)) {
                OBL_LOG("autotest: FAILED gui-drop-mod could not send SDL event");
                break;
            }
        }
        return true;
    }
    if (name == "gui-mod-action") {
        if (args.empty()) {
            OBL_LOG("autotest: FAILED gui-mod-action needs an expected action");
            return true;
        }
        const std::string& actual = gui_patch_runtime::LastModAction();
        if (actual == args[0])
            OBL_LOG("autotest: PASSED gui-mod-action == %s", actual.c_str());
        else
            OBL_LOG("autotest: FAILED gui-mod-action expected=%s actual=%s", args[0].c_str(), actual.c_str());
        return true;
    }
    if (name != "gui-open" && name != "gui-close" && name != "gui-move" && name != "gui-state" && name != "gui-animate" &&
            name != "gui-close-all")
        return false;
    const GuiManager* gui = Manager();
    if (!gui) {
        OBL_LOG("autotest: FAILED %s - no GUI manager yet", name.c_str());
        return true;
    }
    if (name == "gui-state") {
        State state;
        if (!Summarise(*gui, state)) {
            OBL_LOG("autotest: FAILED gui-state - open-dialogs list unreadable");
            return true;
        }
        const auto* context = reinterpret_cast<const GuiManager*>(gui)->m_context;
        OBL_LOG("autotest: gui-state entries=%d open=%d animating=%d mode=%d requested=%d selection=%d hash=%u ctx0=%d ctx5=%d ctx9=%d "
                "ctx10=%d "
                "ctx11=%d ctx12=%d",
                state.entries, state.open, state.animating, gui->m_mode, gui->m_requestedMode, gui->m_selection, state.hash, context[0],
                context[5], context[9], context[10], context[11], context[12]);
        if (!args.empty()) {
            const GuiWidget* dialog = FindDialog(*gui, args[0]);
            const GuiManagerDialogEntry* entry = dialog ? FindEntry(*gui, dialog) : nullptr;
            if (!dialog)
                OBL_LOG("autotest: FAILED gui-state no dialog named %s", args[0].c_str());
            else
                OBL_LOG("autotest: gui-state-dialog %s entry=%d state=%d visible=%d provider=%d", args[0].c_str(), entry != nullptr,
                        entry ? entry->m_state : -1, dialog->m_visible, entry && entry->m_provider != nullptr);
        }
        return true;
    }

    if (name == "gui-close-all") {
        // For windows whose name depends on the object (a building's m_Menu), which gui-close cannot address.
        const_cast<GuiManager*>(gui)->CloseAllDialogs();
        OBL_LOG("autotest: gui-close-all");
        return true;
    }
    if (args.empty()) {
        OBL_LOG("autotest: FAILED %s needs a dialog name", name.c_str());
        return true;
    }
    const GuiWidget* dialog = FindDialog(*gui, args[0]);
    if (!dialog) {
        OBL_LOG("autotest: FAILED %s no dialog named %s", name.c_str(), args[0].c_str());
        return true;
    }
    auto* manager = const_cast<GuiManager*>(gui);
    if (name == "gui-open") {
        // Queues the dialog (state 0); the next GuiManager::UpdateDialogs opens it, so `step 1` / `await frames` follow.
        const int modal = args.size() > 1 && args[1] == "modal";
        const uint8_t result = manager->ShowDialog(args[0].c_str(), nullptr, modal != 0);
        OBL_LOG("autotest: gui-open %s modal=%d result=%d", args[0].c_str(), modal, result);
    } else if (name == "gui-close") {
        const uint8_t result = manager->CloseDialog(args[0].c_str());
        OBL_LOG("autotest: gui-close %s result=%d", args[0].c_str(), result);
    } else if (name == "gui-animate") {
        // gui-animate <dialog> <type 1..4> <seconds> [delay]: GuiManager::AnimateDialog on an open dialog (type 1 slides in, 2 slides out and closes it).
        if (args.size() < 3) {
            OBL_LOG("autotest: FAILED gui-animate needs <dialog> <type> <seconds> [delay]");
            return true;
        }
        const int type = std::atoi(args[1].c_str()), delay = args.size() > 3 ? std::atoi(args[3].c_str()) : 0;
        manager->AnimateDialog(args[0].c_str(), type, static_cast<float>(std::atof(args[2].c_str())), delay);
        OBL_LOG("autotest: gui-animate %s type=%d seconds=%s delay=%d", args[0].c_str(), type, args[2].c_str(), delay);
    } else {
        const std::vector<std::string> selector(args.begin() + 1, args.end());
        if (selector.size() == 2 && ascii::EqualsNoCase(selector[0], "target") &&
                (ascii::EqualsNoCase(args[0], own_layer::kModsDialog) || ascii::EqualsNoCase(args[0], own_layer::kExtraScenariosDialog))) {
            int x = 0, y = 0;
            const bool found = ascii::EqualsNoCase(args[0], own_layer::kModsDialog)
                    ? obl::gui::mods_screen::FindTarget(selector[1], x, y)
                    : obl::gui::extra_scenarios_screen::FindTarget(selector[1], x, y);
            if (!found) {
                OBL_LOG("autotest: FAILED gui-move %s: no target %s", args[0].c_str(), selector[1].c_str());
                return true;
            }
            platform::SetSyntheticCursor(x, y);
            OBL_LOG("autotest: gui-move %s target %s x=%d y=%d", args[0].c_str(), selector[1].c_str(), x, y);
            return true;
        }
        std::string why;
        const GuiWidget* widget = FindWidget(*dialog, selector, why);
        if (!widget) {
            OBL_LOG("autotest: FAILED gui-move %s: %s", args[0].c_str(), why.c_str());
            return true;
        }
        const int x = (widget->m_x0 + widget->m_x1) / 2, y = (widget->m_y0 + widget->m_y1) / 2;
        platform::SetSyntheticCursor(x, y);
        OBL_LOG("autotest: gui-move %s x=%d y=%d type=%d", args[0].c_str(), x, y, widget->m_type);
    }
    return true;
}

} // namespace obl::test
