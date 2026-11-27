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
constexpr int kMaxOverlapBanners = 2;
constexpr size_t kMaxSourceRows = 7;
// The screen's grid on the 800x600 menu: a header bar of tabs, a list on the left, a card on the right, a status bar.
constexpr int kHeaderH = 48, kFooterH = 44, kTopY = 56, kRowH = 40, kRowPitch = 44, kSectionH = 22;
constexpr int kListX0 = 12, kListX1 = 428, kCardX0 = 438, kCardX1 = 788, kBarY = 512, kCardBottom = 548;
constexpr uint32_t kAmber = 0xc8820a; // needs attention, short of an error
constexpr char16_t kDot[] = {u' ', 0x00b7, u' ', 0};

// State that survives a front-end reload when a presentation mod changes (ReloadFrontEnd destroys and rebuilds the dialog tree).
std::filesystem::path g_gameDirectory;
bool g_startupRecorded = false;
std::vector<std::string> g_startupWorldMods;
std::vector<std::string> g_desiredActive;
std::string g_selectedId;
int g_page = 0;
enum class Tab { Installed, Browse, Sources };
Tab g_tab = Tab::Installed;
std::string g_selectedCatalogId;
bool g_showTechnical = false;
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
            if (m_settle) SettleList();
            ui::GuiManager* gui = game::GetGuiManager();
            if (gui) {
                gui->CloseAllDialogs();
                gui->ShowDialog("MainMenu", nullptr, false);
            }
            return;
        }
        // The reload rebuilds the panel, so it waits for the downloads, the dialog and the settling that belong to this one.
        if (m_settle && !IsBusyWithFiles()) SettleList();
        if (m_pendingReload && !IsBusyWithFiles()) {
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
        if (!m_visible || !HitTestBase(x, y) || !delta || m_editingUrl || IsConfirming()) return false;
        if (g_tab == Tab::Installed)
            ChangePage(delta > 0 ? -1 : 1);
        else if (g_tab == Tab::Browse)
            ChangeCatalogPage(delta > 0 ? -1 : 1);
        return true;
    }

    own::Palette m_pal;
    int m_round = 0; // corner radius of boxes (0: square)
    bool m_packLook = false;
    uint32_t m_amber = kAmber;
    bool m_drawing = false; // Layout draws as well as recording the tap targets
    bool m_recordTargets = true; // false behind a dialog

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
        m_drawing = true;
        Layout();
        m_drawing = false;
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

    bool IsBusyWithFiles() const { return m_activeFetch.handle || !m_downloadQueue.empty() || IsConfirming(); }

    static const mod_pack::ActiveMod* FindLive(const std::string& id)
    {
        const auto& live = mod_pack::Active();
        const auto found = std::find_if(live.begin(), live.end(), [&](const auto& mod) { return mod.manifest.id == id; });
        return found == live.end() ? nullptr : &*found;
    }

    // A running world mod holds its files until the game starts again; a presentation mod lets go of them (ReleaseFiles).
    bool CanChangeFiles(const std::string& id)
    {
        const mod_pack::ActiveMod* live = FindLive(id);
        if (!live || live->manifest.scope != mod_pack::Scope::World) return true;
        SetArchiveStatus("OBL_MODS_ARCHIVE_ACTIVE", id, true);
        return false;
    }

    // Before the files of a running presentation mod change, the game runs without presentation mods; SettleList brings the list
    // back once the change is written, so updating or removing an active mod is one step for the player.
    bool ReleaseFiles(const std::string& id)
    {
        if (!CanChangeFiles(id)) return false;
        if (!FindLive(id)) return true;
        std::vector<std::string> worldOnly;
        for (const mod_pack::ActiveMod& mod : mod_pack::Active())
            if (mod.manifest.scope == mod_pack::Scope::World) worldOnly.push_back(mod.manifest.id);
        std::string error;
        if (!mod_pack::ActivateList(GameDirectory(), pack_path::SelectedId(), worldOnly, error)) {
            SetArchiveStatus("OBL_MODS_CANNOT_START", error, true);
            return false;
        }
        mods::RestartPresentationScripts();
        OBL_LOG("mods screen: released %s", id.c_str());
        m_settle = true;
        m_pendingReload = true; // the menus drop what the mod patched, also when it does not come back
        return true;
    }

    // Applies the list once the files of a batch are written: switches on what the batch installed (a player who installs a mod
    // means to use it), switches off an active mod that can no longer start (a mod it needs was removed or changed) and brings back
    // what ReleaseFiles let go of.
    void SettleList()
    {
        m_settle = false;
        const std::vector<std::string> installed = std::exchange(m_switchOn, {});
        if (!m_list) return;
        std::u16string message = g_statusText;
        bool error = g_statusIsError;
        std::string switchedOn;
        for (const std::string& id : installed) {
            mods::Problem why = mods::Problem::None;
            std::string detail;
            const mods::Entry* entry = m_list->Find(id);
            if (entry && entry->on) {
                OBL_LOG("mods screen: kept on after update: %s", id.c_str());
            } else if (m_list->SetOn(id, true, why, detail)) {
                switchedOn = id;
            } else {
                message = Format(Lookup("OBL_MODS_REFUSED"), {Utf8ToEngine(id), ProblemText(why, detail)});
                error = true;
                OBL_LOG("mods screen: refused %s (%s)", id.c_str(), detail.c_str());
            }
        }
        while (!m_list->CanStart()) {
            const auto& entries = m_list->Entries();
            const auto bad = std::find_if(
                    entries.begin(), entries.end(), [](const mods::Entry& e) { return e.on && e.problem != mods::Problem::None; });
            if (bad == entries.end()) break;
            const std::string id = bad->mod.id;
            message = Format(Lookup("OBL_MODS_SWITCHED_OFF"), {Utf8ToEngine(id), ProblemText(bad->problem, bad->detail)});
            error = true;
            OBL_LOG("mods screen: switched off %s (%s)", id.c_str(), bad->detail.c_str());
            if (id == switchedOn) switchedOn.clear();
            mods::Problem why = mods::Problem::None;
            std::string detail;
            m_list->SetOn(id, false, why, detail);
        }
        if (!switchedOn.empty() && !error) {
            const mods::Entry* entry = m_list->Find(switchedOn);
            const bool world = entry && entry->mod.valid && entry->mod.manifest.scope == mod_pack::Scope::World;
            message = Format(Lookup(world ? "OBL_MODS_INSTALLED_NEXT" : "OBL_MODS_INSTALLED_ON"), {Utf8ToEngine(TitleOf(switchedOn))});
            OBL_LOG("mods screen: switched on after install: %s", switchedOn.c_str());
        }
        ApplyChanges();
        if (!g_statusIsError) {
            g_statusText = message;
            g_statusIsError = error;
        }
        RebuildTargets();
        MarkDirty();
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
            OnInstalled(archive.Id(), true);
        } else {
            SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
        }
    }

    // `switchOn`: a new mod, or a replaced one that was on; SettleList switches it on after the last download of the batch.
    void OnInstalled(const std::string& id, bool switchOn)
    {
        PersistModChange(id);
        g_selectedId = id;
        if (switchOn && std::find(m_switchOn.begin(), m_switchOn.end(), id) == m_switchOn.end()) m_switchOn.push_back(id);
        m_settle = true;
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
        const std::string id = replace ? replace->Id() : remove;
        const mods::Entry* entry = m_list ? m_list->Find(id) : nullptr;
        const bool wasOn = entry && entry->on;
        if (!ReleaseFiles(id)) {
            m_downloadQueue.clear();
            return;
        }

        std::string error;
        if (replace) {
            if (replace->Write(GameDirectory(), true, error) == mods::ModArchive::Result::Installed) {
                OnInstalled(id, wasOn);
                StartNextQueuedDownload();
            } else {
                m_downloadQueue.clear();
                SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
            }
        } else if (mods::RemoveMod(GameDirectory(), id, error)) {
            PersistModChange(id);
            m_settle = m_settle || wasOn;
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
        if (g_selectedCatalogId.empty() || std::none_of(m_catalogRows.begin(), m_catalogRows.end(), [&](const CatalogRow& r) {
                return r.entry.id == g_selectedCatalogId;
            })) {
            g_selectedCatalogId = m_catalogRows.empty() ? std::string() : m_catalogRows[0].entry.id;
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
                OnInstalled(archive.Id(), true);
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

    // ---- Layout ----
    // One pass lays the screen out: it draws while m_drawing is set and records the tap targets either way, so what the player
    // sees and what a tap reaches never disagree.

    enum class Look { Primary, Secondary, Danger, DangerSolid, Disabled };

    void RebuildTargets()
    {
        m_drawing = false;
        Layout();
    }

    void Layout()
    {
        m_targets.clear();
        const own::PackLook& look = own::GetPackLook();
        m_packLook = look.available;
        const own::Palette base = look.available ? look.palette : own::Palette{};
        m_round = look.available ? 8 : 0;
        // A dialog dims the screen behind it and takes every tap. The renderer cannot blend, so the screen is drawn in darker
        // colours instead.
        const bool dialog = m_editingUrl || IsConfirming();
        m_pal = dialog ? Dimmed(base) : base;
        m_amber = dialog ? Dim(kAmber) : kAmber;
        m_recordTargets = !dialog;

        if (m_drawing) {
            for (int y = m_y0; y < m_y1; ++y) own::Fill(m_x0, y, m_x1, y + 1, BackdropAt(y));
        }
        LayoutHeader();
        switch (g_tab) {
        case Tab::Installed: LayoutInstalled(); break;
        case Tab::Browse: LayoutBrowse(); break;
        case Tab::Sources: LayoutSources(); break;
        }
        LayoutFooter();

        m_pal = base;
        m_amber = kAmber;
        m_recordTargets = true;
        if (dialog) LayoutDialog();
    }

    static uint32_t Mix(uint32_t a, uint32_t b, int step, int steps) { return LerpRgb(a, b, step, steps); }
    static uint32_t Dim(uint32_t rgb) { return LerpRgb(rgb, 0x0a1622, 11, 20); }

    static own::Palette Dimmed(own::Palette p)
    {
        for (uint32_t* c : {&p.sea, &p.seaDark, &p.sand, &p.foam, &p.ink, &p.red, &p.green, &p.grey, &p.white, &p.border}) *c = Dim(*c);
        return p;
    }

    uint32_t BackdropAt(int y) const
    {
        // Sky-like on a pack with coloured menu buttons: from the foam tint at the top to the sand tint at the bottom.
        return m_packLook ? LerpRgb(m_pal.foam, m_pal.sand, y - m_y0, std::max(1, m_y1 - m_y0 - 1)) : m_pal.sand;
    }

    void AddTarget(std::string name, int x0, int y0, int x1, int y1, std::function<void()> onClick)
    {
        if (m_recordTargets) m_targets.push_back({std::move(name), x0, y0, x1, y1, std::move(onClick)});
    }

    void Label(int font, int x, int y, int width, const std::u16string& text, uint32_t rgb, int align = 0) const
    {
        if (m_drawing && width > 0 && !text.empty()) own::Text(font, x, y, width, text.c_str(), rgb, align);
    }

    struct Colours {
        uint32_t fill, border, ink;
    };

    Colours ColoursOf(Look look) const
    {
        switch (look) {
        case Look::Primary: break;
        case Look::Secondary: return {m_pal.white, m_pal.sea, m_pal.seaDark};
        case Look::Danger: return {m_pal.white, m_pal.red, m_pal.red};
        case Look::DangerSolid: return {m_pal.red, m_pal.red, m_pal.white};
        case Look::Disabled: return {m_pal.sand, m_pal.border, m_pal.grey};
        }
        return {m_pal.seaDark, m_pal.seaDark, m_pal.white};
    }

    void Button(int x0, int y0, int x1, int y1, const std::u16string& label, Look look, std::string name = {},
            std::function<void()> onClick = {})
    {
        if (m_drawing) {
            const Colours c = ColoursOf(look);
            own::Box(x0, y0, x1, y1, m_round ? std::min((y1 - y0) / 2, 16) : 0, c.fill, c.border);
            if (!label.empty()) own::Text(1, x0 + 4, (y0 + y1) / 2 - own::InkCentre(1), x1 - x0 - 8, label.c_str(), c.ink, 2);
        }
        if (look != Look::Disabled && !name.empty()) AddTarget(std::move(name), x0, y0, x1, y1, std::move(onClick));
    }

    // The order buttons draw their arrow from pixels: font glyphs differ from pack to pack and sit off centre. The triangle is
    // 12 x 6 around the centre of the button (an even box), moved a pixel toward its tip so it looks centred.
    void ArrowButton(int x0, int y0, int x1, int y1, bool up, Look look, std::string name, std::function<void()> onClick)
    {
        Button(x0, y0, x1, y1, {}, look, std::move(name), std::move(onClick));
        if (!m_drawing) return;
        const int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2 + (up ? -1 : 1);
        const uint32_t ink = ColoursOf(look).ink;
        for (int row = 0; row < 6; ++row) {
            const int half = row + 1, y = up ? cy - 3 + row : cy + 2 - row;
            own::Fill(cx - half, y, cx + half, y + 1, ink);
        }
    }
    // A switch says "state" where a button says "action": a track that is green when on, with its knob at that end.
    void Switch(int x, int y, bool on, bool usable, std::string name, std::function<void()> onClick)
    {
        constexpr int kW = 40, kH = 20;
        if (m_drawing) {
            const uint32_t track = on ? m_pal.green : (usable ? Mix(m_pal.grey, m_pal.white, 1, 3) : Mix(m_pal.border, m_pal.white, 1, 2));
            own::Box(x, y, x + kW, y + kH, m_round ? kH / 2 : 0, track, Mix(track, m_pal.ink, 1, 5));
            const int knob = on ? x + kW - kH + 2 : x + 2;
            own::Box(knob, y + 2, knob + kH - 4, y + kH - 2, m_round ? (kH - 4) / 2 : 0, m_pal.white, Mix(track, m_pal.ink, 1, 4));
        }
        AddTarget(std::move(name), x - 6, y - 8, x + kW + 6, y + kH + 8, std::move(onClick));
    }

    void Marker(int cx, int cy, uint32_t fill) const
    {
        if (!m_drawing) return;
        own::Box(cx - 9, cy - 9, cx + 9, cy + 9, m_round ? 9 : 0, fill, fill);
        // From pixels like the arrows: a 2-pixel bar and dot, centred in the 18-pixel mark.
        own::Fill(cx - 1, cy - 5, cx + 1, cy + 2, m_pal.white);
        own::Fill(cx - 1, cy + 3, cx + 1, cy + 5, m_pal.white);
    }

    // A small pill ending at `right`; returns its left edge.
    int Badge(int right, int y, const std::u16string& text, uint32_t tone) const
    {
        const int left = right - own::TextWidth(0, text.c_str()) - 16;
        if (m_drawing) {
            own::Box(left, y, right, y + 18, m_round ? 9 : 0, Mix(tone, m_pal.white, 4, 5), Mix(tone, m_pal.white, 1, 3));
            own::Text(0, left, y + 9 - own::InkCentre(0), right - left, text.c_str(), Mix(tone, m_pal.ink, 1, 3), 2);
        }
        return left;
    }

    // Wrapped small text of at most `maxLines` lines above `limitY`, cut at a word with an ellipsis; returns the height used.
    int Paragraph(int x, int y, int width, std::u16string text, int maxLines, int limitY, uint32_t ink)
    {
        const int lh = own::LineHeight(0);
        maxLines = std::min(maxLines, (limitY - y) / lh);
        if (maxLines <= 0 || text.empty()) return 0;
        if (own::TextLines(0, text.c_str(), width) > maxLines) {
            while (!text.empty()) {
                const size_t space = text.find_last_of(u' ');
                text.resize(space == std::u16string::npos ? 0 : space);
                if (own::TextLines(0, (text + u"...").c_str(), width) <= maxLines) break;
            }
            text += u"...";
        }
        const int lines = std::max(1, own::TextLines(0, text.c_str(), width));
        if (m_drawing) own::TextWrapped(0, x, y, width, lines * lh, text.c_str(), ink, 0);
        return lines * lh + 6;
    }

    // A tinted box with what the player should notice about the selected mod, and a button that resolves it when there is one.
    int Banner(int x0, int y, int x1, int limitY, const std::u16string& text, uint32_t tone, const std::u16string& action = {},
            std::string name = {}, std::function<void()> onClick = {})
    {
        const int lh = own::LineHeight(0);
        const int lines = std::clamp(own::TextLines(0, text.c_str(), x1 - x0 - 16), 1, 3);
        const int h = lines * lh + 12 + (action.empty() ? 0 : 34);
        if (y + h > limitY) return 0;
        if (m_drawing) {
            own::Box(x0, y, x1, y + h, m_round ? 6 : 0, Mix(tone, m_pal.white, 5, 6), Mix(tone, m_pal.white, 1, 3));
            own::TextWrapped(0, x0 + 8, y + 6, x1 - x0 - 16, lines * lh, text.c_str(), Mix(tone, m_pal.ink, 1, 2), 0);
        }
        if (!action.empty()) {
            const int w = std::min(x1 - x0 - 16, own::TextWidth(1, action.c_str()) + 32);
            Button(x1 - 8 - w, y + h - 36, x1 - 8, y + h - 8, action, Look::Secondary, std::move(name), std::move(onClick));
        }
        return h + 8;
    }

    void LayoutHeader()
    {
        if (m_drawing) {
            own::Fill(m_x0, m_y0, m_x1, m_y0 + kHeaderH, m_pal.sea);
            own::Text(2, m_x0 + 16, m_y0 + kHeaderH / 2 - own::InkCentre(2), 92, Lookup("OBL_MODS_TITLE").c_str(), m_pal.white, 0);
        }
        const size_t installed = m_list ? m_list->Entries().size() : 0;
        const std::u16string labels[] = {Format(Lookup("OBL_MODS_TAB_INSTALLED"), {Utf8ToEngine(std::to_string(installed))}),
                Lookup("OBL_MODS_TAB_BROWSE"), Lookup("OBL_MODS_TAB_SOURCES")};
        static constexpr const char* kNames[] = {"tab.installed", "tab.browse", "tab.sources"};
        static constexpr int kEdges[] = {112, 304, 448, 592};
        for (int i = 0; i < 3; ++i) {
            const int x0 = m_x0 + kEdges[i] + 2, x1 = m_x0 + kEdges[i + 1] - 2;
            const bool open = static_cast<int>(g_tab) == i;
            const int top = m_y0 + (open ? 8 : 12);
            if (m_drawing) {
                // The open tab takes the colour of the page and runs into it, like a folder tab; the others sit in the bar.
                own::Box(x0, top, x1, m_y0 + kHeaderH + 8, m_round, open ? BackdropAt(m_y0 + kHeaderH) : m_pal.seaDark,
                        open ? m_pal.border : m_pal.seaDark);
                for (int y = m_y0 + kHeaderH; y < m_y0 + kHeaderH + 8; ++y) own::Fill(x0, y, x1, y + 1, BackdropAt(y));
                own::Text(1, x0 + 6, (top + m_y0 + kHeaderH) / 2 - own::InkCentre(1), x1 - x0 - 12, labels[i].c_str(),
                        open ? m_pal.ink : m_pal.white, 2);
            }
            AddTarget(kNames[i], x0, m_y0 + 8, x1, m_y0 + kHeaderH, [this, i] { SelectTab(static_cast<Tab>(i)); });
        }
        Button(m_x0 + 696, m_y0 + 8, m_x0 + 784, m_y0 + 40, Lookup("OBL_MODS_BACK"), Look::Secondary, "back",
                [this] { m_pendingBack = true; });
    }

    void LayoutFooter()
    {
        const int footerY = m_y1 - kFooterH;
        std::u16string text;
        uint32_t ink = m_pal.white;
        if (!g_statusText.empty()) {
            text = g_statusText;
            if (g_statusIsError) ink = Mix(m_pal.red, m_pal.white, 3, 4);
        } else if (m_list && !m_list->CanStart()) {
            text = Lookup("OBL_MODS_FIX_FIRST");
            ink = Mix(m_pal.red, m_pal.white, 3, 4);
        } else if (WorldModsChanged()) {
            text = Lookup("OBL_MODS_WORLD_NEXT");
            ink = Mix(m_amber, m_pal.white, 1, 2);
        }
        if (m_drawing) {
            own::Fill(m_x0, footerY, m_x1, m_y1, m_pal.seaDark);
            // The game version a mod's `engine_min` is checked against.
            const std::u16string version = Format(Lookup("OBL_MODS_GAME_VERSION"), {Utf8ToEngine(std::string(kEngineVersion))});
            const int versionW = own::TextWidth(0, version.c_str());
            own::Text(0, m_x1 - 52 - versionW, footerY + kFooterH / 2 - own::InkCentre(0), versionW + 4, version.c_str(),
                    Mix(m_pal.white, m_pal.seaDark, 1, 3), 0);
            if (!text.empty())
                own::Text(
                        1, m_x0 + 16, footerY + kFooterH / 2 - own::InkCentre(1), m_x1 - 68 - versionW - (m_x0 + 16), text.c_str(), ink, 0);
        }
        Button(m_x1 - 40, footerY + 6, m_x1 - 8, footerY + 38, u"?", Look::Secondary, "help", [this] { OnHelp(); });
    }

    void LayoutPager(int page, int pages, void (GuiModsPanel::*change)(int))
    {
        if (pages <= 1) return;
        const int x1 = m_x0 + kListX1, y0 = m_y0 + kBarY, y1 = y0 + 32;
        Button(x1 - 112, y0, x1 - 80, y1, u"<", page > 0 ? Look::Secondary : Look::Disabled, "page.prev",
                [this, change] { (this->*change)(-1); });
        Label(1, x1 - 80, y0 + 16 - own::InkCentre(1), 48, Utf8ToEngine(std::to_string(page + 1) + "/" + std::to_string(pages)), m_pal.ink,
                2);
        Button(x1 - 32, y0, x1, y1, u">", page + 1 < pages ? Look::Secondary : Look::Disabled, "page.next",
                [this, change] { (this->*change)(1); });
    }

    // ---- Installed tab ----

    struct Slot {
        int y = 0;
        size_t index = 0; // into m_list->Entries()
        bool header = false; // a section title before the entry
        bool on = false;
    };

    // The rows of the current page: the active mods under one title (in load order), the others under another.
    std::vector<Slot> InstalledSlots() const
    {
        std::vector<Slot> out;
        if (!m_list) return out;
        const size_t activeCount = m_list->ActiveCount();
        const size_t start = static_cast<size_t>(g_page * kPageSize);
        const size_t end = std::min(m_list->Entries().size(), start + static_cast<size_t>(kPageSize));
        int y = m_y0 + kTopY;
        bool titledOn = false, titledOff = false;
        for (size_t i = start; i < end; ++i) {
            const bool on = i < activeCount;
            if (on ? !titledOn : !titledOff) {
                out.push_back({y, i, true, on});
                y += kSectionH;
                (on ? titledOn : titledOff) = true;
            }
            out.push_back({y, i, false, on});
            y += kRowPitch;
        }
        return out;
    }

    static bool IsHardProblem(mods::Problem problem)
    {
        return problem == mods::Problem::Invalid || problem == mods::Problem::WrongPack || problem == mods::Problem::NeedsApi ||
                problem == mods::Problem::NeedsEngine;
    }

    std::string TitleOf(const mods::Entry& entry) const
    {
        return entry.mod.valid && !entry.mod.manifest.title.empty() ? entry.mod.manifest.title : entry.mod.id;
    }

    std::string TitleOf(const std::string& id) const
    {
        const mods::Entry* entry = m_list ? m_list->Find(id) : nullptr;
        return entry ? TitleOf(*entry) : id;
    }

    // Why the entry cannot start, naming other mods by their titles.
    std::u16string ProblemLine(const mods::Entry& entry) const
    {
        const bool namesMod = entry.problem == mods::Problem::MissingRequirement || entry.problem == mods::Problem::RequirementLater ||
                entry.problem == mods::Problem::ConflictsWith;
        return ProblemText(entry.problem, namesMod ? TitleOf(entry.detail) : entry.detail);
    }

    std::u16string Meta(const std::string& version, mod_pack::Scope scope) const
    {
        std::u16string out = Format(Lookup("OBL_MODS_VERSION"), {Utf8ToEngine(version)});
        out += kDot;
        out += ScopeText(scope);
        return out;
    }

    // A world mod switched on or off since the game started: the running world still has the old list.
    bool PendingRestart(const mods::Entry& entry) const
    {
        if (!entry.mod.valid || entry.mod.manifest.scope != mod_pack::Scope::World) return false;
        const bool started = std::find(g_startupWorldMods.begin(), g_startupWorldMods.end(), entry.mod.id) != g_startupWorldMods.end();
        return entry.on != started;
    }

    static bool Involves(const mods::Conflict& c, const std::string& id)
    {
        return c.winner == id || c.loser == id || (c.winner.empty() && c.what.find(id) != std::string::npos);
    }

    bool InConflict(const std::string& id) const
    {
        if (!m_list) return false;
        const auto& all = m_list->Conflicts();
        return std::any_of(all.begin(), all.end(), [&](const mods::Conflict& c) { return Involves(c, id); });
    }

    std::u16string ConflictLine(const mods::Conflict& c) const
    {
        if (c.winner.empty()) return Utf8ToEngine(c.what);
        return Format(
                Lookup("OBL_MODS_CONFLICT_VALUE"), {Utf8ToEngine(TitleOf(c.winner)), Utf8ToEngine(TitleOf(c.loser)), Utf8ToEngine(c.what)});
    }

    void Select(const std::string& id)
    {
        g_selectedId = id;
        RebuildTargets();
        MarkDirty();
    }

    void SelectFirstOverlap()
    {
        if (!m_list || m_list->Conflicts().empty()) return;
        const mods::Conflict& c = m_list->Conflicts().front();
        const std::string id = !c.loser.empty() ? c.loser : c.winner;
        if (id.empty() || !m_list->Find(id)) return;
        EnsureVisible(id);
        Select(id);
    }

    void SelectTab(Tab tab)
    {
        g_tab = tab;
        if (tab == Tab::Browse && !os::HasEnv("OBL_SCRIPT")) {
            const mods::StoredCatalog* official = m_catalogStore.Find(mods::kOfficialCatalogUrl);
            if (official && official->enabled && official->mods.empty()) BeginFetchCatalog(mods::kOfficialCatalogUrl, false);
        }
        RebuildTargets();
        MarkDirty();
    }

    void LayoutInstalled()
    {
        const int x0 = m_x0 + kListX0, x1 = m_x0 + kListX1;
        if (!m_list || m_list->Entries().empty()) {
            if (m_drawing) own::Box(x0, m_y0 + kTopY, x1, m_y0 + kTopY + 64, m_round, m_pal.foam, m_pal.border);
            Paragraph(x0 + 12, m_y0 + kTopY + 10, x1 - x0 - 24, Lookup("OBL_MODS_NONE"), 3, m_y0 + kTopY + 60, m_pal.ink);
        } else {
            for (const Slot& slot : InstalledSlots()) {
                if (slot.header)
                    LayoutSection(slot);
                else
                    LayoutModRow(slot);
            }
        }
        Button(x0, m_y0 + kBarY, x0 + 200, m_y0 + kBarY + 32, u"+ " + Lookup("OBL_MODS_INSTALL"), Look::Secondary, "install",
                [this] { OnInstall(); });
        LayoutPager(g_page, PageCount(), &GuiModsPanel::ChangePage);
        LayoutModCard();
    }

    void LayoutSection(const Slot& slot)
    {
        const int x0 = m_x0 + kListX0, x1 = m_x0 + kListX1;
        Label(1, x0 + 4, slot.y + 4, 280, Lookup(slot.on ? "OBL_MODS_SECTION_ON" : "OBL_MODS_SECTION_OFF"), m_pal.seaDark);
        if (slot.on && m_list && !m_list->Conflicts().empty()) {
            const std::u16string text = Format(Lookup("OBL_MODS_OVERLAPS"), {Utf8ToEngine(std::to_string(m_list->Conflicts().size()))});
            const int left = Badge(x1 - 4, slot.y + 1, text, m_amber);
            AddTarget("overlaps", left, slot.y, x1 - 4, slot.y + 20, [this] { SelectFirstOverlap(); });
        }
    }

    void LayoutModRow(const Slot& slot)
    {
        const mods::Entry& entry = m_list->Entries()[slot.index];
        const std::string id = entry.mod.id;
        const int x0 = m_x0 + kListX0, x1 = m_x0 + kListX1, y = slot.y;
        const bool selected = id == g_selectedId;
        const bool hard = IsHardProblem(entry.problem);
        const bool attention = entry.problem != mods::Problem::None || (entry.on && InConflict(id));
        if (m_drawing) {
            own::Box(x0, y, x1, y + kRowH, m_round, selected ? m_pal.foam : m_pal.white, selected ? m_pal.sea : m_pal.border);
            if (entry.problem != mods::Problem::None) own::Fill(x0 + 1, y + 8, x0 + 4, y + kRowH - 8, hard ? m_pal.red : m_amber);
            if (slot.on)
                own::Text(1, x0 + 4, y + kRowH / 2 - own::InkCentre(1), 22, Utf8ToEngine(std::to_string(slot.index + 1)).c_str(),
                        m_pal.grey, 2);
        }
        Switch(x0 + 28, y + 10, entry.on, !hard, "toggle." + id, [this, id] { OnToggle(id); });

        // The selected active mod gets its order buttons; any other row shows a mark when it needs attention.
        int right = x1 - 8;
        if (selected && entry.on) {
            const bool canUp = slot.index > 0, canDown = slot.index + 1 < m_list->ActiveCount();
            ArrowButton(x1 - 66, y + 6, x1 - 38, y + 34, true, canUp ? Look::Secondary : Look::Disabled, "up." + id,
                    [this, id] { OnMove(id, -1); });
            ArrowButton(x1 - 34, y + 6, x1 - 6, y + 34, false, canDown ? Look::Secondary : Look::Disabled, "down." + id,
                    [this, id] { OnMove(id, 1); });
            right = x1 - 72;
        } else if (attention) {
            Marker(x1 - 18, y + kRowH / 2, hard ? m_pal.red : m_amber);
            right = x1 - 32;
        }

        const int tx = x0 + 78;
        Label(1, tx, y + 4, right - tx, Utf8ToEngine(TitleOf(entry)), hard ? m_pal.red : m_pal.ink);
        std::u16string sub;
        uint32_t subInk = m_pal.grey;
        if (entry.problem != mods::Problem::None) {
            sub = ProblemLine(entry);
            subInk = hard ? m_pal.red : Mix(m_amber, m_pal.ink, 1, 3);
        } else if (entry.mod.valid) {
            sub = Meta(entry.mod.manifest.modVersion, entry.mod.manifest.scope);
            if (PendingRestart(entry)) {
                sub += kDot;
                sub += Lookup("OBL_MODS_AFTER_RESTART");
                subInk = Mix(m_amber, m_pal.ink, 1, 3);
            }
        }
        Label(0, tx, y + 22, right - tx, sub, subInk);
        // Last, so the switch and the order buttons above win their taps.
        AddTarget("select." + id, x0, y, x1, y + kRowH, [this, id] { Select(id); });
    }

    void LayoutModCard()
    {
        const int x0 = m_x0 + kCardX0, x1 = m_x0 + kCardX1, top = m_y0 + kTopY, bottom = m_y0 + kCardBottom;
        if (m_drawing) own::Box(x0, top, x1, bottom, m_round, m_pal.foam, m_pal.border);
        const mods::Entry* selected = m_list ? m_list->Find(g_selectedId) : nullptr;
        if (!selected) return;
        const mods::Entry& sel = *selected;
        const std::string id = sel.mod.id;
        const int ix = x0 + 12, iw = x1 - x0 - 24;
        const int actionsY = bottom - 44;
        const int limit = actionsY - 8;
        const int lh0 = own::LineHeight(0), lh1 = own::LineHeight(1);

        int y = top + 10;
        Label(2, ix, y, iw, Utf8ToEngine(TitleOf(sel)), m_pal.ink);
        y += own::LineHeight(2) + 6;
        if (sel.mod.valid) {
            Label(0, ix, y, iw, Meta(sel.mod.manifest.modVersion, sel.mod.manifest.scope), m_pal.grey);
            y += lh0 + 8;
            if (!sel.mod.manifest.description.empty())
                y += Paragraph(ix, y, iw, Utf8ToEngine(sel.mod.manifest.description), 4, limit, m_pal.ink);
        }

        if (sel.problem != mods::Problem::None) {
            std::u16string action;
            std::function<void()> fix;
            const mods::Entry* other = m_list->Find(sel.detail);
            if (other && sel.problem == mods::Problem::ConflictsWith && other->on) {
                action = Format(Lookup("OBL_MODS_FIX_OFF"), {Utf8ToEngine(TitleOf(*other))});
            } else if (other && sel.problem == mods::Problem::MissingRequirement && !other->on && !IsHardProblem(other->problem)) {
                action = Format(Lookup("OBL_MODS_FIX_ON"), {Utf8ToEngine(TitleOf(*other))});
            }
            if (!action.empty()) {
                const std::string otherId = other->mod.id;
                fix = [this, otherId] { OnToggle(otherId); };
            }
            y += Banner(
                    ix, y, x1 - 12, limit, ProblemLine(sel), IsHardProblem(sel.problem) ? m_pal.red : m_amber, action, "fix." + id, fix);
        }
        if (PendingRestart(sel)) y += Banner(ix, y, x1 - 12, limit, Lookup("OBL_MODS_WORLD_NEXT"), m_amber);
        if (sel.on && m_list) {
            int shown = 0;
            for (const mods::Conflict& c : m_list->Conflicts()) {
                if (!Involves(c, id) || shown == kMaxOverlapBanners) continue;
                const int used = Banner(ix, y, x1 - 12, limit, ConflictLine(c), m_amber);
                if (!used) break;
                y += used;
                ++shown;
            }
            if (shown) y += Paragraph(ix, y, iw, Lookup("OBL_MODS_CONFLICT_HINT"), 2, limit, m_pal.grey);
        }

        if (sel.mod.valid && !sel.mod.manifest.settings.empty()) y = LayoutSettings(sel, ix, y, iw, limit);

        if (sel.mod.valid && y + lh1 <= limit) {
            const std::u16string label = (g_showTechnical ? u"- " : u"+ ") + Lookup("OBL_MODS_TECHNICAL");
            Label(1, ix, y, iw, label, m_pal.seaDark);
            AddTarget("technical", ix, y - 4, ix + std::min(iw, own::TextWidth(1, label.c_str()) + 8), y + lh1 + 4, [this] {
                g_showTechnical = !g_showTechnical;
                RebuildTargets();
                MarkDirty();
            });
            y += lh1 + 6;
            if (g_showTechnical) {
                const auto& m = sel.mod.manifest;
                std::vector<std::u16string> lines = {Format(Lookup("OBL_MODS_PACKS"), {Utf8ToEngine(Join(m.packs, ", "))})};
                if (!m.takesOver.empty()) lines.push_back(Format(Lookup("OBL_MODS_TAKES_OVER"), {Utf8ToEngine(Join(m.takesOver, ", "))}));
                if (!m.actions.empty()) lines.push_back(Format(Lookup("OBL_MODS_ACTIONS"), {Utf8ToEngine(FormatActions(m.actions))}));
                if (!m.bindings.empty()) lines.push_back(Format(Lookup("OBL_MODS_BINDINGS"), {Utf8ToEngine(FormatBindings(m.bindings))}));
                if (!m.requiredMods.empty())
                    lines.push_back(Format(Lookup("OBL_MODS_REQUIRES"), {Utf8ToEngine(Join(m.requiredMods, ", "))}));
                if (!m.conflictingMods.empty())
                    lines.push_back(Format(Lookup("OBL_MODS_INCOMPATIBLE"), {Utf8ToEngine(Join(m.conflictingMods, ", "))}));
                for (const std::u16string& line : lines) {
                    if (y + lh0 > limit) break;
                    Label(0, ix, y, iw, line, m_pal.ink);
                    y += lh0 + 2;
                }
            }
        }

        Button(ix, actionsY, ix + 150, actionsY + 32, Lookup(sel.on ? "OBL_MODS_TURN_OFF" : "OBL_MODS_TURN_ON"), Look::Primary,
                "card.toggle", [this, id] { OnToggle(id); });
        Button(x1 - 132, actionsY, x1 - 12, actionsY + 32, Lookup("OBL_MODS_REMOVE_BUTTON"), Look::Danger, "remove." + id,
                [this, id] { OnRemove(id); });
    }

    int LayoutSettings(const mods::Entry& sel, int ix, int y, int iw, int limit)
    {
        const int lh1 = own::LineHeight(1);
        if (y + lh1 + 6 > limit) return y;
        Label(1, ix, y, iw, Lookup("OBL_MODS_SETTINGS"), m_pal.seaDark);
        y += lh1 + 6;
        const auto& active = mod_pack::Active();
        const auto live = std::find_if(active.begin(), active.end(), [&](const auto& m) { return m.manifest.id == sel.mod.id; });
        if (!sel.on || sel.problem != mods::Problem::None || live == active.end())
            return y + Paragraph(ix, y, iw, Lookup("OBL_MODS_SETTINGS_OFF"), 2, limit, m_pal.grey);
        const auto& fields = sel.mod.manifest.settings;
        const int right = ix + iw;
        for (size_t i = 0; i < fields.size() && i < static_cast<size_t>(kMaxSettingRows); ++i) {
            if (y + 26 > limit) break;
            const settings_schema::Field field = fields[i];
            const std::string modId = sel.mod.id;
            Label(0, ix, y + 12 - own::InkCentre(0), iw - 176, Utf8ToEngine(field.label), m_pal.ink);
            Button(right - 168, y, right - 140, y + 24, u"<", Look::Secondary, "setting." + field.id + ".prev",
                    [this, modId, field] { OnStepSetting(modId, field, -1); });
            if (m_drawing) {
                own::Box(right - 136, y, right - 32, y + 24, m_round ? 6 : 0, m_pal.white, m_pal.border);
                const std::string current = mods::ModSettingValue(*live, field);
                const std::u16string shown = field.kind == settings_schema::Kind::Toggle
                        ? Lookup(settings_schema::IsOn(current) ? "OBL_MODS_ON" : "OBL_MODS_OFF")
                        : Utf8ToEngine(settings_schema::Display(field, current));
                own::Text(0, right - 132, y + 12 - own::InkCentre(0), 96, shown.c_str(), m_pal.ink, 2);
            }
            Button(right - 28, y, right, y + 24, u">", Look::Secondary, "setting." + field.id + ".next",
                    [this, modId, field] { OnStepSetting(modId, field, 1); });
            y += 30;
        }
        return y + 4;
    }

    // ---- Browse tab ----

    void LayoutBrowse()
    {
        if (m_catalogRows.empty()) {
            LayoutBrowseEmpty();
            return;
        }
        const size_t start = static_cast<size_t>(m_catalogPage * kPageSize);
        const size_t end = std::min(m_catalogRows.size(), start + static_cast<size_t>(kPageSize));
        int y = m_y0 + kTopY;
        for (size_t i = start; i < end; ++i, y += kRowPitch) LayoutCatalogRow(m_catalogRows[i], y);
        LayoutPager(m_catalogPage, CatalogPageCount(), &GuiModsPanel::ChangeCatalogPage);
        LayoutCatalogCard();
    }

    void LayoutBrowseEmpty()
    {
        const int x0 = m_x0 + kListX0, x1 = m_x0 + kCardX1, top = m_y0 + kTopY;
        if (m_drawing) own::Box(x0, top, x1, top + 112, m_round, m_pal.foam, m_pal.border);
        const auto& cats = m_catalogStore.Catalogs();
        const bool anyEnabled = std::any_of(cats.begin(), cats.end(), [](const mods::StoredCatalog& c) { return c.enabled; });
        const char* key = m_activeFetch.handle && !m_activeFetch.isModZip ? "OBL_MODS_CAT_LOADING"
                : anyEnabled                                              ? "OBL_MODS_CAT_NO_MODS"
                                                                          : "OBL_MODS_CAT_NONE";
        Paragraph(x0 + 14, top + 14, x1 - x0 - 28, Lookup(key), 3, top + 64, m_pal.ink);
        Button(x0 + 14, top + 68, x0 + 174, top + 100, Lookup("OBL_MODS_CAT_REFRESH"),
                m_activeFetch.handle ? Look::Disabled : Look::Secondary, "catalog.refresh", [this] { RefreshAllCatalogs(); });
        Button(x0 + 182, top + 68, x0 + 342, top + 100, Lookup("OBL_MODS_TAB_SOURCES"), Look::Secondary, "browse.sources",
                [this] { SelectTab(Tab::Sources); });
    }

    void LayoutCatalogRow(const CatalogRow& row, int y)
    {
        const std::string id = row.entry.id;
        const int x0 = m_x0 + kListX0, x1 = m_x0 + kListX1;
        const bool selected = id == g_selectedCatalogId;
        if (m_drawing) own::Box(x0, y, x1, y + kRowH, m_round, selected ? m_pal.foam : m_pal.white, selected ? m_pal.sea : m_pal.border);
        int right = x1 - 8;
        const mods::CatalogOffer offer = OfferFor(row.entry);
        if (offer == mods::CatalogOffer::UpToDate)
            right = Badge(x1 - 8, y + 11, Lookup("OBL_MODS_CAT_UP_TO_DATE"), m_pal.green) - 8;
        else if (offer == mods::CatalogOffer::Update)
            right = Badge(x1 - 8, y + 11, Lookup("OBL_MODS_CAT_UPDATE_BADGE"), m_amber) - 8;
        Label(1, x0 + 12, y + 4, right - x0 - 12, Utf8ToEngine(row.entry.title), m_pal.ink);
        std::u16string sub = Meta(row.entry.modVersion, row.entry.scope);
        sub += kDot;
        sub += Utf8ToEngine(row.catalogTitle);
        Label(0, x0 + 12, y + 22, right - x0 - 12, sub, m_pal.grey);
        AddTarget("select." + id, x0, y, x1, y + kRowH, [this, id] {
            g_selectedCatalogId = id;
            RebuildTargets();
            MarkDirty();
        });
    }

    void LayoutCatalogCard()
    {
        const int x0 = m_x0 + kCardX0, x1 = m_x0 + kCardX1, top = m_y0 + kTopY, bottom = m_y0 + kCardBottom;
        if (m_drawing) own::Box(x0, top, x1, bottom, m_round, m_pal.foam, m_pal.border);
        const auto row = std::find_if(
                m_catalogRows.begin(), m_catalogRows.end(), [&](const CatalogRow& r) { return r.entry.id == g_selectedCatalogId; });
        if (row == m_catalogRows.end()) return;
        const mods::CatalogEntry& e = row->entry;
        const int ix = x0 + 12, iw = x1 - x0 - 24;
        const int actionsY = bottom - 44;
        const int limit = actionsY - 8;
        const int lh0 = own::LineHeight(0);

        int y = top + 10;
        Label(2, ix, y, iw, Utf8ToEngine(e.title), m_pal.ink);
        y += own::LineHeight(2) + 6;
        std::u16string meta = Meta(e.modVersion, e.scope);
        meta += kDot;
        meta += Format(Lookup("OBL_MODS_CAT_LICENSE"), {Utf8ToEngine(e.license)});
        Label(0, ix, y, iw, meta, m_pal.grey);
        y += lh0 + 2;
        std::u16string source = Format(Lookup("OBL_MODS_CAT_FROM"), {Utf8ToEngine(row->catalogTitle)});
        const mods::StoredCatalog* catalog = m_catalogStore.Find(row->catalogUrl);
        if (!catalog || !catalog->official) source += u" (" + Lookup("OBL_MODS_CAT_THIRD_PARTY") + u")";
        Label(0, ix, y, iw, source, m_pal.grey);
        y += lh0 + 8;
        if (!e.description.empty()) y += Paragraph(ix, y, iw, Utf8ToEngine(e.description), 8, limit, m_pal.ink);
        if (!e.requiresMods.empty()) {
            // Name the mods by their catalog titles; the ones not installed yet come along with this one.
            std::vector<std::string> missing, all;
            for (const std::string& need : e.requiresMods) {
                const auto found =
                        std::find_if(m_catalogRows.begin(), m_catalogRows.end(), [&](const CatalogRow& r) { return r.entry.id == need; });
                const std::string title = found != m_catalogRows.end() ? found->entry.title : need;
                all.push_back(title);
                if (!m_list || !m_list->Find(need)) missing.push_back(title);
            }
            const std::u16string line = missing.empty() ? Format(Lookup("OBL_MODS_REQUIRES"), {Utf8ToEngine(Join(all, ", "))})
                                                        : Format(Lookup("OBL_MODS_CAT_ALSO_INSTALLS"), {Utf8ToEngine(Join(missing, ", "))});
            y += Paragraph(ix, y, iw, line, 2, limit, m_pal.ink);
        }

        const mods::CatalogOffer offer = OfferFor(e);
        std::u16string label;
        Look look = Look::Primary;
        if (m_activeFetch.handle) {
            label = Lookup("OBL_MODS_CAT_BUSY");
            look = Look::Disabled;
        } else if (offer == mods::CatalogOffer::UpToDate) {
            label = Lookup("OBL_MODS_CAT_UP_TO_DATE_BUTTON");
            look = Look::Disabled;
        } else if (offer == mods::CatalogOffer::Update) {
            label = Format(Lookup("OBL_MODS_CAT_UPDATE_TO"), {Utf8ToEngine(e.modVersion)});
        } else {
            label = Lookup("OBL_MODS_CAT_INSTALL");
        }
        const std::string id = e.id;
        Button(ix, actionsY, x1 - 12, actionsY + 32, label, look, "catalog.install." + id, [this, id] { InstallFromCatalog(id); });
    }

    // ---- Sources tab ----

    void LayoutSources()
    {
        const int x0 = m_x0 + kListX0, x1 = m_x0 + kCardX1;
        int y = m_y0 + kTopY;
        Label(1, x0 + 4, y + 2, 420, Lookup("OBL_MODS_CATALOGS"), m_pal.seaDark);
        Label(0, x0 + 4, y + 22, 480, Lookup("OBL_MODS_CAT_SIGNED_NOTE"), m_pal.grey);
        Button(x1 - 250, y + 4, x1 - 128, y + 36, Lookup("OBL_MODS_CAT_REFRESH"), m_activeFetch.handle ? Look::Disabled : Look::Secondary,
                "catalog.refresh", [this] { RefreshAllCatalogs(); });
        Button(x1 - 120, y + 4, x1, y + 36, u"+ " + Lookup("OBL_MODS_CAT_ADD_BUTTON"), Look::Primary, "catalog.add",
                [this] { StartUrlInput(); });
        y += 50;

        const auto& cats = m_catalogStore.Catalogs();
        const size_t shown = std::min<size_t>(cats.size(), kMaxSourceRows);
        for (size_t i = 0; i < shown; ++i, y += 56) {
            const mods::StoredCatalog& cat = cats[i];
            if (m_drawing) own::Box(x0, y, x1, y + 50, m_round, m_pal.white, m_pal.border);
            const std::u16string title = Utf8ToEngine(cat.title.empty() ? cat.url : cat.title);
            const std::u16string kind = Lookup(cat.official ? "OBL_MODS_CAT_OFFICIAL" : "OBL_MODS_CAT_THIRD_PARTY");
            const int kindW = own::TextWidth(0, kind.c_str()) + 16;
            const int titleW = std::min(own::TextWidth(1, title.c_str()) + 4, 470 - kindW);
            Label(1, x0 + 12, y + 6, titleW, title, cat.enabled ? m_pal.ink : m_pal.grey);
            Badge(x0 + 12 + titleW + 8 + kindW, y + 6, kind, cat.official ? m_pal.sea : m_amber);
            Label(0, x0 + 12, y + 28, 500, Utf8ToEngine(cat.url), m_pal.grey);
            const std::u16string state = cat.enabled ? Format(Lookup("OBL_MODS_CAT_COUNT"), {Utf8ToEngine(std::to_string(cat.mods.size()))})
                                                     : Lookup("OBL_MODS_CAT_SWITCHED_OFF");
            Label(0, x1 - 260, y + 25 - own::InkCentre(0), 120, state, m_pal.grey, 1);
            Switch(x1 - 130, y + 15, cat.enabled, true, "catalog.toggle." + std::to_string(i), [this, i] { ToggleCatalogAt(i); });
            if (!cat.official)
                Button(x1 - 82, y + 10, x1 - 10, y + 40, Lookup("OBL_MODS_REMOVE"), Look::Danger, "catalog.remove." + std::to_string(i),
                        [this, i] { RemoveCatalogAt(i); });
        }
    }

    // ---- Dialogs ----

    void LayoutDialog()
    {
        std::u16string title, body;
        std::u16string confirm = Lookup("OBL_MODS_CONFIRM");
        Look confirmLook = Look::Primary;
        std::string confirmName = "archive.confirm", cancelName = "archive.cancel";
        if (m_editingUrl) {
            title = Lookup("OBL_MODS_CAT_ADD");
            body = Lookup("OBL_MODS_CAT_URL_HINT");
            confirm = Lookup("OBL_MODS_CAT_ADD_BUTTON");
        } else if (m_pendingKeyChange) {
            title = Lookup("OBL_MODS_CAT_KEY_TITLE");
            body = Format(Lookup("OBL_MODS_CAT_KEY_CHANGED"), {Utf8ToEngine(m_pendingKeyChange->catalog.title)});
            confirm = Lookup("OBL_MODS_CAT_TRUST");
            confirmLook = Look::DangerSolid;
            confirmName = "catalog.trust_key";
            cancelName = "catalog.cancel";
        } else if (m_pendingAddCatalog) {
            title = Lookup("OBL_MODS_CAT_ADD_TITLE");
            body = Format(Lookup("OBL_MODS_CAT_ADD_CONFIRM"), {Utf8ToEngine(m_pendingAddCatalog->url)});
            confirm = Lookup("OBL_MODS_CAT_ADD_BUTTON");
            confirmName = "catalog.confirm";
            cancelName = "catalog.cancel";
        } else if (m_pendingReplace) {
            title = Lookup("OBL_MODS_REPLACE_TITLE");
            body = Format(Lookup("OBL_MODS_REPLACE_CONFIRM"), {Utf8ToEngine(TitleOf(m_pendingReplace->Id()))});
            confirm = Lookup("OBL_MODS_REPLACE");
        } else {
            title = Lookup("OBL_MODS_REMOVE_TITLE");
            body = Format(Lookup("OBL_MODS_REMOVE_CONFIRM"), {Utf8ToEngine(TitleOf(m_pendingRemove))});
            confirm = Lookup("OBL_MODS_REMOVE");
            confirmLook = Look::DangerSolid;
        }

        constexpr int kW = 460;
        const int x0 = m_x0 + (m_x1 - m_x0 - kW) / 2, x1 = x0 + kW;
        const int lh1 = own::LineHeight(1);
        const int bodyLines = std::clamp(own::TextLines(1, body.c_str(), kW - 48), 1, 6);
        const int fieldH = m_editingUrl ? 40 : 0;
        const int h = 20 + own::LineHeight(2) + 12 + bodyLines * lh1 + 16 + fieldH + 32 + 20;
        const int y0 = m_y0 + (m_y1 - m_y0 - h) / 2 - 16, y1 = y0 + h;
        if (m_drawing) {
            own::Box(x0 + 6, y0 + 6, x1 + 6, y1 + 6, m_round, Dim(Dim(m_pal.seaDark)), Dim(Dim(m_pal.seaDark)));
            own::Box(x0, y0, x1, y1, m_round, m_pal.white, m_pal.seaDark);
            own::Text(2, x0 + 24, y0 + 20, kW - 48, title.c_str(), m_pal.ink, 0);
            own::TextWrapped(1, x0 + 24, y0 + 20 + own::LineHeight(2) + 12, kW - 48, bodyLines * lh1, body.c_str(), m_pal.ink, 0);
        }
        const int buttonsY = y1 - 20 - 32;
        if (m_editingUrl) {
            const int fy = buttonsY - 16 - 32;
            if (m_drawing) {
                own::Box(x0 + 24, fy, x1 - 24, fy + 32, m_round ? 6 : 0, m_pal.foam, m_pal.sea);
                // The end of a long address is the part being typed, so a long one shows its tail.
                std::u16string shown = m_urlDraft.empty() ? u"https://" : Utf8ToEngine(m_urlDraft + "_");
                const int fieldW = kW - 64;
                bool cut = false;
                while (shown.size() > 4 && own::TextWidth(0, shown.c_str()) > fieldW - 16) {
                    shown.erase(0, 1);
                    cut = true;
                }
                if (cut) shown = u"..." + shown;
                own::Text(0, x0 + 32, fy + 16 - own::InkCentre(0), fieldW, shown.c_str(), m_urlDraft.empty() ? m_pal.grey : m_pal.ink, 0);
            }
            Button(x0 + 24, buttonsY, x0 + 134, buttonsY + 32, Lookup("OBL_MODS_CAT_PASTE"), Look::Secondary, "catalog.paste",
                    [this] { PasteIntoUrlDraft(); });
            Button(x1 - 270, buttonsY, x1 - 150, buttonsY + 32, Lookup("OBL_MODS_CANCEL"), Look::Secondary, "catalog.url_cancel", [this] {
                StopUrlInput();
                RebuildTargets();
                MarkDirty();
            });
            Button(x1 - 142, buttonsY, x1 - 24, buttonsY + 32, confirm, Look::Primary, "catalog.url_ok",
                    [this] { SubmitCatalogUrl(m_urlDraft, false); });
            return;
        }
        Button(x1 - 270, buttonsY, x1 - 150, buttonsY + 32, Lookup("OBL_MODS_CANCEL"), Look::Secondary, cancelName,
                [this] { CancelConfirmAction(); });
        Button(x1 - 142, buttonsY, x1 - 24, buttonsY + 32, confirm, confirmLook, confirmName, [this] { ConfirmCatalogAction(); });
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
    bool m_editingUrl = false;
    std::string m_urlDraft;
    mods::CatalogStore m_catalogStore;
    std::vector<CatalogRow> m_catalogRows;
    int m_catalogPage = 0;
    std::optional<PendingAddCatalog> m_pendingAddCatalog;
    std::optional<PendingKeyChange> m_pendingKeyChange;
    ActiveCatalogFetch m_activeFetch;
    std::vector<PendingAddCatalog> m_refreshQueue;
    std::vector<mods::CatalogEntry> m_downloadQueue;
    bool m_pendingBack = false;
    bool m_pendingReload = false;
    bool m_settle = false; // files changed: SettleList applies the list once the batch is done
    std::vector<std::string> m_switchOn; // installed by the current batch, dependencies first
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
    g_tab = Tab::Sources;
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
        if (g_tab != Tab::Installed || !name.starts_with(prefix)) continue;
        const std::string id(name.substr(prefix.size()));
        g_openPanel->EnsureVisible(id);
        // The order buttons and Remove belong to the selected mod, so a player selects it first.
        if (prefix == "up." || prefix == "down." || prefix == "remove.") g_openPanel->Select(id);
        break;
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
