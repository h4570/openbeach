#include "game/ui/mods_screen.h"

#include "core/ini_file.h"
#include "core/key_names.h"
#include "core/log.h"
#include "core/os.h"
#include "core/pack_path.h"
#include "core/settings_schema.h"
#include "formats/data_patch.h"
#include "formats/key_actions.h"
#include "formats/mod_pack.h"
#include "game/game/game_api.h"
#include "game/game/game_accessors.h"
#include "game/game/start_settings.h"
#include "game/mods/http_fetch.h"
#include "game/mods/mod_archive.h"
#include "game/mods/mod_catalog.h"
#include "game/mods/mod_list.h"
#include "game/mods/presentation_scripts.h"
#include "game/tables/text_load.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/own_layer.h"
#include "game/ui/own_widgets.h"
#include "game/ui/text_table.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_misc.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obl::gui::mods_screen {

namespace {

namespace own = own_widgets;

constexpr const char* kHelpUrl = "https://github.com/h4570/openbeach/blob/main/docs/modding/players/mods-screen.md";
constexpr int kPageSize = 9;
constexpr int kMaxSettingRows = 5;
constexpr int kMaxConflictRows = 4;

// State that survives a front-end reload when a presentation mod changes (ReloadFrontEnd destroys and rebuilds the dialog tree).
std::filesystem::path g_gameDirectory;
bool g_startupRecorded = false;
std::vector<std::string> g_startupWorldMods;
std::vector<std::string> g_desiredActive;
std::string g_selectedId;
int g_page = 0;
std::u16string g_statusText;
bool g_statusIsError = false;

#ifndef __EMSCRIPTEN__
// SDL can complete the picker on another thread and after the dialog has closed.
std::mutex g_pickerMutex;
bool g_pickerOpen = false;
bool g_acceptPickerResult = false;
std::string g_pickedFile;
std::string g_pickerError;

void SDLCALL ModFilePicked(void*, const char* const* files, int)
{
    const std::lock_guard lock(g_pickerMutex);
    g_pickerOpen = false;
    if (!g_acceptPickerResult) return;
    if (!files)
        g_pickerError = SDL_GetError();
    else if (*files)
        g_pickedFile = *files;
}
#endif

void SetArchiveInputEnabled(bool enabled)
{
#ifdef __EMSCRIPTEN__
    EM_ASM(
            {
                if (Module["enableModArchiveInput"]) Module["enableModArchiveInput"](!!$0);
            },
            enabled);
#else
    const std::lock_guard lock(g_pickerMutex);
    g_acceptPickerResult = enabled && !g_pickerOpen;
    g_pickedFile.clear();
    g_pickerError.clear();
#endif
}

void PersistModChange(const std::string& id)
{
#ifdef __EMSCRIPTEN__
    EM_ASM(
            {
                if (Module["persistModChange"]) Module["persistModChange"](UTF8ToString($0));
            },
            id.c_str());
#else
    (void)id;
#endif
}

std::filesystem::path GameDirectory()
{
    return g_gameDirectory.empty() ? std::filesystem::current_path() : g_gameDirectory;
}

std::u16string Utf8ToEngine(std::string_view utf8)
{
    std::u16string wide;
    wide.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        const auto c = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 6) | (static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 12) |
                    ((static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu) << 6) | (static_cast<uint8_t>(utf8[i + 3]) & 0x3Fu);
            i += 4;
        } else {
            cp = '?';
            ++i;
        }
        if (cp <= 0xFFFFu)
            wide.push_back(static_cast<char16_t>(cp));
        else
            wide.push_back(u'?');
    }
    return text_load::EngineValue(wide, ui::text_table::Language() == "pl");
}

std::u16string Lookup(const char* key)
{
    const Char16* text = ui::text_table::Lookup(key);
    return text ? std::u16string(text) : std::u16string();
}

// Replaces each `%s` in `pattern` with the next argument in `args`.
std::u16string Format(std::u16string_view pattern, std::initializer_list<std::u16string_view> args)
{
    std::u16string out;
    out.reserve(pattern.size() + 32);
    auto arg = args.begin();
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == u'%' && i + 1 < pattern.size() && pattern[i + 1] == u's' && arg != args.end()) {
            out.append(*arg++);
            ++i;
        } else {
            out.push_back(pattern[i]);
        }
    }
    return out;
}

std::string Join(std::span<const std::string> items, std::string_view separator = ",")
{
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out.append(separator);
        out.append(items[i]);
    }
    return out;
}

std::string FormatKeys(const key_actions::KeyList& keys)
{
    if (keys.empty()) return "none";
    std::string out;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i) out.append(", ");
        const std::string_view name = input::KeyName(keys[i]);
        out.append(name.empty() ? "?" : name);
    }
    return out;
}

std::string FormatActions(const std::vector<key_actions::ModAction>& actions)
{
    std::string out;
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i) out.append(", ");
        out.append(actions[i].id);
        if (!actions[i].keys.empty()) out.append(" (" + FormatKeys(actions[i].keys) + ")");
    }
    return out;
}

std::string FormatBindings(const std::vector<key_actions::Binding>& bindings)
{
    std::string out;
    for (size_t i = 0; i < bindings.size(); ++i) {
        if (i) out.append(", ");
        out.append(bindings[i].action);
        out.append("=");
        out.append(FormatKeys(bindings[i].keys));
    }
    return out;
}

std::u16string ProblemText(mods::Problem problem, std::string_view detail)
{
    const std::u16string arg = Utf8ToEngine(detail);
    switch (problem) {
    case mods::Problem::Invalid: return Format(Lookup("OBL_MODS_PROBLEM_INVALID"), {arg});
    case mods::Problem::WrongPack: return Format(Lookup("OBL_MODS_PROBLEM_PACK"), {arg});
    case mods::Problem::NeedsEngine: return Format(Lookup("OBL_MODS_PROBLEM_ENGINE"), {arg});
    case mods::Problem::NeedsApi: return Format(Lookup("OBL_MODS_PROBLEM_API"), {arg});
    case mods::Problem::MissingRequirement: return Format(Lookup("OBL_MODS_PROBLEM_MISSING"), {arg});
    case mods::Problem::RequirementLater: return Format(Lookup("OBL_MODS_PROBLEM_LATER"), {arg});
    case mods::Problem::ConflictsWith: return Format(Lookup("OBL_MODS_PROBLEM_CONFLICT"), {arg});
    case mods::Problem::None: break;
    }
    return {};
}

std::u16string ScopeText(mod_pack::Scope scope)
{
    return Lookup(scope == mod_pack::Scope::Presentation ? "OBL_MODS_PRESENTATION" : "OBL_MODS_WORLD");
}

void RecordStartupOnce()
{
    if (g_startupRecorded) return;
    g_startupRecorded = true;
    g_startupWorldMods.clear();
    for (const mod_pack::ActiveMod& mod : mod_pack::Active())
        if (mod.manifest.scope == mod_pack::Scope::World) g_startupWorldMods.push_back(mod.manifest.id);
    g_desiredActive = pack_path::ActiveMods();
}

struct Target {
    std::string name;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::function<void()> onClick;
};

struct GuiModsPanel;
GuiModsPanel* g_openPanel = nullptr;

struct GuiModsPanel final : ui::GuiLeaf<ui::GuiWidget> {
    GuiModsPanel(int x0, int y0, int x1, int y1) : GuiLeaf(x0, y0, x1, y1)
    {
        m_type = kModsPanelType;
        own::ResetPackLook(); // a pack reload or a live reload may bring other menu art
    }

    ~GuiModsPanel() override
    {
        CancelPendingFetches();
        StopUrlInput();
        if (g_openPanel == this) {
            SetArchiveInputEnabled(false);
            g_openPanel = nullptr;
        }
    }

    const char* ClassName() const override { return "GuiModsPanel"; }

    void SetVisible(uint32_t visible) override
    {
        VisibleInvalidate(visible);
        if (visible) {
            g_openPanel = this;
            SetArchiveInputEnabled(true);
            LoadList();
            LoadCatalogs();
        } else if (g_openPanel == this) {
            CancelPendingFetches();
            StopUrlInput();
            SetArchiveInputEnabled(false);
            m_pendingReplace.reset();
            m_pendingRemove.clear();
            m_pendingAddCatalog.reset();
            m_pendingKeyChange.reset();
            m_droppedFile.clear();
            g_openPanel = nullptr;
        }
    }

    void Update(float) override
    {
        if (m_pendingBack) {
            m_pendingBack = false;
            ui::GuiManager* gui = game::GetGuiManager();
            if (gui) {
                gui->CloseAllDialogs();
                gui->ShowDialog("MainMenu", nullptr, false);
            }
            return;
        }
        if (m_pendingReload) {
            m_pendingReload = false;
            game::game_api::ReloadFrontEnd(own_layer::kModsDialog);
            return;
        }
        if (m_visible) {
            PollCatalogFetches();
            if (!IsConfirming()) PollArchiveInput();
        }
    }

    bool MouseLeft(int x, int y, int state) override
    {
        if (!m_visible || state != 1 || !HitTestBase(x, y)) return false;
        // Walk a snapshot of the targets: a click rebuilds the list and targets.
        for (size_t i = 0; i < m_targets.size(); ++i) {
            const Target& target = m_targets[i];
            if (x >= target.x0 && x < target.x1 && y >= target.y0 && y < target.y1) {
                auto action = target.onClick;
                if (action) action();
                return true;
            }
        }
        return true;
    }

    bool MouseWheel(int x, int y, int delta) override
    {
        if (!m_visible || !HitTestBase(x, y) || !m_list) return false;
        if (delta > 0)
            ChangePage(-1);
        else if (delta < 0)
            ChangePage(1);
        return true;
    }

    own::Palette m_pal;
    int m_round = 0; // corner radius of boxes (0: square)

    static uint32_t LerpRgb(uint32_t a, uint32_t b, int step, int steps)
    {
        uint32_t out = 0;
        for (int shift = 16; shift >= 0; shift -= 8) {
            const int ca = static_cast<int>((a >> shift) & 255), cb = static_cast<int>((b >> shift) & 255);
            out |= static_cast<uint32_t>(ca + (cb - ca) * step / steps) << shift;
        }
        return out;
    }

    void Draw() override
    {
        if (!m_visible || !m_dirty) return;
        if (!m_list) LoadList();
        RebuildTargets();

        const int w = m_x1 - m_x0;
        const int h = m_y1 - m_y0;
        // A pack with coloured menu buttons gives the screen its colour and rounded boxes; any other pack keeps the flat style.
        const own::PackLook& look = own::GetPackLook();
        m_pal = look.available ? look.palette : own::Palette{};
        m_round = look.available ? 8 : 0;
        if (look.available) {
            // Sky-like backdrop: from the foam tint at the top to the sand tint at the bottom.
            for (int y = m_y0; y < m_y1; ++y)
                own::Fill(m_x0, y, m_x1, y + 1, LerpRgb(m_pal.foam, m_pal.sand, y - m_y0, std::max(1, h - 1)));
        } else {
            own::Fill(m_x0, m_y0, m_x1, m_y1, m_pal.sand);
        }

        // Header bar.
        own::Fill(m_x0, m_y0, m_x1, m_y0 + 48, m_pal.sea);
        own::Text(2, m_x0 + 16, m_y0 + 5, 130, Lookup("OBL_MODS_TITLE").c_str(), m_pal.white, 0);
        // The game version a mod's `engine_min` is checked against.
        own::Text(0, m_x0 + 16, m_y0 + 30, 130,
                Format(Lookup("OBL_MODS_GAME_VERSION"), {Utf8ToEngine(std::string(kEngineVersion))}).c_str(), m_pal.white, 0);
        DrawButton(m_x0 + 150, m_y0 + 8, m_x0 + 310, m_y0 + 40,
                Lookup(m_catalogView ? "OBL_MODS_INSTALLED_TAB" : "OBL_MODS_CATALOGS").c_str(), m_catalogView ? m_pal.foam : m_pal.seaDark,
                m_catalogView ? m_pal.ink : m_pal.white);
        if (!m_catalogView) {
            DrawButton(m_x0 + 318, m_y0 + 8, m_x0 + 520, m_y0 + 40, Lookup("OBL_MODS_INSTALL").c_str(), m_pal.seaDark, m_pal.white);
        }
        DrawButton(m_x0 + 696, m_y0 + 8, m_x0 + 784, m_y0 + 40, Lookup("OBL_MODS_BACK").c_str(), m_pal.foam, m_pal.ink);

        if (m_catalogView) {
            DrawCatalogListColumn();
            DrawCatalogRightColumn();
        } else {
            DrawListColumn();
            DrawRightColumn();
        }

        // Footer status bar.
        const int footerY = m_y0 + h - 44;
        own::Fill(m_x0, footerY, m_x0 + w, m_y1, m_pal.seaDark);
        std::u16string footer;
        uint32_t footerColor = m_pal.white;
        if (m_editingUrl) {
            own::Box(m_x0 + 12, footerY + 6, m_x0 + 500, footerY + 38, m_round, m_pal.white, m_pal.sea);
            const std::u16string shown = m_urlDraft.empty() ? Lookup("OBL_MODS_CAT_URL_HINT") : Utf8ToEngine(m_urlDraft + "_");
            own::Text(0, m_x0 + 18, footerY + 14, 476, shown.c_str(), m_urlDraft.empty() ? m_pal.grey : m_pal.ink, 0);
            DrawButton(m_x0 + 506, footerY + 6, m_x0 + 586, footerY + 38, Lookup("OBL_MODS_CAT_PASTE").c_str(), m_pal.sea, m_pal.white);
            DrawButton(m_x0 + 592, footerY + 6, m_x0 + 684, footerY + 38, Lookup("OBL_MODS_CONFIRM").c_str(), m_pal.green, m_pal.white);
            DrawButton(m_x0 + 690, footerY + 6, m_x0 + 784, footerY + 38, Lookup("OBL_MODS_CANCEL").c_str(), m_pal.foam, m_pal.ink);
        } else if (IsConfirming()) {
            if (m_pendingKeyChange) {
                footer = Format(Lookup("OBL_MODS_CAT_KEY_CHANGED"), {Utf8ToEngine(m_pendingKeyChange->catalog.title)});
                footerColor = 0xffc8c8;
            } else if (m_pendingAddCatalog) {
                footer = Format(Lookup("OBL_MODS_CAT_ADD_CONFIRM"), {Utf8ToEngine(m_pendingAddCatalog->url)});
            } else {
                const auto& id = m_pendingReplace ? m_pendingReplace->Id() : m_pendingRemove;
                footer = Format(Lookup(m_pendingReplace ? "OBL_MODS_REPLACE_CONFIRM" : "OBL_MODS_REMOVE_CONFIRM"), {Utf8ToEngine(id)});
            }
            DrawButton(m_x0 + 570, footerY + 6, m_x0 + 672, footerY + 38, Lookup("OBL_MODS_CONFIRM").c_str(), m_pal.sea, m_pal.white);
            DrawButton(m_x0 + 680, footerY + 6, m_x0 + 784, footerY + 38, Lookup("OBL_MODS_CANCEL").c_str(), m_pal.foam, m_pal.ink);
        } else if (!g_statusText.empty()) {
            footer = g_statusText;
            footerColor = g_statusIsError ? 0xffc8c8 : m_pal.white;
        } else if (m_list && !m_list->CanStart()) {
            footer = Lookup("OBL_MODS_FIX_FIRST");
            footerColor = 0xffc8c8;
        } else if (WorldModsChanged()) {
            footer = Lookup("OBL_MODS_WORLD_NEXT");
            footerColor = m_pal.foam;
        }
        if (!footer.empty()) own::Text(1, m_x0 + 16, footerY + 14, IsConfirming() ? 546 : w - 64, footer.c_str(), footerColor, 0);
        if (!m_editingUrl && !IsConfirming()) {
            DrawButton(m_x0 + w - 40, footerY + 6, m_x0 + w - 8, footerY + 38, u"?", m_pal.sea, m_pal.white);
        }

        m_dirty = 0;
    }

    void MarkDirty()
    {
        m_dirty = 1;
        if (ui::GuiManager* gui = game::GetGuiManager()) gui->InvalidateDialog(own_layer::kModsDialog, 0);
    }

    void LoadList()
    {
        RecordStartupOnce();
        const std::filesystem::path gameDir = GameDirectory();
        const std::string packId = pack_path::SelectedId();
        auto installed = mod_pack::ListInstalled(gameDir);
        m_list = std::make_unique<mods::ModList>(std::move(installed), packId, g_desiredActive, mod_pack::kPresentationApi);

        const std::optional<std::filesystem::path> packRel = pack_path::PackDirectory(packId);
        if (packRel) {
            const std::filesystem::path packPath = gameDir / *packRel;
            m_list->AddConflictSource([packPath](std::span<const std::string> ids) {
                std::vector<data_patch::Conflict> found;
                std::string error;
                std::vector<mods::Conflict> out;
                if (!data_patch::FindConflicts(packPath, ids, found, error)) return out;
                out.reserve(found.size());
                for (const data_patch::Conflict& c : found)
                    out.push_back({mods::ConflictKind::Value, c.winner, c.loser, c.file + " " + c.path});
                return out;
            });
        }
        mods::ModList* list = m_list.get();
        m_list->AddConflictSource([list](std::span<const std::string> ids) { return mods::FindTakeOverConflicts(*list, ids); });
        const std::filesystem::path iniPath = gameDir / game::start_settings::kFileName;
        const bool scripted = os::HasEnv("OBL_SCRIPT");
        key_actions::PlayerLookup playerKeys;
        if (!scripted) {
            playerKeys = [iniPath](std::string_view name) -> std::optional<std::string> {
                const std::string value = ini::ReadString(iniPath, "Keys", name, " ", 256);
                if (value == " ") return std::nullopt;
                return value;
            };
        }
        m_list->AddConflictSource(
                [list, playerKeys](std::span<const std::string> ids) { return mods::FindKeyConflicts(*list, ids, playerKeys); });

        if (g_selectedId.empty() || !m_list->Find(g_selectedId)) {
            g_selectedId = m_list->Entries().empty() ? std::string() : m_list->Entries()[0].mod.id;
        }
        ClampPage();
        LogConflicts();
        RebuildTargets();
        MarkDirty();
    }

    int PageCount() const
    {
        if (!m_list || m_list->Entries().empty()) return 1;
        return static_cast<int>((m_list->Entries().size() + kPageSize - 1) / kPageSize);
    }

    void ClampPage()
    {
        const int pages = PageCount();
        if (g_page < 0) g_page = 0;
        if (g_page >= pages) g_page = pages - 1;
    }

    void ChangePage(int delta)
    {
        const int before = g_page;
        g_page += delta;
        ClampPage();
        if (g_page != before) {
            RebuildTargets();
            MarkDirty();
        }
    }

    bool WorldModsChanged() const
    {
        if (!m_list) return false;
        std::vector<std::string> currentWorld;
        for (const mods::Entry& entry : m_list->Entries())
            if (entry.on && entry.mod.valid && entry.mod.manifest.scope == mod_pack::Scope::World) currentWorld.push_back(entry.mod.id);
        return currentWorld != g_startupWorldMods;
    }

    void LogConflicts() const
    {
        if (!m_list) return;
        for (const mods::Conflict& c : m_list->Conflicts()) {
            if (c.winner.empty())
                OBL_LOG("mods screen: conflict %s", c.what.c_str());
            else
                OBL_LOG("mods screen: conflict %s wins over %s (%s)", c.winner.c_str(), c.loser.c_str(), c.what.c_str());
        }
    }

    void ApplyChanges()
    {
        if (!m_list || !m_list->CanStart()) {
            g_statusText = Lookup("OBL_MODS_FIX_FIRST");
            g_statusIsError = true;
            RebuildTargets();
            MarkDirty();
            return;
        }
        const std::vector<std::string> desired = m_list->ActiveIds();
        g_desiredActive = desired;
        const std::filesystem::path gameDir = GameDirectory();
        if (!os::HasEnv("OBL_SCRIPT")) {
            const std::filesystem::path iniPath = gameDir / game::start_settings::kFileName;
            game::start_settings::CreateDefaultFile(iniPath);
            ini::WriteString(iniPath, "Mods", "Active", Join(desired));
        }

        // Presentation mods apply at once; world mods stay at the list the game started with until the next start.
        std::vector<std::string> liveIds;
        if (!WorldModsChanged()) {
            liveIds = desired;
        } else {
            liveIds = g_startupWorldMods;
            for (const mods::Entry& entry : m_list->Entries())
                if (entry.on && entry.mod.valid && entry.mod.manifest.scope == mod_pack::Scope::Presentation)
                    liveIds.push_back(entry.mod.id);
        }

        LogConflicts();
        if (liveIds != pack_path::ActiveMods()) {
            std::string error;
            if (!mod_pack::ActivateList(gameDir, pack_path::SelectedId(), liveIds, error)) {
                g_statusText = Format(Lookup("OBL_MODS_CANNOT_START"), {Utf8ToEngine(error)});
                g_statusIsError = true;
                OBL_LOG("mods screen: activate failed: %s", error.c_str());
                RebuildTargets();
                MarkDirty();
                return;
            }
            mods::RestartPresentationScripts();
            mods::ConfigureKeyBindings(gameDir / game::start_settings::kFileName);
            OBL_LOG("mods screen: active=%s", Join(liveIds).c_str());
            g_statusText.clear();
            g_statusIsError = false;
            m_pendingReload = true;
        } else if (WorldModsChanged()) {
            OBL_LOG("mods screen: saved=%s (world mods apply on next start)", Join(desired).c_str());
            g_statusText = Lookup("OBL_MODS_WORLD_NEXT");
            g_statusIsError = false;
        } else {
            g_statusText.clear();
            g_statusIsError = false;
        }
        RebuildTargets();
        MarkDirty();
    }

    void OnToggle(const std::string& id)
    {
        if (!m_list) return;
        const mods::Entry* entry = m_list->Find(id);
        if (!entry) return;
        const bool next = !entry->on;
        g_selectedId = id;
        mods::Problem why = mods::Problem::None;
        std::string detail;
        if (!m_list->SetOn(id, next, why, detail)) {
            const std::u16string reason = ProblemText(why, detail);
            g_statusText = Format(Lookup("OBL_MODS_REFUSED"), {Utf8ToEngine(id), reason});
            g_statusIsError = true;
            OBL_LOG("mods screen: refused %s (%s)", id.c_str(), detail.c_str());
            RebuildTargets();
            MarkDirty();
            return;
        }
        ApplyChanges();
    }

    void OnMove(const std::string& id, int delta)
    {
        if (!m_list) return;
        g_selectedId = id;
        if (!m_list->Move(id, delta)) return;
        ApplyChanges();
    }

    void OnStepSetting(const std::string& modId, const settings_schema::Field& field, int delta)
    {
        const auto& active = mod_pack::Active();
        const auto found = std::find_if(active.begin(), active.end(), [&](const auto& m) { return m.manifest.id == modId; });
        if (found == active.end()) return;
        const std::string current = mods::ModSettingValue(*found, field);
        const std::string next = settings_schema::Step(field, current, delta);
        if (next == current) return;
        std::string error;
        if (!mods::SetModSetting(modId, field.id, next, error)) {
            g_statusText = Utf8ToEngine(error);
            g_statusIsError = true;
        } else {
            g_statusText.clear();
            g_statusIsError = false;
            OBL_LOG("mods screen: setting %s.%s=%s", modId.c_str(), field.id.c_str(), next.c_str());
        }
        MarkDirty();
    }

    bool IsConfirming() const
    {
        return m_pendingReplace || !m_pendingRemove.empty() || m_pendingAddCatalog.has_value() || m_pendingKeyChange.has_value();
    }

    bool CanChangeFiles(const std::string& id)
    {
        const auto* entry = m_list ? m_list->Find(id) : nullptr;
        const auto& live = mod_pack::Active();
        if ((!entry || !entry->on) && std::none_of(live.begin(), live.end(), [&](const auto& mod) { return mod.manifest.id == id; }))
            return true;
        SetArchiveStatus("OBL_MODS_ARCHIVE_ACTIVE", id, true);
        return false;
    }

    void SetArchiveStatus(const char* key, std::string_view detail, bool error)
    {
        g_statusText = Format(Lookup(key), {Utf8ToEngine(detail)});
        g_statusIsError = error;
        OBL_LOG("mods screen: %s: %.*s", key, static_cast<int>(detail.size()), detail.data());
        RebuildTargets();
        MarkDirty();
    }

    void OnInstall()
    {
        if (os::HasEnv("OBL_SCRIPT")) return;
#ifdef __EMSCRIPTEN__
        EM_ASM({
            if (Module["pickModArchive"]) Module["pickModArchive"]();
        });
#else
        {
            const std::lock_guard lock(g_pickerMutex);
            if (g_pickerOpen) return;
            g_pickerOpen = true;
            g_acceptPickerResult = true;
        }
        static constexpr SDL_DialogFileFilter kFilters[] = {{"Mod ZIP", "zip"}};
        SDL_ShowOpenFileDialog(ModFilePicked, nullptr, SDL_GetKeyboardFocus(), kFilters, 1, nullptr, false);
#endif
    }

    void ReadArchiveFile(const std::string& path)
    {
        // SDL also opens content:// URIs returned by Android's system picker.
        const std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> stream(SDL_IOFromFile(path.c_str(), "rb"), SDL_CloseIO);
        if (!stream) {
            SetArchiveStatus("OBL_MODS_INSTALL_ERROR", SDL_GetError(), true);
            return;
        }
        const auto size = SDL_GetIOSize(stream.get());
        if (size > static_cast<int64_t>(mods::kMaxModZipBytes)) {
            SetArchiveStatus("OBL_MODS_INSTALL_ERROR", "ZIP exceeds 64 MiB", true);
            return;
        }
        // A document provider may offer a stream that cannot seek.
        std::vector<uint8_t> bytes;
        if (size > 0) bytes.reserve(static_cast<size_t>(size));
        std::array<uint8_t, 65536> block;
        while (const size_t count = SDL_ReadIO(stream.get(), block.data(), block.size())) {
            if (count > mods::kMaxModZipBytes - bytes.size()) {
                SetArchiveStatus("OBL_MODS_INSTALL_ERROR", "ZIP exceeds 64 MiB", true);
                return;
            }
            bytes.insert(bytes.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
        }
        if (SDL_GetIOStatus(stream.get()) != SDL_IO_STATUS_EOF) {
            SetArchiveStatus("OBL_MODS_INSTALL_ERROR", "cannot read the complete file", true);
            return;
        }
        InspectArchive(bytes);
    }

    void PollArchiveInput()
    {
        if (!m_droppedFile.empty()) {
            ReadArchiveFile(std::exchange(m_droppedFile, {}));
            return;
        }
#ifdef __EMSCRIPTEN__
        const int size = EM_ASM_INT({ return Module["modArchiveSize"] ? Module["modArchiveSize"]() : 0; });
        if (!size) return;
        if (size < 0) {
            SetArchiveStatus("OBL_MODS_INSTALL_ERROR", size == -2 ? "ZIP exceeds 64 MiB" : "cannot read archive", true);
            return;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        EM_ASM({ Module["copyModArchive"](HEAPU8, $0); }, bytes.data());
        InspectArchive(bytes);
#else
        std::string path;
        std::string error;
        {
            const std::lock_guard lock(g_pickerMutex);
            path.swap(g_pickedFile);
            error.swap(g_pickerError);
        }
        if (!error.empty()) SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
        if (!path.empty()) ReadArchiveFile(path);
#endif
    }

    void InspectArchive(std::span<const uint8_t> bytes)
    {
        mods::ModArchive archive;
        std::string error;
        if (!archive.Read(bytes, pack_path::SelectedId(), error)) {
            SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
            return;
        }
        const auto result = archive.Write(GameDirectory(), false, error);
        if (result == mods::ModArchive::Result::AlreadyInstalled) {
            if (!CanChangeFiles(archive.Id())) return;
            m_pendingReplace.emplace(std::move(archive));
            RebuildTargets();
            MarkDirty();
        } else if (result == mods::ModArchive::Result::Installed) {
            OnInstalled(archive.Id());
        } else {
            SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
        }
    }

    void OnInstalled(const std::string& id)
    {
        PersistModChange(id);
        g_selectedId = id;
        LoadList();
        EnsureVisible(id);
        SetArchiveStatus("OBL_MODS_INSTALLED", id, false);
    }

    void OnRemove(const std::string& id)
    {
        g_selectedId = id;
        if (CanChangeFiles(id)) {
            m_pendingRemove = id;
            RebuildTargets();
            MarkDirty();
        }
    }

    void ConfirmArchiveChange()
    {
        auto replace = std::exchange(m_pendingReplace, std::nullopt);
        const auto remove = std::exchange(m_pendingRemove, {});
        const auto& id = replace ? replace->Id() : remove;
        if (!CanChangeFiles(id)) return;

        std::string error;
        if (replace) {
            if (replace->Write(GameDirectory(), true, error) == mods::ModArchive::Result::Installed) {
                OnInstalled(id);
                StartNextQueuedDownload();
            } else {
                m_downloadQueue.clear();
                SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
            }
        } else if (mods::RemoveMod(GameDirectory(), id, error)) {
            PersistModChange(id);
            LoadList();
            SetArchiveStatus("OBL_MODS_REMOVED", id, false);
        } else {
            SetArchiveStatus("OBL_MODS_REMOVE_ERROR", error, true);
        }
    }

    // ---- Mod catalogs (#120) ----

    struct PendingAddCatalog {
        std::string url;
        bool allowLocal = false;
    };

    struct PendingKeyChange {
        std::string url;
        bool allowLocal = false;
        mods::Catalog catalog;
    };

    struct CatalogRow {
        std::string catalogTitle;
        std::string catalogUrl;
        mods::CatalogEntry entry;
    };

    struct ActiveCatalogFetch {
        uint64_t handle = 0;
        std::string url;
        bool allowLocal = false;
        bool isModZip = false;
        mods::CatalogEntry modEntry;
    };

    void LoadCatalogs()
    {
        std::string error;
        if (!m_catalogStore.Load(GameDirectory(), error)) {
            OBL_LOG("mods screen: cannot load catalogs: %s", error.c_str());
        }
        RebuildCatalogRows();
    }

    void SaveCatalogs()
    {
        if (os::HasEnv("OBL_SCRIPT")) return;
        std::string error;
        if (!m_catalogStore.Save(GameDirectory(), error)) {
            OBL_LOG("mods screen: cannot save catalogs: %s", error.c_str());
        }
    }

    void RebuildCatalogRows()
    {
        m_catalogRows.clear();
        const mods::SemVer engineVer = EngineVersion();
        const std::string packId = pack_path::SelectedId();
        for (const mods::StoredCatalog& cat : m_catalogStore.Catalogs()) {
            if (!cat.enabled) continue;
            const auto compatible = mods::FilterCompatibleMods(cat.mods, engineVer, packId, mod_pack::kPresentationApi);
            for (const mods::CatalogEntry& e : compatible) {
                m_catalogRows.push_back({cat.title, cat.url, e});
            }
        }
        if (m_selectedCatalogModId.empty() || std::none_of(m_catalogRows.begin(), m_catalogRows.end(), [&](const CatalogRow& r) {
                return r.entry.id == m_selectedCatalogModId;
            })) {
            m_selectedCatalogModId = m_catalogRows.empty() ? std::string() : m_catalogRows[0].entry.id;
        }
        const int pages = CatalogPageCount();
        if (m_catalogPage < 0) m_catalogPage = 0;
        if (m_catalogPage >= pages) m_catalogPage = pages - 1;
        RebuildTargets();
        MarkDirty();
    }

    mods::CatalogOffer OfferFor(const mods::CatalogEntry& entry) const
    {
        const mods::Entry* installed = m_list ? m_list->Find(entry.id) : nullptr;
        if (!installed) return mods::CatalogOffer::Install;
        const std::string version = installed->mod.valid ? installed->mod.manifest.modVersion : std::string();
        return mods::OfferFor(entry, &version);
    }

    int CatalogPageCount() const
    {
        if (m_catalogRows.empty()) return 1;
        return static_cast<int>((m_catalogRows.size() + kPageSize - 1) / kPageSize);
    }

    void ChangeCatalogPage(int delta)
    {
        const int before = m_catalogPage;
        m_catalogPage += delta;
        const int pages = CatalogPageCount();
        if (m_catalogPage < 0) m_catalogPage = 0;
        if (m_catalogPage >= pages) m_catalogPage = pages - 1;
        if (m_catalogPage != before) {
            RebuildTargets();
            MarkDirty();
        }
    }

    void CancelPendingFetches()
    {
        if (m_activeFetch.handle) {
            net::CancelFetch(m_activeFetch.handle);
            m_activeFetch = {};
        }
        m_refreshQueue.clear();
        m_downloadQueue.clear();
    }

    void StartUrlInput()
    {
        if (os::HasEnv("OBL_SCRIPT")) return;
#ifdef __EMSCRIPTEN__
        char urlBuf[1024] = {};
        const int got = EM_ASM_INT(
                {
                    const v = Module["promptCatalogUrl"] ? Module["promptCatalogUrl"]() : "";
                    if (!v) return 0;
                    stringToUTF8(v, $0, $1);
                    return 1;
                },
                urlBuf, static_cast<int>(sizeof(urlBuf)));
        if (got && urlBuf[0]) SubmitCatalogUrl(urlBuf, false);
#else
        m_editingUrl = true;
        m_urlDraft.clear();
        if (SDL_Window* win = SDL_GetKeyboardFocus()) SDL_StartTextInput(win);
        RebuildTargets();
        MarkDirty();
#endif
    }

    void StopUrlInput()
    {
        if (!m_editingUrl) return;
        m_editingUrl = false;
        m_urlDraft.clear();
#ifndef __EMSCRIPTEN__
        if (SDL_Window* win = SDL_GetKeyboardFocus()) SDL_StopTextInput(win);
#endif
    }

    void PasteIntoUrlDraft()
    {
        if (!m_editingUrl) return;
        if (char* clip = SDL_GetClipboardText()) {
            for (const char* p = clip; *p && m_urlDraft.size() < 512; ++p) {
                if (static_cast<unsigned char>(*p) >= 0x20 && static_cast<unsigned char>(*p) < 0x7f) m_urlDraft.push_back(*p);
            }
            SDL_free(clip);
            MarkDirty();
        }
    }

    void SubmitCatalogUrl(std::string_view rawUrl, bool allowLocal)
    {
        // Trim surrounding spaces.
        while (!rawUrl.empty() && (rawUrl.front() == ' ' || rawUrl.front() == '\t')) rawUrl.remove_prefix(1);
        while (!rawUrl.empty() && (rawUrl.back() == ' ' || rawUrl.back() == '\t')) rawUrl.remove_suffix(1);
        StopUrlInput();
        std::string error;
        if (!mods::IsValidCatalogUrl(rawUrl, allowLocal, error)) {
            SetArchiveStatus("OBL_MODS_CAT_ERROR", error, true);
            return;
        }
        if (rawUrl == mods::kOfficialCatalogUrl) {
            m_catalogStore.SetEnabled(mods::kOfficialCatalogUrl, true);
            SaveCatalogs();
            RebuildCatalogRows();
            BeginFetchCatalog(std::string(rawUrl), false);
            return;
        }
        // Adding a third-party online source always shows a notice and requires confirmation first.
        m_pendingAddCatalog = PendingAddCatalog{std::string(rawUrl), allowLocal};
        RebuildTargets();
        MarkDirty();
    }

    void ConfirmCatalogAction()
    {
        if (m_pendingAddCatalog) {
            const PendingAddCatalog req = std::exchange(m_pendingAddCatalog, std::nullopt).value();
            BeginFetchCatalog(req.url, req.allowLocal);
            return;
        }
        if (m_pendingKeyChange) {
            const PendingKeyChange kc = std::exchange(m_pendingKeyChange, std::nullopt).value();
            std::string error;
            const auto status = m_catalogStore.AddOrUpdate(kc.url, kc.catalog, kc.allowLocal, true, error);
            if (status == mods::CatalogStore::UpdateStatus::Failed) {
                SetArchiveStatus("OBL_MODS_CAT_ERROR", error, true);
            } else {
                SaveCatalogs();
                RebuildCatalogRows();
                SetArchiveStatus("OBL_MODS_CAT_ADDED", kc.catalog.title, false);
            }
            return;
        }
        ConfirmArchiveChange();
    }

    void CancelConfirmAction()
    {
        m_pendingReplace.reset();
        m_pendingRemove.clear();
        m_pendingAddCatalog.reset();
        m_pendingKeyChange.reset();
        m_downloadQueue.clear();
        RebuildTargets();
        MarkDirty();
    }

    void BeginFetchCatalog(const std::string& url, bool allowLocal)
    {
        if (m_activeFetch.handle) return;
        m_activeFetch = {net::StartFetch(url, mods::kMaxCatalogBytes, allowLocal), url, allowLocal, false, {}};
        SetArchiveStatus("OBL_MODS_CAT_FETCHING", url, false);
    }

    void RefreshAllCatalogs()
    {
        if (m_activeFetch.handle) return;
        m_refreshQueue.clear();
        for (const mods::StoredCatalog& cat : m_catalogStore.Catalogs()) {
            if (!cat.enabled) continue;
            m_refreshQueue.push_back({cat.url, cat.allowLocal});
        }
        StartNextRefresh();
    }

    void StartNextRefresh()
    {
        if (m_activeFetch.handle || m_refreshQueue.empty()) return;
        const auto next = m_refreshQueue.front();
        m_refreshQueue.erase(m_refreshQueue.begin());
        BeginFetchCatalog(next.url, next.allowLocal);
    }

    void ToggleCatalogAt(size_t index)
    {
        const auto& list = m_catalogStore.Catalogs();
        if (index >= list.size()) return;
        const std::string url = list[index].url;
        const bool nextEnabled = !list[index].enabled;
        const bool needsFetch = nextEnabled && list[index].mods.empty() && !os::HasEnv("OBL_SCRIPT");
        m_catalogStore.SetEnabled(url, nextEnabled);
        SaveCatalogs();
        RebuildCatalogRows();
        if (needsFetch) BeginFetchCatalog(url, list[index].allowLocal);
    }

    void RemoveCatalogAt(size_t index)
    {
        const auto& list = m_catalogStore.Catalogs();
        if (index >= list.size() || list[index].official) return;
        const std::string title = list[index].title;
        const std::string url = list[index].url;
        m_catalogStore.Remove(url);
        SaveCatalogs();
        RebuildCatalogRows();
        SetArchiveStatus("OBL_MODS_CAT_REMOVED", title.empty() ? url : title, false);
    }

    void InstallFromCatalog(const std::string& modId)
    {
        if (m_activeFetch.handle) return;
        const auto row = std::find_if(m_catalogRows.begin(), m_catalogRows.end(), [&](const CatalogRow& r) { return r.entry.id == modId; });
        if (row == m_catalogRows.end() || OfferFor(row->entry) == mods::CatalogOffer::UpToDate) return;
        std::vector<mods::CatalogEntry> available;
        available.reserve(m_catalogRows.size());
        for (const CatalogRow& r : m_catalogRows) available.push_back(r.entry);

        std::vector<std::string> installedIds;
        bool alreadyInstalled = false;
        if (m_list) {
            for (const mods::Entry& e : m_list->Entries()) {
                installedIds.push_back(e.mod.id);
                if (e.mod.id == modId) alreadyInstalled = true;
            }
        }
        if (alreadyInstalled && !CanChangeFiles(modId)) return;

        std::vector<mods::CatalogEntry> order;
        std::string error;
        if (!mods::ResolveInstallOrder(modId, available, installedIds, true, order, error)) {
            SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
            return;
        }
        m_downloadQueue = std::move(order);
        StartNextQueuedDownload();
    }

    void StartNextQueuedDownload()
    {
        if (m_activeFetch.handle || m_downloadQueue.empty()) return;
        const mods::CatalogEntry entry = m_downloadQueue.front();
        m_downloadQueue.erase(m_downloadQueue.begin());
        const bool isLocal = entry.url.starts_with("file://");
        m_activeFetch = {net::StartFetch(entry.url, mods::kMaxModZipBytes, isLocal), entry.url, isLocal, true, entry};
        SetArchiveStatus("OBL_MODS_CAT_FETCHING", entry.id, false);
    }

    void PollCatalogFetches()
    {
        if (!m_activeFetch.handle) return;
        net::FetchResult res;
        if (!net::PollFetch(m_activeFetch.handle, res)) return;
        const ActiveCatalogFetch finished = std::exchange(m_activeFetch, {});

        if (!res.ok) {
            m_downloadQueue.clear();
            if (!finished.isModZip && finished.url == mods::kOfficialCatalogUrl) {
                OBL_LOG("mods screen: official source unavailable: %s", res.error.c_str());
                g_statusText = Lookup("OBL_MODS_CAT_OFFICIAL_UNAVAILABLE");
                g_statusIsError = false;
                MarkDirty();
            } else {
                SetArchiveStatus(finished.isModZip ? "OBL_MODS_INSTALL_ERROR" : "OBL_MODS_CAT_ERROR", res.error, true);
            }
            StartNextRefresh();
            return;
        }

        if (finished.isModZip) {
            std::string error;
            if (!mods::VerifyCatalogDownload(res.bytes, finished.modEntry, error)) {
                m_downloadQueue.clear();
                SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
                return;
            }
            mods::ModArchive archive;
            if (!archive.Read(res.bytes, pack_path::SelectedId(), error)) {
                m_downloadQueue.clear();
                SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
                return;
            }
            if (archive.Id() != finished.modEntry.id) {
                m_downloadQueue.clear();
                SetArchiveStatus("OBL_MODS_INSTALL_ERROR", "archive mod id does not match catalog entry", true);
                return;
            }
            const auto writeRes = archive.Write(GameDirectory(), false, error);
            if (writeRes == mods::ModArchive::Result::AlreadyInstalled) {
                if (!CanChangeFiles(archive.Id())) {
                    m_downloadQueue.clear();
                    return;
                }
                m_pendingReplace.emplace(std::move(archive));
                RebuildTargets();
                MarkDirty();
            } else if (writeRes == mods::ModArchive::Result::Installed) {
                OnInstalled(archive.Id());
                StartNextQueuedDownload();
            } else {
                m_downloadQueue.clear();
                SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
            }
            return;
        }

        // Catalog index response.
        const std::string_view text(std::bit_cast<const char*>(res.bytes.data()), res.bytes.size());
        mods::Catalog parsed;
        std::string error;
        if (!mods::ParseCatalog(text, finished.url, finished.allowLocal, parsed, error)) {
            if (finished.url == mods::kOfficialCatalogUrl) {
                OBL_LOG("mods screen: official source parse failed: %s", error.c_str());
                g_statusText = Lookup("OBL_MODS_CAT_OFFICIAL_UNAVAILABLE");
                g_statusIsError = false;
                MarkDirty();
            } else {
                SetArchiveStatus("OBL_MODS_CAT_ERROR", error, true);
            }
            StartNextRefresh();
            return;
        }
        const auto status = m_catalogStore.AddOrUpdate(finished.url, parsed, finished.allowLocal, false, error);
        if (status == mods::CatalogStore::UpdateStatus::KeyChanged) {
            OBL_LOG("mods screen: catalog key changed for %s", finished.url.c_str());
            m_pendingKeyChange = PendingKeyChange{finished.url, finished.allowLocal, std::move(parsed)};
            RebuildTargets();
            MarkDirty();
            return;
        }
        if (status == mods::CatalogStore::UpdateStatus::Failed) {
            SetArchiveStatus("OBL_MODS_CAT_ERROR", error, true);
            StartNextRefresh();
            return;
        }
        SaveCatalogs();
        RebuildCatalogRows();
        SetArchiveStatus("OBL_MODS_CAT_ADDED", parsed.title, false);
        StartNextRefresh();
    }

    void OnHelp()
    {
        OBL_LOG("mods screen: help %s", kHelpUrl);
        if (os::HasEnv("OBL_SCRIPT")) return;
        if (!SDL_OpenURL(kHelpUrl)) {
            g_statusText = Format(Lookup("OBL_MODS_HELP_FAILED"), {Utf8ToEngine(kHelpUrl)});
            g_statusIsError = true;
            MarkDirty();
        }
    }

    void RebuildTargets()
    {
        m_targets.clear();
        const int footerY = m_y1 - 44;
        if (m_editingUrl) {
            m_targets.push_back({"catalog.paste", m_x0 + 506, footerY + 6, m_x0 + 586, footerY + 38, [this] { PasteIntoUrlDraft(); }});
            m_targets.push_back(
                    {"catalog.url_ok", m_x0 + 592, footerY + 6, m_x0 + 684, footerY + 38, [this] { SubmitCatalogUrl(m_urlDraft, false); }});
            m_targets.push_back({"catalog.url_cancel", m_x0 + 690, footerY + 6, m_x0 + 784, footerY + 38, [this] {
                                     StopUrlInput();
                                     RebuildTargets();
                                     MarkDirty();
                                 }});
            return;
        }
        if (IsConfirming()) {
            const char* confirmName =
                    m_pendingKeyChange ? "catalog.trust_key" : (m_pendingAddCatalog ? "catalog.confirm" : "archive.confirm");
            const char* cancelName = (m_pendingKeyChange || m_pendingAddCatalog) ? "catalog.cancel" : "archive.cancel";
            m_targets.push_back({confirmName, m_x0 + 570, footerY + 6, m_x0 + 672, footerY + 38, [this] { ConfirmCatalogAction(); }});
            m_targets.push_back({cancelName, m_x0 + 680, footerY + 6, m_x0 + 784, footerY + 38, [this] { CancelConfirmAction(); }});
            return;
        }
        m_targets.push_back({"catalogs", m_x0 + 150, m_y0 + 8, m_x0 + 310, m_y0 + 40, [this] {
                                 m_catalogView = !m_catalogView;
                                 if (m_catalogView && !os::HasEnv("OBL_SCRIPT")) {
                                     const mods::StoredCatalog* official = m_catalogStore.Find(mods::kOfficialCatalogUrl);
                                     if (official && official->enabled && official->mods.empty()) {
                                         BeginFetchCatalog(mods::kOfficialCatalogUrl, false);
                                     }
                                 }
                                 RebuildTargets();
                                 MarkDirty();
                             }});
        if (!m_catalogView) {
            m_targets.push_back({"install", m_x0 + 318, m_y0 + 8, m_x0 + 520, m_y0 + 40, [this] { OnInstall(); }});
        }
        m_targets.push_back({"back", m_x0 + 696, m_y0 + 8, m_x0 + 784, m_y0 + 40, [this] { m_pendingBack = true; }});
        m_targets.push_back({"help", m_x1 - 40, footerY + 6, m_x1 - 8, footerY + 38, [this] { OnHelp(); }});

        if (m_catalogView) {
            const size_t start = static_cast<size_t>(m_catalogPage * kPageSize);
            const size_t end = std::min(m_catalogRows.size(), start + static_cast<size_t>(kPageSize));
            for (size_t i = start; i < end; ++i) {
                const int rowY = m_y0 + 58 + static_cast<int>(i - start) * 48;
                const std::string id = m_catalogRows[i].entry.id;
                m_targets.push_back({"select." + id, m_x0 + 12, rowY, m_x0 + 394, rowY + 44, [this, id] {
                                         m_selectedCatalogModId = id;
                                         RebuildTargets();
                                         MarkDirty();
                                     }});
            }
            const int pages = CatalogPageCount();
            if (pages > 1) {
                if (m_catalogPage > 0)
                    m_targets.push_back({"page.prev", m_x0 + 14, m_y0 + 504, m_x0 + 124, m_y0 + 536, [this] { ChangeCatalogPage(-1); }});
                if (m_catalogPage + 1 < pages)
                    m_targets.push_back({"page.next", m_x0 + 282, m_y0 + 504, m_x0 + 392, m_y0 + 536, [this] { ChangeCatalogPage(1); }});
            }
            // Selected catalog mod Install / Update button.
            const auto selIt = std::find_if(
                    m_catalogRows.begin(), m_catalogRows.end(), [&](const CatalogRow& r) { return r.entry.id == m_selectedCatalogModId; });
            if (selIt != m_catalogRows.end() && OfferFor(selIt->entry) != mods::CatalogOffer::UpToDate) {
                const std::string id = selIt->entry.id;
                m_targets.push_back(
                        {"catalog.install." + id, m_x0 + 414, m_y0 + 210, m_x0 + 570, m_y0 + 242, [this, id] { InstallFromCatalog(id); }});
            }
            // Added catalogs management buttons.
            m_targets.push_back({"catalog.add", m_x0 + 560, m_y0 + 264, m_x0 + 676, m_y0 + 292, [this] { StartUrlInput(); }});
            m_targets.push_back({"catalog.refresh", m_x0 + 682, m_y0 + 264, m_x0 + 780, m_y0 + 292, [this] { RefreshAllCatalogs(); }});
            const auto& cats = m_catalogStore.Catalogs();
            const size_t maxShown = std::min<size_t>(cats.size(), 6);
            for (size_t i = 0; i < maxShown; ++i) {
                const int y = m_y0 + 300 + static_cast<int>(i) * 38;
                if (cats[i].official) {
                    m_targets.push_back({"catalog.toggle." + std::to_string(i), m_x0 + 706, y + 4, m_x0 + 780, y + 32,
                            [this, i] { ToggleCatalogAt(i); }});
                } else {
                    m_targets.push_back({"catalog.remove." + std::to_string(i), m_x0 + 706, y + 4, m_x0 + 780, y + 32,
                            [this, i] { RemoveCatalogAt(i); }});
                }
            }
            return;
        }
        if (!m_list) return;

        const auto& entries = m_list->Entries();
        const size_t activeCount = m_list->ActiveCount();
        const size_t start = static_cast<size_t>(g_page * kPageSize);
        const size_t end = std::min(entries.size(), start + static_cast<size_t>(kPageSize));

        for (size_t i = start; i < end; ++i) {
            const mods::Entry& entry = entries[i];
            const int rowY = m_y0 + 58 + static_cast<int>(i - start) * 48;
            const std::string id = entry.mod.id;
            m_targets.push_back({"toggle." + id, m_x0 + 18, rowY + 6, m_x0 + 70, rowY + 38, [this, id] { OnToggle(id); }});
            m_targets.push_back({"select." + id, m_x0 + 74, rowY + 2, m_x0 + (entry.on ? 292 : 354), rowY + 42, [this, id] {
                                     g_selectedId = id;
                                     RebuildTargets();
                                     MarkDirty();
                                 }});
            if (entry.on) {
                if (i > 0) m_targets.push_back({"up." + id, m_x0 + 296, rowY + 6, m_x0 + 324, rowY + 38, [this, id] { OnMove(id, -1); }});
                if (i + 1 < activeCount)
                    m_targets.push_back({"down." + id, m_x0 + 328, rowY + 6, m_x0 + 356, rowY + 38, [this, id] { OnMove(id, 1); }});
            }
            m_targets.push_back({"remove." + id, m_x0 + 360, rowY + 6, m_x0 + 388, rowY + 38, [this, id] { OnRemove(id); }});
        }

        const int pages = PageCount();
        if (pages > 1) {
            if (g_page > 0) m_targets.push_back({"page.prev", m_x0 + 14, m_y0 + 504, m_x0 + 124, m_y0 + 536, [this] { ChangePage(-1); }});
            if (g_page + 1 < pages)
                m_targets.push_back({"page.next", m_x0 + 282, m_y0 + 504, m_x0 + 392, m_y0 + 536, [this] { ChangePage(1); }});
        }

        // Settings targets for the selected active mod.
        const mods::Entry* selected = m_list->Find(g_selectedId);
        if (selected && selected->on && selected->mod.valid && selected->problem == mods::Problem::None &&
                !selected->mod.manifest.settings.empty()) {
            const auto& active = mod_pack::Active();
            const auto live = std::find_if(active.begin(), active.end(), [&](const auto& m) { return m.manifest.id == g_selectedId; });
            if (live != active.end()) {
                const auto& fields = selected->mod.manifest.settings;
                const size_t count = std::min(fields.size(), static_cast<size_t>(kMaxSettingRows));
                for (size_t i = 0; i < count; ++i) {
                    const settings_schema::Field field = fields[i];
                    const std::string modId = selected->mod.id;
                    const int y = m_y0 + 254 + static_cast<int>(i) * 28;
                    m_targets.push_back({"setting." + field.id + ".prev", m_x0 + 610, y, m_x0 + 638, y + 24,
                            [this, modId, field] { OnStepSetting(modId, field, -1); }});
                    m_targets.push_back({"setting." + field.id + ".next", m_x0 + 752, y, m_x0 + 780, y + 24,
                            [this, modId, field] { OnStepSetting(modId, field, 1); }});
                }
            }
        }
    }

    void DrawButton(int x0, int y0, int x1, int y1, const Char16* label, uint32_t fill, uint32_t ink) const
    {
        own::Box(x0, y0, x1, y1, m_round ? std::min((y1 - y0) / 2, 16) : 0, fill, m_pal.border);
        const int y = (y0 + y1) / 2 - own::InkCentre(1);
        own::Text(1, x0 + 4, y, x1 - x0 - 8, label, ink, 2);
    }

    void DrawListColumn() const
    {
        const int x0 = m_x0 + 12;
        const int x1 = m_x0 + 394;
        if (!m_list || m_list->Entries().empty()) {
            own::Box(x0, m_y0 + 58, x1, m_y0 + 116, m_round, m_pal.foam, m_pal.border);
            own::TextWrapped(1, x0 + 10, m_y0 + 68, x1 - x0 - 20, 44, Lookup("OBL_MODS_NONE").c_str(), m_pal.ink, 0);
            return;
        }
        const auto& entries = m_list->Entries();
        const size_t activeCount = m_list->ActiveCount();
        const size_t start = static_cast<size_t>(g_page * kPageSize);
        const size_t end = std::min(entries.size(), start + static_cast<size_t>(kPageSize));

        for (size_t i = start; i < end; ++i) {
            const mods::Entry& entry = entries[i];
            const int rowY = m_y0 + 58 + static_cast<int>(i - start) * 48;
            const bool selected = entry.mod.id == g_selectedId;
            const bool hasProblem = entry.problem != mods::Problem::None;
            own::Box(x0, rowY, x1, rowY + 44, m_round, selected ? m_pal.foam : m_pal.white,
                    hasProblem ? m_pal.red : (selected ? m_pal.sea : m_pal.border));

            const uint32_t toggleFill = entry.on ? m_pal.green : (hasProblem ? m_pal.grey : m_pal.seaDark);
            DrawButton(m_x0 + 18, rowY + 6, m_x0 + 70, rowY + 38, Lookup(entry.on ? "OBL_MODS_ON" : "OBL_MODS_OFF").c_str(), toggleFill,
                    m_pal.white);

            const std::string& title = (entry.mod.valid && !entry.mod.manifest.title.empty()) ? entry.mod.manifest.title : entry.mod.id;
            const int textW = entry.on ? 216 : 278;
            own::Text(1, m_x0 + 76, rowY + 6, textW, Utf8ToEngine(title).c_str(), hasProblem ? m_pal.red : m_pal.ink, 0);

            if (hasProblem) {
                own::Text(0, m_x0 + 76, rowY + 24, textW, ProblemText(entry.problem, entry.detail).c_str(), m_pal.red, 0);
            } else {
                std::u16string sub = Utf8ToEngine(entry.mod.id + "  " + entry.mod.manifest.modVersion + "  ");
                sub += ScopeText(entry.mod.manifest.scope);
                own::Text(0, m_x0 + 76, rowY + 24, textW, sub.c_str(), m_pal.grey, 0);
            }

            if (entry.on) {
                const bool canUp = i > 0;
                const bool canDown = i + 1 < activeCount;
                DrawButton(m_x0 + 296, rowY + 6, m_x0 + 324, rowY + 38, u"^", canUp ? m_pal.sea : m_pal.sand,
                        canUp ? m_pal.white : m_pal.grey);
                DrawButton(m_x0 + 328, rowY + 6, m_x0 + 356, rowY + 38, u"v", canDown ? m_pal.sea : m_pal.sand,
                        canDown ? m_pal.white : m_pal.grey);
            }
            DrawButton(m_x0 + 360, rowY + 6, m_x0 + 388, rowY + 38, u"X", entry.on ? m_pal.sand : m_pal.seaDark,
                    entry.on ? m_pal.grey : m_pal.white);
        }

        const int pages = PageCount();
        if (pages > 1) {
            DrawButton(m_x0 + 14, m_y0 + 504, m_x0 + 124, m_y0 + 536, Lookup("OBL_MODS_PREVIOUS").c_str(),
                    g_page > 0 ? m_pal.sea : m_pal.sand, g_page > 0 ? m_pal.white : m_pal.grey);
            const std::u16string pageLabel = Utf8ToEngine(std::to_string(g_page + 1) + " / " + std::to_string(pages));
            own::Text(1, m_x0 + 130, m_y0 + 512, 146, pageLabel.c_str(), m_pal.ink, 2);
            DrawButton(m_x0 + 282, m_y0 + 504, m_x0 + 392, m_y0 + 536, Lookup("OBL_MODS_NEXT").c_str(),
                    g_page + 1 < pages ? m_pal.sea : m_pal.sand, g_page + 1 < pages ? m_pal.white : m_pal.grey);
        }
    }

    void DrawRightColumn() const
    {
        const int x0 = m_x0 + 404;
        const int x1 = m_x0 + 788;
        const int innerW = x1 - x0 - 20;

        // Top card: selected mod info.
        own::Box(x0, m_y0 + 58, x1, m_y0 + 216, m_round, m_pal.foam, m_pal.border);
        const mods::Entry* selected = m_list ? m_list->Find(g_selectedId) : nullptr;
        if (selected) {
            const std::string& title =
                    (selected->mod.valid && !selected->mod.manifest.title.empty()) ? selected->mod.manifest.title : selected->mod.id;
            own::Text(2, x0 + 10, m_y0 + 66, innerW, Utf8ToEngine(title).c_str(), m_pal.ink, 0);
            int y = m_y0 + 90;
            if (selected->mod.valid) {
                const auto& m = selected->mod.manifest;
                own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_VERSION"), {Utf8ToEngine(m.modVersion)}).c_str(), m_pal.ink, 0);
                y += 16;
                own::Text(
                        0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_PACKS"), {Utf8ToEngine(Join(m.packs, ", "))}).c_str(), m_pal.ink, 0);
                y += 16;
                own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_SCOPE"), {ScopeText(m.scope)}).c_str(), m_pal.ink, 0);
                y += 16;
                if (!m.takesOver.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_TAKES_OVER"), {Utf8ToEngine(Join(m.takesOver, ", "))}).c_str(),
                            m_pal.ink, 0);
                    y += 16;
                }
                if (!m.actions.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_ACTIONS"), {Utf8ToEngine(FormatActions(m.actions))}).c_str(),
                            m_pal.ink, 0);
                    y += 16;
                }
                if (!m.bindings.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW,
                            Format(Lookup("OBL_MODS_BINDINGS"), {Utf8ToEngine(FormatBindings(m.bindings))}).c_str(), m_pal.ink, 0);
                    y += 16;
                }
                if (!m.requiredMods.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW,
                            Format(Lookup("OBL_MODS_REQUIRES"), {Utf8ToEngine(Join(m.requiredMods, ", "))}).c_str(), m_pal.ink, 0);
                    y += 16;
                }
                if (!m.conflictingMods.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW,
                            Format(Lookup("OBL_MODS_INCOMPATIBLE"), {Utf8ToEngine(Join(m.conflictingMods, ", "))}).c_str(), m_pal.ink, 0);
                    y += 16;
                }
            }
            if (selected->problem != mods::Problem::None) {
                own::Text(1, x0 + 10, std::min(y + 4, m_y0 + 194), innerW, ProblemText(selected->problem, selected->detail).c_str(),
                        m_pal.red, 0);
            }
        }

        // Middle card: settings of the selected mod.
        own::Box(x0, m_y0 + 224, x1, m_y0 + 402, m_round, m_pal.foam, m_pal.border);
        own::Text(1, x0 + 10, m_y0 + 232, innerW, Lookup("OBL_MODS_SETTINGS").c_str(), m_pal.seaDark, 0);
        if (!selected || !selected->mod.valid || selected->mod.manifest.settings.empty()) {
            own::Text(0, x0 + 10, m_y0 + 258, innerW, Lookup("OBL_MODS_NO_SETTINGS").c_str(), m_pal.grey, 0);
        } else {
            const auto& active = mod_pack::Active();
            const auto live = std::find_if(active.begin(), active.end(), [&](const auto& m) { return m.manifest.id == g_selectedId; });
            if (!selected->on || selected->problem != mods::Problem::None || live == active.end()) {
                own::Text(0, x0 + 10, m_y0 + 258, innerW, Lookup("OBL_MODS_SETTINGS_OFF").c_str(), m_pal.grey, 0);
            } else {
                const auto& fields = selected->mod.manifest.settings;
                const size_t count = std::min(fields.size(), static_cast<size_t>(kMaxSettingRows));
                for (size_t i = 0; i < count; ++i) {
                    const settings_schema::Field& field = fields[i];
                    const int y = m_y0 + 254 + static_cast<int>(i) * 28;
                    own::Text(0, x0 + 10, y + 6, 190, Utf8ToEngine(field.label).c_str(), m_pal.ink, 0);
                    DrawButton(m_x0 + 610, y, m_x0 + 638, y + 24, u"<", m_pal.sea, m_pal.white);
                    own::Box(m_x0 + 640, y, m_x0 + 750, y + 24, m_round, m_pal.white, m_pal.border);
                    const std::string current = mods::ModSettingValue(*live, field);
                    std::u16string shown;
                    if (field.kind == settings_schema::Kind::Toggle)
                        shown = Lookup(settings_schema::IsOn(current) ? "OBL_MODS_ON" : "OBL_MODS_OFF");
                    else
                        shown = Utf8ToEngine(settings_schema::Display(field, current));
                    own::Text(0, m_x0 + 644, y + 6, 102, shown.c_str(), m_pal.ink, 2);
                    DrawButton(m_x0 + 752, y, m_x0 + 780, y + 24, u">", m_pal.sea, m_pal.white);
                }
            }
        }

        // Bottom card: conflicts of the active list.
        own::Box(x0, m_y0 + 410, x1, m_y0 + 548, m_round, m_pal.foam, m_pal.border);
        own::Text(1, x0 + 10, m_y0 + 418, innerW, Lookup("OBL_MODS_CONFLICTS").c_str(), m_pal.seaDark, 0);
        if (!m_list || m_list->Conflicts().empty()) {
            own::Text(0, x0 + 10, m_y0 + 444, innerW, Lookup("OBL_MODS_NO_CONFLICTS").c_str(), m_pal.grey, 0);
        } else {
            own::Text(0, x0 + 10, m_y0 + 438, innerW, Lookup("OBL_MODS_CONFLICT_HINT").c_str(), m_pal.grey, 0);
            const auto& conflicts = m_list->Conflicts();
            const size_t count = std::min(conflicts.size(), static_cast<size_t>(kMaxConflictRows));
            for (size_t i = 0; i < count; ++i) {
                const mods::Conflict& c = conflicts[i];
                const std::u16string line = c.winner.empty()
                        ? Utf8ToEngine(c.what)
                        : Format(Lookup("OBL_MODS_CONFLICT_VALUE"), {Utf8ToEngine(c.winner), Utf8ToEngine(c.loser), Utf8ToEngine(c.what)});
                own::Text(0, x0 + 10, m_y0 + 458 + static_cast<int>(i) * 20, innerW, line.c_str(), m_pal.red, 0);
            }
        }
    }

    void DrawCatalogListColumn() const
    {
        const int x0 = m_x0 + 12;
        const int x1 = m_x0 + 394;
        if (m_catalogRows.empty()) {
            own::Box(x0, m_y0 + 58, x1, m_y0 + 126, m_round, m_pal.foam, m_pal.border);
            const auto& cats = m_catalogStore.Catalogs();
            const bool anyEnabled = std::any_of(cats.begin(), cats.end(), [](const mods::StoredCatalog& c) { return c.enabled; });
            const char* key = anyEnabled ? "OBL_MODS_CAT_NO_MODS" : "OBL_MODS_CAT_NONE";
            own::TextWrapped(1, x0 + 10, m_y0 + 68, x1 - x0 - 20, 52, Lookup(key).c_str(), m_pal.ink, 0);
            return;
        }
        const size_t start = static_cast<size_t>(m_catalogPage * kPageSize);
        const size_t end = std::min(m_catalogRows.size(), start + static_cast<size_t>(kPageSize));
        for (size_t i = start; i < end; ++i) {
            const CatalogRow& row = m_catalogRows[i];
            const int rowY = m_y0 + 58 + static_cast<int>(i - start) * 48;
            const bool selected = row.entry.id == m_selectedCatalogModId;
            own::Box(x0, rowY, x1, rowY + 44, m_round, selected ? m_pal.foam : m_pal.white, selected ? m_pal.sea : m_pal.border);
            own::Text(1, x0 + 10, rowY + 6, 360, Utf8ToEngine(row.entry.title).c_str(), m_pal.ink, 0);

            const mods::CatalogOffer offer = OfferFor(row.entry);
            std::u16string sub = Utf8ToEngine(row.entry.id + "  " + row.entry.modVersion + "  ");
            sub += ScopeText(row.entry.scope);
            if (offer != mods::CatalogOffer::Install) {
                sub += u"  (";
                sub += Lookup(offer == mods::CatalogOffer::UpToDate ? "OBL_MODS_CAT_UP_TO_DATE" : "OBL_MODS_CAT_UPDATE");
                sub += u")";
            }
            own::Text(0, x0 + 10, rowY + 24, 360, sub.c_str(), m_pal.grey, 0);
        }
        const int pages = CatalogPageCount();
        if (pages > 1) {
            DrawButton(m_x0 + 14, m_y0 + 504, m_x0 + 124, m_y0 + 536, Lookup("OBL_MODS_PREVIOUS").c_str(),
                    m_catalogPage > 0 ? m_pal.sea : m_pal.sand, m_catalogPage > 0 ? m_pal.white : m_pal.grey);
            const std::u16string pageLabel = Utf8ToEngine(std::to_string(m_catalogPage + 1) + " / " + std::to_string(pages));
            own::Text(1, m_x0 + 130, m_y0 + 512, 146, pageLabel.c_str(), m_pal.ink, 2);
            DrawButton(m_x0 + 282, m_y0 + 504, m_x0 + 392, m_y0 + 536, Lookup("OBL_MODS_NEXT").c_str(),
                    m_catalogPage + 1 < pages ? m_pal.sea : m_pal.sand, m_catalogPage + 1 < pages ? m_pal.white : m_pal.grey);
        }
    }

    void DrawCatalogRightColumn() const
    {
        const int x0 = m_x0 + 404;
        const int x1 = m_x0 + 788;
        const int innerW = x1 - x0 - 20;

        // Top card: selected catalog mod details and Install/Update button.
        own::Box(x0, m_y0 + 58, x1, m_y0 + 250, m_round, m_pal.foam, m_pal.border);
        const auto selIt = std::find_if(
                m_catalogRows.begin(), m_catalogRows.end(), [&](const CatalogRow& r) { return r.entry.id == m_selectedCatalogModId; });
        if (selIt != m_catalogRows.end()) {
            const auto& e = selIt->entry;
            own::Text(2, x0 + 10, m_y0 + 66, innerW, Utf8ToEngine(e.title).c_str(), m_pal.ink, 0);
            int y = m_y0 + 90;
            own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_VERSION"), {Utf8ToEngine(e.modVersion)}).c_str(), m_pal.ink, 0);
            y += 16;
            own::Text(
                    0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_CAT_FROM"), {Utf8ToEngine(selIt->catalogTitle)}).c_str(), m_pal.ink, 0);
            y += 16;
            own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_SCOPE"), {ScopeText(e.scope)}).c_str(), m_pal.ink, 0);
            y += 16;
            own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_CAT_LICENSE"), {Utf8ToEngine(e.license)}).c_str(), m_pal.ink, 0);
            y += 16;
            if (!e.requiresMods.empty()) {
                own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_REQUIRES"), {Utf8ToEngine(Join(e.requiresMods, ", "))}).c_str(),
                        m_pal.ink, 0);
            }
            const mods::CatalogOffer offer = OfferFor(e);
            if (offer == mods::CatalogOffer::UpToDate) {
                DrawButton(
                        m_x0 + 414, m_y0 + 210, m_x0 + 570, m_y0 + 242, Lookup("OBL_MODS_CAT_UP_TO_DATE").c_str(), m_pal.sand, m_pal.grey);
            } else {
                const char* btnKey = offer == mods::CatalogOffer::Update ? "OBL_MODS_CAT_UPDATE" : "OBL_MODS_CAT_INSTALL";
                DrawButton(m_x0 + 414, m_y0 + 210, m_x0 + 570, m_y0 + 242, Lookup(btnKey).c_str(), m_pal.sea, m_pal.white);
            }
        }

        // Bottom card: added catalogs list + Add / Refresh / Remove.
        own::Box(x0, m_y0 + 258, x1, m_y0 + 548, m_round, m_pal.foam, m_pal.border);
        own::Text(1, x0 + 10, m_y0 + 270, 140, Lookup("OBL_MODS_CATALOGS").c_str(), m_pal.seaDark, 0);
        DrawButton(m_x0 + 560, m_y0 + 264, m_x0 + 676, m_y0 + 292, Lookup("OBL_MODS_CAT_ADD").c_str(), m_pal.sea, m_pal.white);
        DrawButton(m_x0 + 682, m_y0 + 264, m_x0 + 780, m_y0 + 292, Lookup("OBL_MODS_CAT_REFRESH").c_str(), m_pal.seaDark, m_pal.white);

        const auto& cats = m_catalogStore.Catalogs();
        if (cats.empty()) {
            own::TextWrapped(0, x0 + 10, m_y0 + 306, innerW, 60, Lookup("OBL_MODS_CAT_NONE").c_str(), m_pal.grey, 0);
        } else {
            const size_t maxShown = std::min<size_t>(cats.size(), 6);
            for (size_t i = 0; i < maxShown; ++i) {
                const int y = m_y0 + 300 + static_cast<int>(i) * 38;
                own::Box(x0 + 8, y, x1 - 8, y + 34, m_round, m_pal.white, m_pal.border);
                std::u16string title = Utf8ToEngine(cats[i].title);
                if (!cats[i].official) {
                    title += u" (";
                    title += Lookup("OBL_MODS_CAT_THIRD_PARTY");
                    title += u")";
                }
                own::Text(0, x0 + 14, y + 4, 280, title.c_str(), cats[i].enabled ? m_pal.ink : m_pal.grey, 0);
                own::Text(0, x0 + 14, y + 18, 280, Utf8ToEngine(cats[i].url).c_str(), m_pal.grey, 0);
                if (cats[i].official) {
                    DrawButton(m_x0 + 706, y + 4, m_x0 + 780, y + 32, Lookup(cats[i].enabled ? "OBL_MODS_ON" : "OBL_MODS_OFF").c_str(),
                            cats[i].enabled ? m_pal.green : m_pal.seaDark, m_pal.white);
                } else {
                    DrawButton(m_x0 + 706, y + 4, m_x0 + 780, y + 32, Lookup("OBL_MODS_REMOVE").c_str(), m_pal.seaDark, m_pal.white);
                }
            }
        }
    }

    void EnsureVisible(std::string_view id)
    {
        if (!m_list) return;
        const auto& entries = m_list->Entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].mod.id == id) {
                const int page = static_cast<int>(i / kPageSize);
                if (page != g_page) {
                    g_page = page;
                    RebuildTargets();
                    MarkDirty();
                }
                return;
            }
        }
    }

    std::unique_ptr<mods::ModList> m_list;
    std::vector<Target> m_targets;
    std::string m_droppedFile;
    std::optional<mods::ModArchive> m_pendingReplace;
    std::string m_pendingRemove;
    bool m_catalogView = false;
    bool m_editingUrl = false;
    std::string m_urlDraft;
    mods::CatalogStore m_catalogStore;
    std::vector<CatalogRow> m_catalogRows;
    std::string m_selectedCatalogModId;
    int m_catalogPage = 0;
    std::optional<PendingAddCatalog> m_pendingAddCatalog;
    std::optional<PendingKeyChange> m_pendingKeyChange;
    ActiveCatalogFetch m_activeFetch;
    std::vector<PendingAddCatalog> m_refreshQueue;
    std::vector<mods::CatalogEntry> m_downloadQueue;
    bool m_pendingBack = false;
    bool m_pendingReload = false;
};

} // namespace

void SetGameDirectory(const std::filesystem::path& gameDirectory)
{
    g_gameDirectory = gameDirectory;
}

ui::GuiWidget* CreatePanel(int x0, int y0, int x1, int y1)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the dialog's widget tree owns its children (gui_widget_virtual.h)
    return new GuiModsPanel(x0, y0, x1, y1);
}

bool QueueArchive(std::string_view path)
{
    if (!g_openPanel || !g_openPanel->m_visible || g_openPanel->IsConfirming() || !g_openPanel->m_droppedFile.empty()) return false;
    g_openPanel->m_droppedFile = path;
    return true;
}

bool QueueCatalogUrl(std::string_view url, bool allowLocal)
{
    if (!g_openPanel || !g_openPanel->m_visible || g_openPanel->IsConfirming()) return false;
    g_openPanel->m_catalogView = true;
    g_openPanel->SubmitCatalogUrl(url, allowLocal);
    return true;
}

bool IsEditingCatalogUrl()
{
    return g_openPanel && g_openPanel->m_visible && g_openPanel->m_editingUrl;
}

bool HandleTextInput(std::string_view utf8)
{
    if (!IsEditingCatalogUrl()) return false;
    for (char c : utf8) {
        if (g_openPanel->m_urlDraft.size() < 512 && static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7f)
            g_openPanel->m_urlDraft.push_back(c);
    }
    g_openPanel->MarkDirty();
    return true;
}

bool HandleKeyDown(int scancode, bool ctrl)
{
    if (!IsEditingCatalogUrl()) return false;
    if (scancode == SDL_SCANCODE_BACKSPACE) {
        if (!g_openPanel->m_urlDraft.empty()) {
            g_openPanel->m_urlDraft.pop_back();
            g_openPanel->MarkDirty();
        }
        return true;
    }
    if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER) {
        const std::string draft = g_openPanel->m_urlDraft;
        g_openPanel->SubmitCatalogUrl(draft, false);
        return true;
    }
    if (scancode == SDL_SCANCODE_ESCAPE) {
        g_openPanel->StopUrlInput();
        g_openPanel->RebuildTargets();
        g_openPanel->MarkDirty();
        return true;
    }
    if (ctrl && scancode == SDL_SCANCODE_V) {
        g_openPanel->PasteIntoUrlDraft();
        return true;
    }
    return true;
}

bool FindTarget(std::string_view name, int& x, int& y)
{
    if (!g_openPanel || !g_openPanel->m_visible) return false;
    for (std::string_view prefix : {"toggle.", "select.", "up.", "down.", "remove."}) {
        if (name.starts_with(prefix)) {
            g_openPanel->EnsureVisible(name.substr(prefix.size()));
            break;
        }
    }
    for (const Target& target : g_openPanel->m_targets) {
        if (target.name == name) {
            x = (target.x0 + target.x1) / 2;
            y = (target.y0 + target.y1) / 2;
            return true;
        }
    }
    return false;
}

} // namespace obl::gui::mods_screen
