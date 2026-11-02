#pragma once

#include "core/char16.h"
#include "game/objects/object_base.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"

#include <bit>
#include <cstdint>

namespace obl::gui {

// Frees a widget through its virtual destructor (flags & 1) or only runs the destructor in place.
inline void DestroyWidget(ui::GuiWidget& w, uint32_t flags = 1)
{
    if (flags & 1)
        delete &w; // NOLINT(cppcoreguidelines-owning-memory): widget trees hold raw pointers until the ownership item
    else
        w.~GuiWidget();
}

} // namespace obl::gui

// A dialog's variable provider (a world object, object::Base): GetVariableText(key, out) fills the dialog's texts,
// OnSelect(clicked, centerCamera) selects a world object, and ReduceSelectionToHover runs when the provider's dialog closes.
namespace obl::gui::provider_virtual {

// Every provider is an object::Base; its slots are the C++ virtuals.
inline object::Base& Of(void* provider)
{
    return *static_cast<object::Base*>(provider);
}
inline void GetVariableText(void* provider, const char* key, void* out)
{
    Of(provider).GetVariableText(key, static_cast<Char16*>(out));
}
inline void Select(void* object, uint32_t clicked, uint32_t centerCamera)
{
    Of(object).OnSelect(clicked, centerCamera);
}
inline void Closed(void* provider)
{
    Of(provider).ReduceSelectionToHover();
}

} // namespace obl::gui::provider_virtual
