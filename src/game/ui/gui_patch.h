#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// GUI layout patches (`gui/<layout>.patch.json`, format `obl.gui_patch` v1): control IDs (explicit `id=...` or
// deterministic derivation), named slots (row/column auto-layout in load order), screen/dialog anchors, and patch
// operations (`add_dialog`, `add_slot`, `add_control`, `hide_control`, `move_control`, `set_anchor`).
//
// Pure portable logic: no Windows APIs, no dependency on presentation script runtimes. Kept in its own module so the
// scope check can classify `gui/*.patch.json` as a presentation file.
namespace obl::gui_patch {

inline constexpr const char* kFormat = "obl.gui_patch";
inline constexpr int kVersion = 1;
inline constexpr int32_t kReferenceWidth = 800;
inline constexpr int32_t kReferenceHeight = 600;

enum class Anchor : uint8_t {
    None,
    Left,
    Right,
    Top,
    Bottom,
    Center,
    CenterX,
    CenterY,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Stretch,
    StretchX,
    StretchY,
};

enum class SlotDirection : uint8_t {
    Column,
    Row,
};

struct Rect {
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
    bool operator==(const Rect&) const = default;
};

struct ControlBinding {
    std::string id;
    std::string containerName; // DialogItem name, "MainItem", or "MainItem640"
    size_t childIndex = 0; // 0-based index among emitted children of the container
    bool fromMod = false; // added by a patch (add_control, add_dialog), not part of the base layout
};

struct PatchSource {
    std::string modId;
    std::string file; // display path for error messages
    std::string jsonText;
};

struct ApplyResult {
    std::string scriptText; // emitted .gui text ready for GuiScriptParse (without #obl-gui 1 header)
    std::vector<ControlBinding> bindings; // control ID -> emitted container/child index
};

// Parses an anchor name ("left", "right", "center", "centre", "stretch", ...).
bool ParseAnchor(std::string_view name, Anchor& out);

// Applies an anchor to `rect` given its reference container size (`refW`, `refH`) and target size (`targetW`, `targetH`).
Rect ApplyAnchor(Rect rect, Anchor anchor, int32_t refW, int32_t refH, int32_t targetW, int32_t targetH);

// Validates a `Mod:<mod_id>.<event>` button action string. Returns false if it does not start with "Mod:".
// When it does start with "Mod:", sets `modId` and `event` if valid, or sets `error` if malformed or inactive.
bool IsModAction(std::string_view action);
bool ValidateModAction(
        std::string_view action, std::span<const std::string> activeMods, std::string& modId, std::string& event, std::string& error);

// Parses a `.gui` script (without the `#obl-gui 1` header) and validates that all explicit and derived control IDs are
// unique. Populates `ids` in declaration order on success; on collision or parse error returns false with `error`.
bool DeriveControlIds(std::string_view scriptText, std::vector<std::string>& ids, std::string& error);

// Parses `baseScriptText` (without `#obl-gui 1`), applies `patches` in order, lays out slots, resolves anchors for
// (`screenWidth`, `screenHeight`), validates `Mod:<id>.<event>` actions against `activeMods`, and emits the patched
// `.gui` text and control ID bindings.
bool ApplyPatches(std::string_view baseScriptText, std::span<const PatchSource> patches, std::span<const std::string> activeMods,
        int32_t screenWidth, int32_t screenHeight, ApplyResult& out, std::string& error, std::vector<std::string>* errors = nullptr);

// Discovers `gui/<layout>.patch.json` files across `pack_path::Layers(pack)` in load order (bottom-up, later mod wins).
std::vector<std::filesystem::path> FindPatchFiles(const std::filesystem::path& pack, std::string_view layout);

// Convenience: reads discovered patch files for `layout` and applies them on top of `baseScriptText`, after `leading` (the
// engine's own layer, game/ui/own_layer.h, which sits under the mods). If there is no patch at all, sets
// `out.scriptText = baseScriptText` untouched and returns true.
bool ApplyPackPatches(const std::filesystem::path& pack, std::string_view layout, std::string_view baseScriptText,
        std::span<const std::string> activeMods, int32_t screenWidth, int32_t screenHeight, ApplyResult& out, std::string& error,
        std::span<const PatchSource> leading = {}, std::vector<std::string>* errors = nullptr);

} // namespace obl::gui_patch
