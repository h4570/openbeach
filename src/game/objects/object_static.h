#pragma once

#include "core/pointer_width.h"
#include "game/objects/object_base.h"
#include "game/objects/object_fields.h"

#include <cstdint>

// Prop: the grid footprint and the
// cleaner/mechanic claims every grid object shares; the base of Scenery, Building and Furniture. The logic is
// prop.h. The size stays 0x60: Scenery and Building still extend it with offset checks.
namespace obl::object {

struct Prop : Base {
    Prop() = default;
    explicit Prop(const char* name);
    // The slots that differ from the parent's (prop.cpp; the unit tests' trap seam).
    bool Update() override;
    bool ReceiveMessage(messages::Message* message) override;

    // Each setter stores one byte.
    void SetGridWidth(uint32_t width);
    void SetGridHeight(uint32_t height);
    void SetCleanerClaimed(uint32_t claimed);
    void SetMechanicClaimed(uint32_t claimed);
    void SetCalledRepairing(uint32_t called);
    void SetUrgentCleaningCalled(uint32_t called);
    void SetUrgentRepairCalled(uint32_t called);
    bool IsCleanerClaimed() const;
    bool IsMechanicClaimed() const;

    uint8_t m_gridW = 0; // footprint in tiles
    uint8_t m_gridH = 0;
    uint8_t m_cleaning = 0; // a cleaner is working on it (CleaningManager)
    uint8_t m_repairing = 0; // a mechanic is working on it (RepairManager)
    uint8_t m_calledCleaning = 0;
    uint8_t m_calledRepairing = 0;
    uint8_t m_urgentCleaningCalled = 0; // the urgent calls; the save reuses the two labels above for them
    uint8_t m_urgentRepairCalled = 0;
};

// The save records of Prop, then Entity's (the caller). Visitor: Byte/Bool(key, field, hashed). The last two entries
// have their own keys (the converter tells the reused labels apart by occurrence). All of them are hashed.
template<SelfOf<Prop> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Byte("static.grid_w", o.m_gridW, Hashed::Yes);
    v.Byte("static.grid_h", o.m_gridH, Hashed::Yes);
    v.Bool("static.cleaner_claimed", o.m_cleaning, Hashed::Yes);
    v.Bool("static.mechanic_claimed", o.m_repairing, Hashed::Yes);
    v.Bool("static.called_cleaning", o.m_calledCleaning, Hashed::Yes);
    v.Bool("static.called_repairing", o.m_calledRepairing, Hashed::Yes);
    v.Bool("static.urgent_cleaning", o.m_urgentCleaningCalled, Hashed::Yes);
    v.Bool("static.urgent_repairing", o.m_urgentRepairCalled, Hashed::Yes);
}

} // namespace obl::object
