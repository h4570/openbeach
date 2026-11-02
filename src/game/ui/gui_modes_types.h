#pragma once

#include "game/ui/gui_types.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>

namespace obl::ui {

struct GuiManager; // gui_manager_types.h

// What GuiManager::m_modeObject points at. The callbacks a mode does not need do nothing; Update and the two mouse callbacks have one
// shared answer in GuiModeCommon, which needs the game (the unit tests' modes derive from this class directly).
struct GuiMode {
    explicit GuiMode(GuiManager* manager) : m_manager(manager) {}
    GuiMode(const GuiMode&) = delete;
    GuiMode& operator=(const GuiMode&) = delete;
    virtual ~GuiMode() = default;

    virtual void Enter() {}
    virtual void Exit() {}
    virtual void Update(float dt) = 0;
    virtual void DrawWorld() {}
    virtual void DrawScreen() {}
    virtual bool OnMouseWheel(int x, int y, int delta) = 0;
    virtual bool OnMouse(int x, int y, int left, int right) = 0;

    GuiManager* m_manager;
};

// The base of every game mode: the ticker animates, the wheel and the mouse are not taken.
struct GuiModeCommon : GuiMode {
    explicit GuiModeCommon(GuiManager* manager);
    ~GuiModeCommon() override;

    void Update(float dt) override;
    bool OnMouseWheel(int x, int y, int delta) override;
    bool OnMouse(int x, int y, int left, int right) override;
};

// GuiModePaint (mode 0): the debug tool that paints tile flags.
struct GuiModePaint : GuiModeCommon {
    using GuiModeCommon::GuiModeCommon;
    void Update(float dt) override;
};

// GuiModeNone (mode 6): the front end, where nothing updates.
struct GuiModeNone : GuiModeCommon {
    using GuiModeCommon::GuiModeCommon;
    void Update(float) override {}
};

// Node of a std::list<int> (from the small-block pool); the list head is a sentinel node of the same shape.
struct GuiModeCameraThumb {
    GuiModeCameraThumb* m_next;
    GuiModeCameraThumb* m_prev;
    int32_t m_surface; // Renderer surface index of one photo thumbnail
};

// GuiModeCamera (mode 2): the photo mode. Update stores its dt and DrawScreen hands it to every sprite
// draw as the animation time.
struct GuiModeCamera : GuiModeCommon {
    explicit GuiModeCamera(GuiManager* manager);
    ~GuiModeCamera() override;

    void Enter() override;
    void Exit() override;
    void Update(float dt) override;
    void DrawScreen() override;
    void ComposePhoto(int photo);
    bool OnMouse(int x, int y, int left, int right) override;

    float m_dt{}; // last Update dt
    GuiModeCameraThumb*
            m_thumbs{}; // sentinel of the thumbnail list (the constructor allocates it, Enter clears it, Exit releases the surfaces)
};

// GuiModeDefault (mode 4): selection by click and rectangle (OnMouse). The press stores the world point under
// the cursor, holding the button outside the panels starts the drag, the release stores the second corner.
struct GuiModeDefault : GuiModeCommon {
    using GuiModeCommon::GuiModeCommon;

    void Enter() override;
    void Exit() override;
    void Update(float dt) override;
    bool SelectUnderCursor();
    void SelectGuestsInRectangle();
    bool OnMouse(int x, int y, int left, int right) override;
    void DrawScreen() override;

    float m_pressWorld[3]{}; // world point of the last left press that picked nothing
    float m_releaseWorld[3]{}; // world point where the drag ended
    uint8_t m_dragging{}; // cleared by Enter; the held button is stretching the selection rectangle
    uint8_t m_pressed{}; // cleared by Enter; a press on the map is waiting to become a drag
    uint8_t m_selectionShown{}; // set by the draw of the selection frame; when set, the release selects the guests inside the rectangle
    int32_t m_selectedGuestCount{}; // guests selected by the last rectangle release; cleared by Enter, and by a press that picked nothing
};

// GuiModeSendStaff (mode 9): the target of a worker sent by hand (OnMouse).
struct GuiModeSendStaff : GuiModeCommon {
    using GuiModeCommon::GuiModeCommon;

    void Enter() override;
    bool CanDirect(uint32_t* tile, int* targetId);
    bool OnMouse(int x, int y, int left, int right) override;
    void DrawWorld() override;

    void* m_target{}; // the object whose id is in GuiManager context[9], looked up at Enter
    uint32_t m_tile[2]{}; // tile under the cursor (x, y), refreshed every OnMouse
    uint8_t m_canDirect{}; // CanDirect's answer for that tile
    uint8_t m_overMap{}; // OnMouse: the cursor is over the map, not a panel
    uint8_t m_targetsObject{}; // OnMouse sets it when the cursor picks a worker target
};

// GuiModeDozer (mode 10): scenery removal (OnMouse).
struct GuiModeDozer : GuiModeCommon {
    using GuiModeCommon::GuiModeCommon;

    void Enter() override { m_canRemove = 0; }
    void Exit() override;
    bool CanRemove(uint32_t* tile);
    bool OnMouse(int x, int y, int left, int right) override;
    void DrawWorld() override;

    uint32_t m_tile[2]{}; // tile under the cursor (x, y), refreshed every OnMouse
    uint8_t m_canRemove{}; // cleared by Enter; CanRemove's answer for that tile
    uint8_t m_overMap{}; // OnMouse: the cursor is over the map, not a panel
};

// The place modes count their own lifecycle (`call-report ui.gui-modes-place`).
struct GuiModePlaceBase : GuiModeCommon {
    explicit GuiModePlaceBase(GuiManager* manager);
    ~GuiModePlaceBase() override;
};

struct GuiModePlaceScenery : GuiModePlaceBase {
    using GuiModePlaceBase::GuiModePlaceBase;

    void Enter() override;
    void Exit() override; // Exit owns the preview: destruction alone leaves it to the sprite bank
    void Update(float dt) override;
    void DrawWorld() override;
    bool OnMouseWheel(int x, int y, int delta) override;
    bool OnMouse(int x, int y, int left, int right) override;

    tables::SceneryDef* m_definition{};
    SpriteInstance* m_preview{};
    int32_t m_definitionId{};
    uint16_t m_validColor{}, m_invalidColor{};
    uint32_t m_tileX{}, m_tileY{}; // the tile under the cursor, then the footprint's origin (OnMouse adjusts it in place)
    int32_t m_lastX = -1, m_lastY = -1; // the origin of the last placement
};

struct GuiModePlaceGrass : GuiModePlaceBase {
    using GuiModePlaceBase::GuiModePlaceBase;

    void Enter() override;
    void Exit() override;
    void DrawWorld() override;
    bool OnMouse(int x, int y, int left, int right) override;

    tables::SceneryDef* m_definition{};
    SpriteInstance* m_preview{};
    int32_t m_definitionId{};
    uint8_t m_canPlace{};
    int32_t m_lastX = -1, m_lastY = -1; // the last tile PlaceTile paid for
    uint32_t m_tileX{}, m_tileY{}; // the tile under the cursor, written by OnMouse
};

struct GuiModePlacePavement : GuiModePlaceBase {
    using GuiModePlaceBase::GuiModePlaceBase;

    void Enter() override;
    void Exit() override;
    void DrawWorld() override;
    bool OnMouse(int x, int y, int left, int right) override;

    int32_t m_definitionId{};
    tables::SceneryDef* m_definition{};
    SpriteInstance* m_preview{};
    uint8_t m_canPlace{};
    int32_t m_lastX = -1, m_lastY = -1; // the last tile PlaceTile paid for
    uint32_t m_tileX{}, m_tileY{}; // the tile under the cursor, written by OnMouse
};

struct GuiModePlaceBuilding : GuiModePlaceBase {
    using GuiModePlaceBase::GuiModePlaceBase;

    void Enter() override;
    void Exit() override;
    void Update(float dt) override;
    void DrawWorld() override;
    bool OnMouseWheel(int x, int y, int delta) override;

    SpriteInstance* m_preview{};
    tables::BuildingDef* m_definition{};
    uint16_t m_invalidColor{}, m_neutralColor{}, m_validColor{};
    uint8_t m_cancelHeld{};
    int32_t m_savedHotspotX{};
    uint8_t m_canPlace{}; // 1 after Enter, then the answer of CanPlace for the tile under the cursor (Update)
};

} // namespace obl::ui
