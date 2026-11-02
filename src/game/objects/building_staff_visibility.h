#pragma once

#include <cstdint>

// The staff visibility of a Building (docs/architecture/objects.md): HideStaffAndEvents and
// ShowStaffAndEvents run when a building closes/opens. They walk three lists of object ids and flip the
// visibility byte of the performers, then do the same for the show (BuildingEvents). No game memory here: the glue in
// src/game/objects/building_methods.cpp supplies an `Env` that looks objects up and writes, and the unit tests supply a fake one.
//
// Env contract (ids are object ids):
//   int  ClassOf(int32_t id)              Entity::m_type of the object, or -1 when there is no such object
//   void SetVisible(int32_t id, bool v)   Entity::m_visible of that object (called for existing objects only)
//   bool WaiterOf(int32_t tableId, int32_t& waiterId)
//                                         true when the table exists and its definition has plates (TableDefinition::m_hasPlates);
//                                         `waiterId` is Furniture::m_WaiterID
//   bool HasEvents()                      Building::m_events != null
//   int  ActiveEventId()                  the active event id (-1 when no show is active)
//   void HideEventActors()                hiding event actors
//   void CancelEvent()                    cancelling the current event
//   void ShowEventActors()                showing event actors
namespace obl {

// The lists of the building. `staff`/`busy` hold the serving staff (males then females, `serving` entries, the busy flag per entry),
// `nonServe` the other performers and `tables` the table object ids (a signed count: a negative one walks nothing).
struct StaffLists {
    uint32_t serving = 0;
    const int32_t* staff = nullptr;
    uint8_t* busy = nullptr;
    uint32_t nonServe = 0;
    const int32_t* nonServeIds = nullptr;
    int32_t tables = 0;
    const int32_t* tableIds = nullptr;
};

// Object classes whose visibility follows the building: ServiceStaff 6 and the performers Cabaret 0x0e, Singer 0x0f, WeightLifter 0x10,
// Catwalk 0x11, WetTShirt 0x12 (the order of the tests does not matter).
constexpr bool IsStaffClass(int objectClass)
{
    return objectClass == 6 || (objectClass >= 0x0e && objectClass <= 0x12);
}

namespace detail {

template<class Env> void SetPerformersVisible(Env& env, const StaffLists& lists, bool visible, bool clearBusy)
{
    for (uint32_t i = 0; i < lists.serving; ++i) {
        const int32_t id = lists.staff[i];
        if (!IsStaffClass(env.ClassOf(id))) continue;
        if (clearBusy) lists.busy[i] = 0;
        env.SetVisible(id, visible);
    }
    for (uint32_t i = 0; i < lists.nonServe; ++i) {
        const int32_t id = lists.nonServeIds[i];
        if (IsStaffClass(env.ClassOf(id))) env.SetVisible(id, visible);
    }
    for (int32_t i = 0; i < lists.tables; ++i) {
        int32_t waiter = 0;
        if (env.WaiterOf(lists.tableIds[i], waiter) && env.ClassOf(waiter) >= 0) env.SetVisible(waiter, visible);
    }
}

} // namespace detail

// HideStaffAndEvents: every performer and table waiter becomes invisible, the serving staff also loses its busy flag; a show in progress
// has its actors hidden and is cancelled.
template<class Env> void HideStaffAndEvents(Env& env, const StaffLists& lists)
{
    detail::SetPerformersVisible(env, lists, false, true);
    if (env.HasEvents() && env.ActiveEventId() != -1) {
        env.HideEventActors();
        env.CancelEvent();
    }
}

// ShowStaffAndEvents: the reverse, without touching the busy flags; the show's actors are shown (also when no show is active: the
// reference does not ask).
template<class Env> void ShowStaffAndEvents(Env& env, const StaffLists& lists)
{
    detail::SetPerformersVisible(env, lists, true, false);
    if (env.HasEvents()) env.ShowEventActors();
}

} // namespace obl
