#pragma once

#include "core/text16.h"
#include "game/ui/sprite.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"
#include "game/messages/message_state.h"
#include <cstring>
#include <cwchar>

namespace obl::scenery {

template<class Env> void OnSelect(object::Scenery& s, bool clicked, bool center, Env& env)
{
    if (clicked) env.ShowDialog(s.m_definition->m_Menu.c_str(), &s);
    if (center) {
        env.ForgetSelection();
        int x, y;
        env.WorldToInt(s.m_WorldXYZ, x, y);
        const double divisor = static_cast<double>(env.Zoom()) * 2.0;
        x -= static_cast<int>(static_cast<double>(env.Width()) / divisor);
        y -= static_cast<int>(static_cast<double>(env.Height()) / divisor);
        env.SetCamera(x, y);
        env.StopCamera();
    }
}

inline const char* RepairText(unsigned status, bool called, bool urgent)
{
    switch (status) {
    case 0: return "BI_REPAIR_NO_MECHANICS";
    case 1: return "BI_REPAIR_CANT_REACH";
    case 2: return called || urgent ? "BI_REPAIR_WAITING" : "BI_REPAIR_NOT_NEEDED";
    case 3: return "BI_REPAIR_REQUIRED";
    case 4: return "BI_INSPECT_REQUIRED";
    case 5: return "BI_REPAIR_MECHANIC_COMING";
    case 6: return "BI_REPAIR_REPAIRING";
    default: return nullptr;
    }
}

inline const char* CleaningText(unsigned status)
{
    switch (status) {
    case 1: return "BI_CLEAN_NO_CLEANERS";
    case 3: return "BI_CLEAN_REQUIRED";
    case 4: return "BI_CLEAN_ONROUTE";
    case 5: return "BI_CLEAN_CLEANING";
    default: return "BI_CLEAN_NOT_NEEDED";
    }
}

template<class Env> void GetVariableText(const object::Scenery& s, const char* key, Char16* out, Env& env)
{
    if (std::strcmp(key, "BIN_LITTER") == 0) return env.PrintInt(out, s.m_NumItems);
    if (std::strcmp(key, "BI_CALL_CLEANER_STATE") == 0) return env.PrintInt(out, env.Count(11) != 0 && s.m_urgentCleaningCalled == 0);
    if (std::strcmp(key, "BI_CALL_MECHANIC_STATE") == 0) return env.PrintInt(out, env.Count(8) != 0 && s.m_urgentRepairCalled == 0);
    const char* text;
    if (std::strcmp(key, "SCENERY_REPAIRSTATUS") == 0)
        text = RepairText(env.RepairStatus(s.m_id), s.m_calledRepairing != 0, s.m_urgentRepairCalled != 0);
    else if (std::strcmp(key, "SCENERY_CLEANINGSTATUS") == 0)
        text = CleaningText(env.CleaningStatus(s.m_id));
    else
        return env.Fatal(key);
    if (text) text16::Copy(out, env.Lookup(text));
}

template<class Env> void Vandalise(object::Scenery& s, Env& env)
{
    auto& sprite = *static_cast<ui::SpriteInstance*>(s.m_sprite);
    if (s.m_vandalised != 1) {
        s.m_vandalised = 1;
        s.m_RepairState = 1;
        s.m_PreVandlismAnim = sprite.m_animIndex;
        messages::Message message{};
        message.sender = s.m_id;
        message.receiver = s.m_id;
        message.type = 84;
        env.Post(message);
    }
    const char* anim = nullptr;
    if (env.EqualName(sprite.m_name, "street_lamp"))
        anim = "StreetLamp_Van_2Static";
    else if (env.EqualName(sprite.m_name, "bin"))
        anim = s.m_NumItems == s.m_definition->m_Capacity ? "fullbin_vandalise_static" : "emptybin_vandalise_static";
    else if (env.EqualName(sprite.m_name, "bench"))
        anim = "bench_vandalise_static";
    if (anim) env.Animate(s.m_sprite, anim, env.Time());
}

} // namespace obl::scenery
