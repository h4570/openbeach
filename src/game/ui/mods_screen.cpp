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
#include "game/mods/mod_archive.h"
#include "game/mods/mod_list.h"
#include "game/mods/presentation_scripts.h"
#include "game/tables/text_load.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/own_layer.h"
#include "game/ui/own_widgets.h"
#include "game/ui/text_table.h"

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
    GuiModsPanel(int x0, int y0, int x1, int y1) : GuiLeaf(x0, y0, x1, y1) { m_type = kModsPanelType; }

    ~GuiModsPanel() override
    {
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
        } else if (g_openPanel == this) {
            SetArchiveInputEnabled(false);
            m_pendingReplace.reset();
            m_pendingRemove.clear();
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
        if (m_visible && !IsConfirming()) PollArchiveInput();
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

    void Draw() override
    {
        if (!m_visible || !m_dirty) return;
        if (!m_list) LoadList();
        RebuildTargets();

        const int w = m_x1 - m_x0;
        const int h = m_y1 - m_y0;
        own::Fill(m_x0, m_y0, m_x1, m_y1, own::kSand);

        // Header bar.
        own::Fill(m_x0, m_y0, m_x1, m_y0 + 48, own::kSea);
        own::Text(2, m_x0 + 16, m_y0 + 14, 560, Lookup("OBL_MODS_TITLE").c_str(), own::kWhite, 0);
        DrawButton(m_x0 + 248, m_y0 + 8, m_x0 + 460, m_y0 + 40, Lookup("OBL_MODS_INSTALL").c_str(), own::kSeaDark, own::kWhite);
        const bool canRemove = m_list && m_list->Find(g_selectedId);
        DrawButton(m_x0 + 468, m_y0 + 8, m_x0 + 602, m_y0 + 40, Lookup("OBL_MODS_REMOVE").c_str(), canRemove ? own::kSeaDark : own::kSand,
                canRemove ? own::kWhite : own::kGrey);
        DrawButton(m_x0 + 610, m_y0 + 8, m_x0 + 692, m_y0 + 40, Lookup("OBL_MODS_HELP").c_str(), own::kSeaDark, own::kWhite);
        DrawButton(m_x0 + 700, m_y0 + 8, m_x0 + 784, m_y0 + 40, Lookup("OBL_MODS_BACK").c_str(), own::kFoam, own::kInk);

        DrawListColumn();
        DrawRightColumn();

        // Footer status bar.
        const int footerY = m_y0 + h - 44;
        own::Fill(m_x0, footerY, m_x0 + w, m_y1, own::kSeaDark);
        std::u16string footer;
        uint32_t footerColor = own::kWhite;
        if (IsConfirming()) {
            const auto& id = m_pendingReplace ? m_pendingReplace->Id() : m_pendingRemove;
            footer = Format(Lookup(m_pendingReplace ? "OBL_MODS_REPLACE_CONFIRM" : "OBL_MODS_REMOVE_CONFIRM"), {Utf8ToEngine(id)});
            DrawButton(m_x0 + 570, footerY + 6, m_x0 + 672, footerY + 38, Lookup("OBL_MODS_CONFIRM").c_str(), own::kSea, own::kWhite);
            DrawButton(m_x0 + 680, footerY + 6, m_x0 + 784, footerY + 38, Lookup("OBL_MODS_CANCEL").c_str(), own::kFoam, own::kInk);
        } else if (!g_statusText.empty()) {
            footer = g_statusText;
            footerColor = g_statusIsError ? 0xffc8c8 : own::kWhite;
        } else if (m_list && !m_list->CanStart()) {
            footer = Lookup("OBL_MODS_FIX_FIRST");
            footerColor = 0xffc8c8;
        } else if (WorldModsChanged()) {
            footer = Lookup("OBL_MODS_WORLD_NEXT");
            footerColor = own::kFoam;
        }
        if (!footer.empty()) own::Text(1, m_x0 + 16, footerY + 14, IsConfirming() ? 546 : w - 32, footer.c_str(), footerColor, 0);

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

    bool IsConfirming() const { return m_pendingReplace || !m_pendingRemove.empty(); }

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

    void OnRemove()
    {
        if (CanChangeFiles(g_selectedId)) {
            m_pendingRemove = g_selectedId;
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
            if (replace->Write(GameDirectory(), true, error) == mods::ModArchive::Result::Installed)
                OnInstalled(id);
            else
                SetArchiveStatus("OBL_MODS_INSTALL_ERROR", error, true);
        } else if (mods::RemoveMod(GameDirectory(), id, error)) {
            PersistModChange(id);
            LoadList();
            SetArchiveStatus("OBL_MODS_REMOVED", id, false);
        } else {
            SetArchiveStatus("OBL_MODS_REMOVE_ERROR", error, true);
        }
    }

    void OnHelp()
    {
        OBL_LOG("mods screen: help %s", kHelpUrl);
        if (!os::HasEnv("OBL_SCRIPT")) SDL_OpenURL(kHelpUrl);
        g_statusText = Format(Lookup("OBL_MODS_HELP_OPENED"), {Utf8ToEngine(kHelpUrl)});
        g_statusIsError = false;
        MarkDirty();
    }

    void RebuildTargets()
    {
        m_targets.clear();
        if (IsConfirming()) {
            const int footerY = m_y1 - 44;
            m_targets.push_back({"archive.confirm", m_x0 + 570, footerY + 6, m_x0 + 672, footerY + 38, [this] { ConfirmArchiveChange(); }});
            m_targets.push_back({"archive.cancel", m_x0 + 680, footerY + 6, m_x0 + 784, footerY + 38, [this] {
                                     m_pendingReplace.reset();
                                     m_pendingRemove.clear();
                                     RebuildTargets();
                                     MarkDirty();
                                 }});
            return;
        }
        m_targets.push_back({"install", m_x0 + 248, m_y0 + 8, m_x0 + 460, m_y0 + 40, [this] { OnInstall(); }});
        if (m_list && m_list->Find(g_selectedId))
            m_targets.push_back({"remove", m_x0 + 468, m_y0 + 8, m_x0 + 602, m_y0 + 40, [this] { OnRemove(); }});
        m_targets.push_back({"help", m_x0 + 610, m_y0 + 8, m_x0 + 692, m_y0 + 40, [this] { OnHelp(); }});
        m_targets.push_back({"back", m_x0 + 700, m_y0 + 8, m_x0 + 784, m_y0 + 40, [this] { m_pendingBack = true; }});
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
            m_targets.push_back({"select." + id, m_x0 + 74, rowY + 2, m_x0 + 324, rowY + 42, [this, id] {
                                     g_selectedId = id;
                                     RebuildTargets();
                                     MarkDirty();
                                 }});
            if (entry.on) {
                if (i > 0) m_targets.push_back({"up." + id, m_x0 + 328, rowY + 6, m_x0 + 356, rowY + 38, [this, id] { OnMove(id, -1); }});
                if (i + 1 < activeCount)
                    m_targets.push_back({"down." + id, m_x0 + 360, rowY + 6, m_x0 + 388, rowY + 38, [this, id] { OnMove(id, 1); }});
            }
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
        own::Fill(x0, y0, x1, y1, fill);
        own::Frame(x0, y0, x1, y1, own::kBorder);
        const int y = y0 + (y1 - y0 - own::LineHeight(1)) / 2 + 1;
        own::Text(1, x0 + 4, y, x1 - x0 - 8, label, ink, 2);
    }

    void DrawListColumn() const
    {
        const int x0 = m_x0 + 12;
        const int x1 = m_x0 + 394;
        if (!m_list || m_list->Entries().empty()) {
            own::Fill(x0, m_y0 + 58, x1, m_y0 + 116, own::kFoam);
            own::Frame(x0, m_y0 + 58, x1, m_y0 + 116, own::kBorder);
            own::TextWrapped(1, x0 + 10, m_y0 + 68, x1 - x0 - 20, 44, Lookup("OBL_MODS_NONE").c_str(), own::kInk, 0);
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
            own::Fill(x0, rowY, x1, rowY + 44, selected ? own::kFoam : own::kWhite);
            own::Frame(x0, rowY, x1, rowY + 44, hasProblem ? own::kRed : (selected ? own::kSea : own::kBorder));

            const uint32_t toggleFill = entry.on ? own::kGreen : (hasProblem ? own::kGrey : own::kSeaDark);
            DrawButton(m_x0 + 18, rowY + 6, m_x0 + 70, rowY + 38, Lookup(entry.on ? "OBL_MODS_ON" : "OBL_MODS_OFF").c_str(), toggleFill,
                    own::kWhite);

            const std::string& title = (entry.mod.valid && !entry.mod.manifest.title.empty()) ? entry.mod.manifest.title : entry.mod.id;
            own::Text(1, m_x0 + 76, rowY + 6, 246, Utf8ToEngine(title).c_str(), hasProblem ? own::kRed : own::kInk, 0);

            if (hasProblem) {
                own::Text(0, m_x0 + 76, rowY + 24, 246, ProblemText(entry.problem, entry.detail).c_str(), own::kRed, 0);
            } else {
                std::u16string sub = Utf8ToEngine(entry.mod.id + "  " + entry.mod.manifest.modVersion + "  ");
                sub += ScopeText(entry.mod.manifest.scope);
                own::Text(0, m_x0 + 76, rowY + 24, 246, sub.c_str(), own::kGrey, 0);
            }

            if (entry.on) {
                const bool canUp = i > 0;
                const bool canDown = i + 1 < activeCount;
                DrawButton(m_x0 + 328, rowY + 6, m_x0 + 356, rowY + 38, u"^", canUp ? own::kSea : own::kSand,
                        canUp ? own::kWhite : own::kGrey);
                DrawButton(m_x0 + 360, rowY + 6, m_x0 + 388, rowY + 38, u"v", canDown ? own::kSea : own::kSand,
                        canDown ? own::kWhite : own::kGrey);
            }
        }

        const int pages = PageCount();
        if (pages > 1) {
            DrawButton(m_x0 + 14, m_y0 + 504, m_x0 + 124, m_y0 + 536, Lookup("OBL_MODS_PREVIOUS").c_str(),
                    g_page > 0 ? own::kSea : own::kSand, g_page > 0 ? own::kWhite : own::kGrey);
            const std::u16string pageLabel = Utf8ToEngine(std::to_string(g_page + 1) + " / " + std::to_string(pages));
            own::Text(1, m_x0 + 130, m_y0 + 512, 146, pageLabel.c_str(), own::kInk, 2);
            DrawButton(m_x0 + 282, m_y0 + 504, m_x0 + 392, m_y0 + 536, Lookup("OBL_MODS_NEXT").c_str(),
                    g_page + 1 < pages ? own::kSea : own::kSand, g_page + 1 < pages ? own::kWhite : own::kGrey);
        }
    }

    void DrawRightColumn() const
    {
        const int x0 = m_x0 + 404;
        const int x1 = m_x0 + 788;
        const int innerW = x1 - x0 - 20;

        // Top card: selected mod info.
        own::Fill(x0, m_y0 + 58, x1, m_y0 + 216, own::kFoam);
        own::Frame(x0, m_y0 + 58, x1, m_y0 + 216, own::kBorder);
        const mods::Entry* selected = m_list ? m_list->Find(g_selectedId) : nullptr;
        if (selected) {
            const std::string& title =
                    (selected->mod.valid && !selected->mod.manifest.title.empty()) ? selected->mod.manifest.title : selected->mod.id;
            own::Text(2, x0 + 10, m_y0 + 66, innerW, Utf8ToEngine(title).c_str(), own::kInk, 0);
            int y = m_y0 + 90;
            if (selected->mod.valid) {
                const auto& m = selected->mod.manifest;
                own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_VERSION"), {Utf8ToEngine(m.modVersion)}).c_str(), own::kInk, 0);
                y += 16;
                own::Text(
                        0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_PACKS"), {Utf8ToEngine(Join(m.packs, ", "))}).c_str(), own::kInk, 0);
                y += 16;
                own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_SCOPE"), {ScopeText(m.scope)}).c_str(), own::kInk, 0);
                y += 16;
                if (!m.takesOver.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_TAKES_OVER"), {Utf8ToEngine(Join(m.takesOver, ", "))}).c_str(),
                            own::kInk, 0);
                    y += 16;
                }
                if (!m.actions.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW, Format(Lookup("OBL_MODS_ACTIONS"), {Utf8ToEngine(FormatActions(m.actions))}).c_str(),
                            own::kInk, 0);
                    y += 16;
                }
                if (!m.bindings.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW,
                            Format(Lookup("OBL_MODS_BINDINGS"), {Utf8ToEngine(FormatBindings(m.bindings))}).c_str(), own::kInk, 0);
                    y += 16;
                }
                if (!m.requiredMods.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW,
                            Format(Lookup("OBL_MODS_REQUIRES"), {Utf8ToEngine(Join(m.requiredMods, ", "))}).c_str(), own::kInk, 0);
                    y += 16;
                }
                if (!m.conflictingMods.empty() && y <= m_y0 + 194) {
                    own::Text(0, x0 + 10, y, innerW,
                            Format(Lookup("OBL_MODS_INCOMPATIBLE"), {Utf8ToEngine(Join(m.conflictingMods, ", "))}).c_str(), own::kInk, 0);
                    y += 16;
                }
            }
            if (selected->problem != mods::Problem::None) {
                own::Text(1, x0 + 10, std::min(y + 4, m_y0 + 194), innerW, ProblemText(selected->problem, selected->detail).c_str(),
                        own::kRed, 0);
            }
        }

        // Middle card: settings of the selected mod.
        own::Fill(x0, m_y0 + 224, x1, m_y0 + 402, own::kFoam);
        own::Frame(x0, m_y0 + 224, x1, m_y0 + 402, own::kBorder);
        own::Text(1, x0 + 10, m_y0 + 232, innerW, Lookup("OBL_MODS_SETTINGS").c_str(), own::kSeaDark, 0);
        if (!selected || !selected->mod.valid || selected->mod.manifest.settings.empty()) {
            own::Text(0, x0 + 10, m_y0 + 258, innerW, Lookup("OBL_MODS_NO_SETTINGS").c_str(), own::kGrey, 0);
        } else {
            const auto& active = mod_pack::Active();
            const auto live = std::find_if(active.begin(), active.end(), [&](const auto& m) { return m.manifest.id == g_selectedId; });
            if (!selected->on || selected->problem != mods::Problem::None || live == active.end()) {
                own::Text(0, x0 + 10, m_y0 + 258, innerW, Lookup("OBL_MODS_SETTINGS_OFF").c_str(), own::kGrey, 0);
            } else {
                const auto& fields = selected->mod.manifest.settings;
                const size_t count = std::min(fields.size(), static_cast<size_t>(kMaxSettingRows));
                for (size_t i = 0; i < count; ++i) {
                    const settings_schema::Field& field = fields[i];
                    const int y = m_y0 + 254 + static_cast<int>(i) * 28;
                    own::Text(0, x0 + 10, y + 6, 190, Utf8ToEngine(field.label).c_str(), own::kInk, 0);
                    DrawButton(m_x0 + 610, y, m_x0 + 638, y + 24, u"<", own::kSea, own::kWhite);
                    own::Fill(m_x0 + 640, y, m_x0 + 750, y + 24, own::kWhite);
                    own::Frame(m_x0 + 640, y, m_x0 + 750, y + 24, own::kBorder);
                    const std::string current = mods::ModSettingValue(*live, field);
                    std::u16string shown;
                    if (field.kind == settings_schema::Kind::Toggle)
                        shown = Lookup(settings_schema::IsOn(current) ? "OBL_MODS_ON" : "OBL_MODS_OFF");
                    else
                        shown = Utf8ToEngine(settings_schema::Display(field, current));
                    own::Text(0, m_x0 + 644, y + 6, 102, shown.c_str(), own::kInk, 2);
                    DrawButton(m_x0 + 752, y, m_x0 + 780, y + 24, u">", own::kSea, own::kWhite);
                }
            }
        }

        // Bottom card: conflicts of the active list.
        own::Fill(x0, m_y0 + 410, x1, m_y0 + 548, own::kFoam);
        own::Frame(x0, m_y0 + 410, x1, m_y0 + 548, own::kBorder);
        own::Text(1, x0 + 10, m_y0 + 418, innerW, Lookup("OBL_MODS_CONFLICTS").c_str(), own::kSeaDark, 0);
        if (!m_list || m_list->Conflicts().empty()) {
            own::Text(0, x0 + 10, m_y0 + 444, innerW, Lookup("OBL_MODS_NO_CONFLICTS").c_str(), own::kGrey, 0);
        } else {
            own::Text(0, x0 + 10, m_y0 + 438, innerW, Lookup("OBL_MODS_CONFLICT_HINT").c_str(), own::kGrey, 0);
            const auto& conflicts = m_list->Conflicts();
            const size_t count = std::min(conflicts.size(), static_cast<size_t>(kMaxConflictRows));
            for (size_t i = 0; i < count; ++i) {
                const mods::Conflict& c = conflicts[i];
                const std::u16string line = c.winner.empty()
                        ? Utf8ToEngine(c.what)
                        : Format(Lookup("OBL_MODS_CONFLICT_VALUE"), {Utf8ToEngine(c.winner), Utf8ToEngine(c.loser), Utf8ToEngine(c.what)});
                own::Text(0, x0 + 10, m_y0 + 458 + static_cast<int>(i) * 20, innerW, line.c_str(), own::kRed, 0);
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

bool FindTarget(std::string_view name, int& x, int& y)
{
    if (!g_openPanel || !g_openPanel->m_visible) return false;
    for (std::string_view prefix : {"toggle.", "select.", "up.", "down."}) {
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
