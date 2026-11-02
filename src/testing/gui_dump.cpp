// gui-dump: a check of the GUI object graph (docs/architecture/data-formats/converter/gui-runtime.md). Reads the widgets the script parser created
// only through the ui:: records and writes them as JSON; tools/check_gui_dump.py compares the tree with the GUI script
// text and the class vtables, so a wrong child list fails a test. Every pointer is checked with platform::MemoryReadable
// before it is followed and every list is bounded, so a broken object graph fails the command instead of crashing the game.

#include "core/char16.h"
#include "gui_dump.h"
#include "gui_tree.h"

#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "test_util.h"
#include "core/wide_path.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace obl::test {
namespace {

using namespace ui;
using namespace gui;

std::filesystem::path g_file;

// Raw bytes >= 0x80 are written as \u00XX like worldparams-dump (the game's text is single-byte).
void WriteString(std::ostream& out, const char* text)
{
    out << '"';
    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(text); *c; ++c) {
        if (*c == '"' || *c == '\\')
            out << '\\' << *c;
        else if (*c < 0x20 || *c >= 0x80) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", *c);
            out << escaped;
        } else
            out << *c;
    }
    out << '"';
}

void WriteText(std::ostream& out, const char* text)
{
    std::string copy;
    if (!text)
        out << "null";
    else if (SafeString(text, 512, copy))
        WriteString(out, copy.c_str());
    else
        out << "null";
}

class Dumper {
public:
    bool Run(const GuiManager& gui, std::string& out)
    {
        // Seeds: the flat widget list, the dialogs and the main container; children are followed from each.
        std::vector<const GuiWidget*> flat, dialogs;
        if (!Chain(gui.m_widgets, flat, "widgets") ||
                !Chain(reinterpret_cast<const GuiManagerWidgetNode*>(gui.m_definitions), dialogs, "dialogs"))
            return false;
        const GuiWidget* main = gui.m_mainItem ? View<GuiWidget>(gui.m_mainItem) : nullptr;
        if (gui.m_mainItem && !main) return Fail("main container unreadable");

        std::vector<const GuiWidget*> seeds = flat;
        seeds.insert(seeds.end(), dialogs.begin(), dialogs.end());
        if (main) seeds.push_back(main);
        for (const auto* seed : seeds)
            if (!Visit(*seed, 0)) return false;

        std::ostringstream json;
        json << "{\"render\":[" << game::GetRenderer()->m_width << ',' << game::GetRenderer()->m_height << "]";
        json << ",\"manager\":{\"selection\":" << gui.m_selection << ",\"mode\":" << gui.m_mode
             << ",\"requestedMode\":" << gui.m_requestedMode << ",\"provider\":" << reinterpret_cast<uintptr_t>(gui.m_variableProvider)
             << ",\"main\":" << reinterpret_cast<uintptr_t>(main) << ",\"modeObject\":" << reinterpret_cast<uintptr_t>(gui.m_modeObject)
             << '}';
        Addresses(json, "flat", flat);
        Addresses(json, "dialogs", dialogs);
        json << ",\"open\":[";
        if (!Open(gui, json)) return false;
        json << "],\"widgets\":[" << m_widgets.str() << "]}\n";
        out = json.str();
        return true;
    }

    size_t Count() const { return m_seen.size(); }
    size_t FlatCount() const { return m_flat; }

private:
    bool Fail(const char* why)
    {
        OBL_LOG("autotest: FAILED gui-dump %s", why);
        return false;
    }

    bool Chain(const GuiManagerWidgetNode* head, std::vector<const GuiWidget*>& out, const char* what)
    {
        if (!ChainWidgets(head, out)) return Fail(what);
        if (what[0] == 'w') m_flat = out.size();
        return true;
    }

    static void Addresses(std::ostream& out, const char* key, const std::vector<const GuiWidget*>& widgets)
    {
        out << ",\"" << key << "\":[";
        for (size_t i = 0; i < widgets.size(); ++i) out << (i ? "," : "") << reinterpret_cast<uintptr_t>(widgets[i]);
        out << ']';
    }

    bool Open(const GuiManager& gui, std::ostream& json)
    {
        const GuiManagerDialogNode* sentinel = gui.m_dialogs;
        if (!Readable(sentinel, sizeof(*sentinel))) return Fail("open-dialogs list unreadable");
        size_t count = 0;
        for (const auto* node = sentinel->m_next; node != sentinel; node = node->m_next) {
            if (!Readable(node, sizeof(*node)) || ++count > kMaxWidgets) return Fail("open-dialogs list");
            const auto* entry = View<GuiManagerDialogEntry>(node->m_entry);
            const auto* dialog = entry ? View<GuiDialogData>(entry->m_dialog) : nullptr;
            if (!dialog) return Fail("open dialog entry unreadable");
            json << (count > 1 ? "," : "") << "{\"dialog\":" << reinterpret_cast<uintptr_t>(dialog) << ",\"state\":" << entry->m_state
                 << ",\"provider\":" << reinterpret_cast<uintptr_t>(entry->m_provider) << ",\"surface\":" << (entry->m_surface != 0) << '}';
        }
        return true;
    }

    bool Visit(const GuiWidget& w, int depth)
    {
        if (depth > kMaxDepth) return Fail("tree deeper than 64 levels");
        if (!m_seen.insert(&w).second) return true;
        if (m_seen.size() > kMaxWidgets) return Fail("too many widgets");

        Children children;
        if (!CollectChildren(w, children)) {
            OBL_LOG("autotest: FAILED gui-dump children of widget %p type %d unreadable", static_cast<const void*>(&w), w.m_type);
            return false;
        }
        std::ostream& out = m_widgets;
        out << (m_seen.size() > 1 ? "," : "") << "{\"addr\":" << reinterpret_cast<uintptr_t>(&w) << ",\"class\":\"" << w.ClassName()
            << "\",\"type\":" << w.m_type << ",\"id\":" << w.m_id << ",\"rect\":[" << w.m_x0 << ',' << w.m_y0 << ',' << w.m_x1 << ','
            << w.m_y1 << "],\"base\":[" << w.m_baseX0 << ',' << w.m_baseY0 << ',' << w.m_baseX1 << ',' << w.m_baseY1
            << "],\"visible\":" << int(w.m_visible) << ",\"hovered\":" << int(w.m_hovered) << ",\"held\":" << int(w.m_held)
            << ",\"clicked\":" << int(w.m_clicked) << ",\"dirty\":" << int(w.m_dirty) << ",\"tooltip\":";
        WriteText(out, w.m_tooltip);
        if (w.m_type == static_cast<int32_t>(GuiType::Dialog)) {
            const auto& d = reinterpret_cast<const GuiDialogData&>(w);
            out << ",\"name\":";
            WriteText(out, d.m_name);
            out << ",\"dialogId\":" << d.m_positionKey << ",\"modal\":" << int(d.m_modal) << ",\"draggable\":" << int(d.m_movable)
                << ",\"animating\":" << int(d.m_animating) << ",\"titleBar\":" << reinterpret_cast<uintptr_t>(d.m_caption);
        }
        if (children.header) out << ",\"header\":" << reinterpret_cast<uintptr_t>(children.header);
        out << ",\"children\":[";
        for (size_t i = 0; i < children.widgets.size(); ++i) out << (i ? "," : "") << reinterpret_cast<uintptr_t>(children.widgets[i]);
        out << "]}";
        // The widgets are written in discovery order; the children after their parent (their own records follow it).
        const auto kids = children.widgets;
        for (const auto* child : kids)
            if (!Visit(*child, depth + 1)) return false;
        if (children.header && !Visit(*children.header, depth + 1)) return false;
        return true;
    }

    std::set<const void*> m_seen;
    std::ostringstream m_widgets;
    size_t m_flat = 0;
};

} // namespace

void InitGuiDump(const Text16& gameDirectory)
{
    g_file = WidePath(gameDirectory) / "gui-dump.json";
}

bool ExecuteGuiDumpCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "gui-dump") return false;
    const GuiManager* gui = Manager();
    std::string text;
    Dumper dumper;
    if (!args.empty() || !gui) {
        OBL_LOG("autotest: FAILED gui-dump needs a GUI manager and no arguments");
    } else if (dumper.Run(*gui, text)) {
        std::ofstream file(g_file, std::ios::binary | std::ios::trunc);
        file << text;
        if (!file) {
            OBL_LOG("autotest: FAILED gui-dump cannot write %s", g_file.string().c_str());
            return true;
        }
        OBL_LOG("autotest: gui-dump widgets=%zu flat=%zu bytes=%u", dumper.Count(), dumper.FlatCount(), static_cast<unsigned>(text.size()));
    }
    return true;
}

} // namespace obl::test
