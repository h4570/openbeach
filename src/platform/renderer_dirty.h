#pragma once

#include "game/ui/renderer_types.h"
#include "core/rect.h"

#include <cstddef>

// AddDirtyRect over any list of rectangles (docs/architecture/presentation-render.md, "Dirty rectangles"). The renderer keeps
// the list in its m_dirtyRects deque, which renderer_helpers.cpp adapts; the unit tests use a vector. The list needs
//   size_t size(), Rect& at(size_t), void swap_remove(size_t) (move the last element over `at`, drop the last), void push_back(const Rect&).
// The algorithm only understands rectangles that overlap with a corner (no merging of crossing or side-by-side ones): the first stored
// rectangle that does is cut away from the new one and what is left is added again, recursively; anything else is just stored.
namespace obl::renderer_ops {

template<class List> void AddDirtyRect(List& list, int left, int top, int right, int bottom)
{
    using screen::Rect;
    bool overlapped = false;
    Rect c{};
    for (size_t i = 0; i < list.size();) {
        const Rect r = list.at(i);
        // Already covered by a stored rectangle: nothing to add.
        if (r.left <= left && right <= r.right && r.top <= top && bottom <= r.bottom) return;
        // A stored rectangle covered by the new one is dropped (the last one takes its place and is looked at next).
        if (!(r.left < left || right < r.right || r.top < top || bottom < r.bottom)) {
            list.swap_remove(i);
            continue;
        }
        // A corner of the new rectangle inside the stored one: a vertical edge inside its x span and a horizontal edge inside its y span.
        const bool xInside = (r.left < left && left < r.right) || (r.left < right && right < r.right);
        if (xInside) {
            const bool yInside =
                    (r.top < top && !(r.bottom <= top)) || ((r.top >= top || r.bottom <= top) && r.top < bottom && bottom < r.bottom);
            if (yInside) {
                c = r;
                overlapped = true;
                break; // the loop ends after the first overlapping rectangle
            }
        }
        ++i;
    }
    if (overlapped) {
        if (c.left < left && left < c.right && c.right < right) {
            if (top < c.top && c.bottom < bottom) {
                AddDirtyRect(list, c.right, c.top, right, c.bottom);
                AddDirtyRect(list, left, top, right, c.top);
                AddDirtyRect(list, left, c.bottom, right, bottom);
                return;
            }
            if (c.top < top && top < c.bottom && c.bottom < bottom) {
                AddDirtyRect(list, c.right, top, right, bottom);
                AddDirtyRect(list, left, c.bottom, c.right, bottom);
                return;
            }
            if (c.top < bottom && bottom < c.bottom && top < c.top) {
                AddDirtyRect(list, c.right, top, right, bottom);
                AddDirtyRect(list, left, top, c.right, c.top);
                return;
            }
            AddDirtyRect(list, c.right, top, right, bottom);
            return;
        }
        if (c.left < right && right < c.right && left < c.left) {
            if (top < c.top) {
                if (c.bottom < bottom) {
                    AddDirtyRect(list, left, c.top, c.left, c.bottom);
                    AddDirtyRect(list, left, top, right, c.top);
                    AddDirtyRect(list, left, c.bottom, right, bottom);
                    return;
                }
            } else if (top <= c.bottom && c.bottom < bottom) {
                AddDirtyRect(list, left, top, c.left, bottom);
                AddDirtyRect(list, c.left, c.bottom, right, bottom);
                return;
            }
            if (c.top < bottom && bottom < c.bottom && top < c.top) {
                AddDirtyRect(list, left, top, c.left, bottom);
                AddDirtyRect(list, c.left, top, right, c.top);
                return;
            }
            AddDirtyRect(list, left, top, c.left, bottom);
            return;
        }
        if (top <= c.top || c.bottom <= top || bottom <= c.bottom) {
            if (bottom <= c.top || c.bottom <= bottom || c.top <= top) return;
            if (left < c.left && c.right < right) {
                AddDirtyRect(list, left, top, c.left, bottom);
                AddDirtyRect(list, c.right, top, right, bottom);
                AddDirtyRect(list, c.left, top, c.right, c.top);
                return;
            }
            AddDirtyRect(list, left, top, right, c.top);
            return;
        }
        if (left < c.left && c.right < right) {
            AddDirtyRect(list, left, top, c.left, bottom);
            AddDirtyRect(list, c.right, top, right, bottom);
            AddDirtyRect(list, c.left, c.bottom, c.right, bottom);
            return;
        }
        AddDirtyRect(list, left, c.bottom, right, bottom);
        return;
    }
    list.push_back({left, top, right, bottom});
}

} // namespace obl::renderer_ops
