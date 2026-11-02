#pragma once

// Native records; its old geometry checks are retired.
namespace obl::disaster {

struct DisasterPlacement {
    char m_Name[64];
    int m_EventIndex;
    int m_Rect[4];
    float m_Position[3];
};

} // namespace obl::disaster
