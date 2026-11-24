#include "game/ui/gui_patch.h"

#include "core/ascii_case.h"
#include "core/pack_path.h"
#include "formats/data_patch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace obl::gui_patch {
namespace {

using nlohmann::json;

bool IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool IsIdentChar(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || c == '_' || c == '-' || c == '.' || c == '$';
}

struct BoundsSpec {
    std::string flags; // optional prefix flags from base .gui (T, L, R, B, S, C, Y)
    Rect rect;
    bool overridden = false; // true when move_control, slot, or set_anchor replaced the coords
};

struct SlotDef {
    std::string name;
    Rect bounds;
    SlotDirection direction = SlotDirection::Column;
    int32_t spacing = 0;
    std::vector<size_t> controlIndices; // indices into Container::children
};

struct ControlNode {
    std::string kind; // "Button", "Text", "TGA", "RadioGroup", "TabWindow", "Table", "DefineUse", ...
    std::string explicitId; // from `id=...` or patch `"id"`
    std::string id; // final ID (explicit or derived)
    bool hasRect = false;
    BoundsSpec bounds;
    bool hasSecondRect = false;
    BoundsSpec secondBounds; // ListBox inner rect or Table scroll rect
    std::vector<std::string> tailArgs; // remaining arguments after bounds
    std::string rawText; // for passthrough constructs (DefineUse, etc.)
    std::string childHeaderName; // for `<TABName(...)>` or `<COLn(...)>`
    bool isAngle = false; // `<TAB...>` or `<COL...>`
    bool hasBraceBlock = false;
    std::vector<ControlNode> children; // for RadioGroup, TabWindow, <TAB>, Table
    Anchor anchor = Anchor::None;
    bool hidden = false;
    std::string slotName; // if placed into a slot
    std::string modActionSource; // for validating Mod:<id>.<event> (left/right action on Button)
    bool fromMod = false; // added by a patch; only such controls get the patch-only drawing rules
};

struct ContainerNode {
    enum class Kind { Dialog, MainItem, MainItem640, Define };
    Kind kind = Kind::Dialog;
    std::string name; // dialog name, "MainItem", "MainItem640", or Define name
    std::string explicitId;
    BoundsSpec bounds; // Dialog only
    std::string background; // Dialog only
    int32_t positionKey = 0; // Dialog only
    int32_t draggable = 0; // Dialog only
    Anchor anchor = Anchor::None;
    std::vector<SlotDef> slots;
    std::vector<ControlNode> children;
};

struct TopLevelItem {
    enum class Kind { Font, TickerText, Container };
    Kind kind = Kind::Font;
    std::string rawText; // Font or TickerText
    size_t containerIndex = 0; // index into Document::containers
};

struct Document {
    std::vector<TopLevelItem> order;
    std::vector<ContainerNode> containers;
    bool hasStartDebug = false;
};

// Lightweight recursive-descent parser for `.gui` layout text.
struct Parser {
    std::string_view text;
    size_t pos = 0;
    std::string error;

    bool Done() const { return pos >= text.size(); }
    char Peek() const { return Done() ? '\0' : text[pos]; }

    void SkipTrivia()
    {
        while (!Done()) {
            if (IsBlank(Peek())) {
                ++pos;
            } else if (Peek() == '/' && pos + 1 < text.size() && text[pos + 1] == '/') {
                pos += 2;
                while (!Done() && Peek() != '\n') ++pos;
            } else {
                break;
            }
        }
    }

    bool Consume(char expected)
    {
        SkipTrivia();
        if (Done() || Peek() != expected) return false;
        ++pos;
        return true;
    }

    std::string ReadWord()
    {
        SkipTrivia();
        const size_t start = pos;
        while (!Done()) {
            const char c = Peek();
            if (IsBlank(c) || c == '(' || c == ')' || c == '{' || c == '}' || c == ',' || c == '<' || c == '>' || c == '=') break;
            ++pos;
        }
        return std::string(text.substr(start, pos - start));
    }

    bool ReadArgToken(std::string& out, bool& endedByComma)
    {
        SkipTrivia();
        out.clear();
        endedByComma = false;
        if (Done()) return false;
        const size_t start = pos;
        while (!Done()) {
            const char c = Peek();
            if (c == ',' || c == ')' || c == '\r' || c == '{') break;
            ++pos;
        }
        size_t end = pos;
        while (end > start && IsBlank(text[end - 1])) --end;
        out.assign(text.substr(start, end - start));
        SkipTrivia();
        if (!Done() && Peek() == ',') {
            endedByComma = true;
            ++pos;
            return true;
        }
        if (!Done() && Peek() == ')') {
            ++pos;
            return true;
        }
        return !out.empty();
    }

    bool ReadIntArg(int32_t& out, bool& endedByComma)
    {
        std::string tok;
        if (!ReadArgToken(tok, endedByComma)) return false;
        if (tok.empty()) return false;
        char* endPtr = nullptr;
        const long long v = std::strtoll(tok.c_str(), &endPtr, 10);
        if (!endPtr || *endPtr != '\0') return false;
        out = static_cast<int32_t>(v);
        return true;
    }

    bool ReadBoundsSpec(BoundsSpec& out, bool& endedByComma)
    {
        SkipTrivia();
        out.flags.clear();
        while (!Done() && std::string_view("TBLRSCY").find(Peek()) != std::string_view::npos) {
            out.flags.push_back(Peek());
            ++pos;
        }
        int32_t vals[4] = {};
        for (int i = 0; i < 4; ++i) {
            bool comma = false;
            if (!ReadIntArg(vals[i], comma)) {
                error = "invalid bounds coordinate";
                return false;
            }
            if (i < 3 && !comma) {
                error = "expected ',' in bounds";
                return false;
            }
            if (i == 3) endedByComma = comma;
        }
        out.rect = {vals[0], vals[1], vals[2], vals[3]};
        out.overridden = false;
        return true;
    }

    // Reads remaining comma-separated arguments inside `(...)` up to and including `)`.
    bool ReadRemainingArgs(std::vector<std::string>& args, bool hadComma)
    {
        args.clear();
        if (!hadComma) return true;
        for (;;) {
            SkipTrivia();
            if (!Done() && Peek() == ')') {
                ++pos;
                return true;
            }
            std::string tok;
            bool comma = false;
            if (!ReadArgToken(tok, comma)) {
                error = "unterminated argument list";
                return false;
            }
            args.push_back(std::move(tok));
            if (!comma) return true;
        }
    }

    // Optional `id=<identifier>` immediately following a command's `)`.
    bool ReadOptionalId(std::string& explicitId)
    {
        SkipTrivia();
        if (pos + 3 <= text.size() && text.substr(pos, 3) == "id=") {
            pos += 3;
            const size_t start = pos;
            while (!Done() && IsIdentChar(Peek())) ++pos;
            if (pos == start) {
                error = "empty id= attribute";
                return false;
            }
            explicitId.assign(text.substr(start, pos - start));
        }
        return true;
    }

    bool ParseSlot(SlotDef& slot)
    {
        if (!Consume('(')) {
            error = "Slot: expected '('";
            return false;
        }
        bool comma = false;
        if (!ReadArgToken(slot.name, comma) || !comma || slot.name.empty()) {
            error = "Slot: expected slot name";
            return false;
        }
        BoundsSpec b;
        if (!ReadBoundsSpec(b, comma)) return false;
        slot.bounds = b.rect;
        slot.direction = SlotDirection::Column;
        slot.spacing = 0;
        std::vector<std::string> rest;
        if (!ReadRemainingArgs(rest, comma)) return false;
        if (!rest.empty()) {
            if (ascii::EqualsNoCase(rest[0], "row"))
                slot.direction = SlotDirection::Row;
            else if (ascii::EqualsNoCase(rest[0], "column"))
                slot.direction = SlotDirection::Column;
            else {
                error = "Slot \"" + slot.name + "\": unknown direction \"" + rest[0] + "\" (expected row or column)";
                return false;
            }
        }
        if (rest.size() >= 2) {
            char* endPtr = nullptr;
            const long long v = std::strtoll(rest[1].c_str(), &endPtr, 10);
            if (!endPtr || *endPtr != '\0') {
                error = "Slot \"" + slot.name + "\": invalid spacing";
                return false;
            }
            slot.spacing = static_cast<int32_t>(v);
        }
        return true;
    }

    bool ParseSingleControl(ControlNode& ctrl, std::vector<SlotDef>* ownerSlots = nullptr)
    {
        SkipTrivia();
        if (Done()) {
            error = "unexpected end of input";
            return false;
        }
        if (Peek() == '<') {
            ++pos;
            SkipTrivia();
            const std::string tag = ReadWord();
            ctrl.isAngle = true;
            ctrl.childHeaderName = tag;
            if (tag.rfind("TAB", 0) == 0) {
                ctrl.kind = "TAB";
                if (!Consume('(')) {
                    error = "<TAB>: expected '('";
                    return false;
                }
                bool comma = false;
                ctrl.hasRect = true;
                if (!ReadBoundsSpec(ctrl.bounds, comma)) return false;
                if (!ReadRemainingArgs(ctrl.tailArgs, comma)) return false;
                if (!Consume('>')) {
                    error = "<TAB>: expected '>'";
                    return false;
                }
                if (!ReadOptionalId(ctrl.explicitId)) return false;
                if (!Consume('{')) {
                    error = "<TAB>: expected '{'";
                    return false;
                }
                ctrl.hasBraceBlock = true;
                return ParseBlockChildren(ctrl.children, ownerSlots);
            }
            if (tag.rfind("COL", 0) == 0) {
                ctrl.kind = "COL";
                if (!Consume('(')) {
                    error = "<COL>: expected '('";
                    return false;
                }
                if (!ReadRemainingArgs(ctrl.tailArgs, true)) return false;
                if (!Consume('>')) {
                    error = "<COL>: expected '>'";
                    return false;
                }
                return ReadOptionalId(ctrl.explicitId);
            }
            error = "unknown angle command <" + tag + ">";
            return false;
        }

        const std::string word = ReadWord();
        if (word.empty()) {
            error = "expected control command";
            return false;
        }
        ctrl.kind = word;

        static constexpr std::string_view kOneRectLeaves[] = {"Button", "CheckBox", "Slider", "ComboBox", "Text", "Variable",
                "VariableImage", "Image", "Icon", "TGA", "Bar", "ImageList", "NumberAdjust", "RadioButton"};
        const bool isOneRectLeaf = std::find(std::begin(kOneRectLeaves), std::end(kOneRectLeaves), word) != std::end(kOneRectLeaves);

        if (isOneRectLeaf) {
            if (!Consume('(')) {
                error = word + ": expected '('";
                return false;
            }
            bool comma = false;
            ctrl.hasRect = true;
            if (!ReadBoundsSpec(ctrl.bounds, comma)) return false;
            if (!ReadRemainingArgs(ctrl.tailArgs, comma)) return false;
            return ReadOptionalId(ctrl.explicitId);
        }

        if (word == "ListBox") {
            if (!Consume('(')) {
                error = "ListBox: expected '('";
                return false;
            }
            bool comma = false;
            ctrl.hasRect = true;
            if (!ReadBoundsSpec(ctrl.bounds, comma) || !comma) {
                error = "ListBox: expected inner bounds";
                return false;
            }
            ctrl.hasSecondRect = true;
            if (!ReadBoundsSpec(ctrl.secondBounds, comma)) return false;
            if (!ReadRemainingArgs(ctrl.tailArgs, comma)) return false;
            return ReadOptionalId(ctrl.explicitId);
        }

        if (word == "RadioGroup" || word == "TabWindow") {
            if (!Consume('(')) {
                error = word + ": expected '('";
                return false;
            }
            bool comma = false;
            ctrl.hasRect = true;
            if (!ReadBoundsSpec(ctrl.bounds, comma)) return false;
            if (!ReadRemainingArgs(ctrl.tailArgs, comma)) return false;
            if (!ReadOptionalId(ctrl.explicitId)) return false;
            SkipTrivia();
            if (!Done() && Peek() == '{') {
                ++pos;
                ctrl.hasBraceBlock = true;
                return ParseBlockChildren(ctrl.children, ownerSlots);
            }
            return true;
        }

        if (word == "Table") {
            if (!Consume('(')) {
                error = "Table: expected '('";
                return false;
            }
            bool comma = false;
            ctrl.hasRect = true;
            if (!ReadBoundsSpec(ctrl.bounds, comma) || !comma) {
                error = "Table: expected scroll bounds";
                return false;
            }
            ctrl.hasSecondRect = true;
            if (!ReadBoundsSpec(ctrl.secondBounds, comma)) return false;
            if (!ReadRemainingArgs(ctrl.tailArgs, comma)) return false;
            if (!ReadOptionalId(ctrl.explicitId)) return false;
            SkipTrivia();
            if (!Done() && Peek() == '{') {
                ++pos;
                ctrl.hasBraceBlock = true;
                return ParseBlockChildren(ctrl.children, ownerSlots);
            }
            return true;
        }

        if (word == "TickerText") {
            if (!Consume('(')) {
                error = "TickerText: expected '('";
                return false;
            }
            bool comma = false;
            ctrl.hasRect = true;
            if (!ReadBoundsSpec(ctrl.bounds, comma)) return false;
            if (!ReadRemainingArgs(ctrl.tailArgs, comma)) return false;
            return ReadOptionalId(ctrl.explicitId);
        }

        // Define template reference inside a container.
        ctrl.kind = "DefineUse";
        ctrl.rawText = word;
        return true;
    }

    bool ParseBlockChildren(std::vector<ControlNode>& children, std::vector<SlotDef>* ownerSlots)
    {
        for (;;) {
            SkipTrivia();
            if (Done()) {
                error = "unterminated '{' block";
                return false;
            }
            if (Peek() == '}') {
                ++pos;
                return true;
            }
            // Peek ahead for Slot(...)
            const size_t saved = pos;
            const std::string maybeSlot = ReadWord();
            if (maybeSlot == "Slot") {
                if (!ownerSlots) {
                    error = "Slot is only allowed inside a DialogItem or MainItem";
                    return false;
                }
                SlotDef slot;
                if (!ParseSlot(slot)) return false;
                for (const auto& existing : *ownerSlots) {
                    if (existing.name == slot.name) {
                        error = "duplicate slot \"" + slot.name + "\"";
                        return false;
                    }
                }
                ownerSlots->push_back(std::move(slot));
                continue;
            }
            pos = saved;
            ControlNode ctrl;
            if (!ParseSingleControl(ctrl, ownerSlots)) return false;
            children.push_back(std::move(ctrl));
        }
    }

    bool ParseDialogItem(ContainerNode& container)
    {
        container.kind = ContainerNode::Kind::Dialog;
        container.name = ReadWord();
        if (container.name.empty()) {
            error = "DialogItem: missing name";
            return false;
        }
        if (!Consume('(')) {
            error = "DialogItem \"" + container.name + "\": expected '('";
            return false;
        }
        bool comma = false;
        if (!ReadBoundsSpec(container.bounds, comma) || !comma) {
            error = "DialogItem \"" + container.name + "\": expected background";
            return false;
        }
        if (!ReadArgToken(container.background, comma) || !comma) {
            error = "DialogItem \"" + container.name + "\": expected position key";
            return false;
        }
        if (!ReadIntArg(container.positionKey, comma) || !comma) {
            error = "DialogItem \"" + container.name + "\": expected draggable flag";
            return false;
        }
        if (!ReadIntArg(container.draggable, comma)) {
            error = "DialogItem \"" + container.name + "\": invalid draggable flag";
            return false;
        }
        if (!ReadOptionalId(container.explicitId)) return false;
        if (!Consume('{')) {
            error = "DialogItem \"" + container.name + "\": expected '{'";
            return false;
        }
        return ParseBlockChildren(container.children, &container.slots);
    }

    bool ParseDocument(Document& doc)
    {
        for (;;) {
            SkipTrivia();
            if (Done()) return true;
            const size_t stmtStart = pos;
            const std::string word = ReadWord();
            if (word.empty()) {
                error = "unexpected character in GUI layout";
                return false;
            }
            if (word == "StartDebug") {
                doc.hasStartDebug = true;
                return true;
            }
            if (word == "Font") {
                if (!Consume('(')) {
                    error = "Font: expected '('";
                    return false;
                }
                std::string name;
                bool comma = false;
                if (!ReadArgToken(name, comma)) {
                    error = "Font: expected font name";
                    return false;
                }
                TopLevelItem item;
                item.kind = TopLevelItem::Kind::Font;
                item.rawText = "Font(" + name + ")";
                doc.order.push_back(std::move(item));
                continue;
            }
            if (word == "TickerText") {
                if (!Consume('(')) {
                    error = "TickerText: expected '('";
                    return false;
                }
                while (!Done() && Peek() != ')') ++pos;
                if (Done()) {
                    error = "TickerText: expected ')'";
                    return false;
                }
                ++pos;
                TopLevelItem item;
                item.kind = TopLevelItem::Kind::TickerText;
                item.rawText = std::string(text.substr(stmtStart, pos - stmtStart));
                doc.order.push_back(std::move(item));
                continue;
            }
            if (word == "DialogItem") {
                ContainerNode c;
                if (!ParseDialogItem(c)) return false;
                TopLevelItem item;
                item.kind = TopLevelItem::Kind::Container;
                item.containerIndex = doc.containers.size();
                doc.containers.push_back(std::move(c));
                doc.order.push_back(std::move(item));
                continue;
            }
            if (word == "MainItem" || word == "MainItem640") {
                ContainerNode c;
                c.kind = (word == "MainItem") ? ContainerNode::Kind::MainItem : ContainerNode::Kind::MainItem640;
                c.name = word;
                if (!Consume('{')) {
                    error = word + ": expected '{'";
                    return false;
                }
                if (!ParseBlockChildren(c.children, &c.slots)) return false;
                TopLevelItem item;
                item.kind = TopLevelItem::Kind::Container;
                item.containerIndex = doc.containers.size();
                doc.containers.push_back(std::move(c));
                doc.order.push_back(std::move(item));
                continue;
            }
            if (word == "Define") {
                ContainerNode c;
                c.kind = ContainerNode::Kind::Define;
                c.name = ReadWord();
                if (c.name.empty() || !Consume('{')) {
                    error = "Define: expected name and '{'";
                    return false;
                }
                if (!ParseBlockChildren(c.children, nullptr)) return false;
                TopLevelItem item;
                item.kind = TopLevelItem::Kind::Container;
                item.containerIndex = doc.containers.size();
                doc.containers.push_back(std::move(c));
                doc.order.push_back(std::move(item));
                continue;
            }
            error = "unexpected top-level command \"" + word + "\"";
            return false;
        }
    }
};

bool IsNoneToken(std::string_view token)
{
    return token.empty() || ascii::EqualsNoCase(token, "None");
}

std::string DeriveControlKey(const ControlNode& ctrl)
{
    if (ctrl.kind == "Button") {
        // Button(rect, graphic, text, left, right [, tooltip])
        if (ctrl.tailArgs.size() >= 3 && !IsNoneToken(ctrl.tailArgs[2])) return ctrl.tailArgs[2];
        if (ctrl.tailArgs.size() >= 4 && !IsNoneToken(ctrl.tailArgs[3])) return ctrl.tailArgs[3];
        if (ctrl.tailArgs.size() >= 2 && !IsNoneToken(ctrl.tailArgs[1])) return ctrl.tailArgs[1];
        if (ctrl.tailArgs.size() >= 1 && !IsNoneToken(ctrl.tailArgs[0])) return ctrl.tailArgs[0];
        return "unnamed";
    }
    if (ctrl.kind == "CheckBox") {
        // CheckBox(rect, graphic, variable [, tooltip])
        return ctrl.tailArgs.size() >= 2 ? ctrl.tailArgs[1] : "unnamed";
    }
    if (ctrl.kind == "Slider" || ctrl.kind == "ComboBox" || ctrl.kind == "Text" || ctrl.kind == "Variable" ||
            ctrl.kind == "VariableImage" || ctrl.kind == "Image" || ctrl.kind == "TGA" || ctrl.kind == "ListBox" ||
            ctrl.kind == "RadioGroup" || ctrl.kind == "Table") {
        return !ctrl.tailArgs.empty() ? ctrl.tailArgs[0] : "unnamed";
    }
    if (ctrl.kind == "Icon") {
        // Icon(rect, name, frame)
        if (ctrl.tailArgs.size() >= 2) return ctrl.tailArgs[0] + "." + ctrl.tailArgs[1];
        return !ctrl.tailArgs.empty() ? ctrl.tailArgs[0] : "unnamed";
    }
    if (ctrl.kind == "Bar") {
        // Bar(rect, min, max, key, flags)
        return ctrl.tailArgs.size() >= 3 ? ctrl.tailArgs[2] : "unnamed";
    }
    if (ctrl.kind == "ImageList") {
        // ImageList(rect, sx, sy, list, highlight)
        return ctrl.tailArgs.size() >= 3 ? ctrl.tailArgs[2] : "unnamed";
    }
    if (ctrl.kind == "NumberAdjust") {
        // NumberAdjust(rect, mode, font, key, flag [, tooltip])
        return ctrl.tailArgs.size() >= 3 ? ctrl.tailArgs[2] : "unnamed";
    }
    if (ctrl.kind == "RadioButton") {
        // RadioButton(rect, graphic, font, value, text [, tooltip])
        return ctrl.tailArgs.size() >= 3 ? ctrl.tailArgs[2] : "unnamed";
    }
    if (ctrl.kind == "TAB") return ctrl.childHeaderName;
    if (ctrl.kind == "COL") {
        // <COLn (font, color, name, ...)>
        return ctrl.tailArgs.size() >= 3 ? ctrl.tailArgs[2] : ctrl.childHeaderName;
    }
    if (ctrl.kind == "DefineUse") return ctrl.rawText;
    return ctrl.kind;
}

bool AssignIdsRecursive(const std::string& scope, std::vector<ControlNode>& nodes, std::unordered_set<std::string>& seen,
        std::vector<std::string>& outIds, std::string& error)
{
    for (auto& ctrl : nodes) {
        if (ctrl.kind == "DefineUse") continue;
        if (!ctrl.explicitId.empty()) {
            ctrl.id = ctrl.explicitId;
        } else {
            const std::string key = DeriveControlKey(ctrl);
            if (ctrl.kind == "TAB")
                ctrl.id = scope + "." + key;
            else
                ctrl.id = scope + "." + ctrl.kind + "." + key;
        }
        if (!seen.insert(ctrl.id).second) {
            error = "duplicate control id \"" + ctrl.id + "\"";
            return false;
        }
        outIds.push_back(ctrl.id);
        if (!ctrl.children.empty()) {
            if (!AssignIdsRecursive(ctrl.id, ctrl.children, seen, outIds, error)) return false;
        }
    }
    return true;
}

bool AssignDocumentIds(Document& doc, std::unordered_set<std::string>& seen, std::vector<std::string>& outIds, std::string& error)
{
    seen.clear();
    outIds.clear();
    for (auto& c : doc.containers) {
        const std::string scope = (c.kind == ContainerNode::Kind::Define) ? ("Define." + c.name) : c.name;
        if (!AssignIdsRecursive(scope, c.children, seen, outIds, error)) return false;
    }
    return true;
}

ControlNode* FindControlByIdRecursive(std::vector<ControlNode>& nodes, std::string_view id)
{
    for (auto& ctrl : nodes) {
        if (ctrl.id == id) return &ctrl;
        if (auto* nested = FindControlByIdRecursive(ctrl.children, id)) return nested;
    }
    return nullptr;
}

ControlNode* FindControlById(Document& doc, std::string_view id, ContainerNode** outOwner = nullptr)
{
    for (auto& c : doc.containers) {
        if (auto* found = FindControlByIdRecursive(c.children, id)) {
            if (outOwner) *outOwner = &c;
            return found;
        }
    }
    return nullptr;
}

ContainerNode* FindContainerByName(Document& doc, std::string_view name)
{
    for (auto& c : doc.containers) {
        if (c.kind != ContainerNode::Kind::Define && c.name == name) return &c;
    }
    return nullptr;
}

SlotDef* FindSlot(ContainerNode& c, std::string_view slotName)
{
    for (auto& s : c.slots) {
        if (s.name == slotName) return &s;
    }
    return nullptr;
}

bool ValidateButtonActionsRecursive(const ControlNode& ctrl, std::span<const std::string> activeMods, std::string& error)
{
    if (ctrl.kind == "Button") {
        for (size_t idx : {size_t(2), size_t(3)}) {
            if (idx < ctrl.tailArgs.size() && IsModAction(ctrl.tailArgs[idx])) {
                std::string modId, event;
                if (!ValidateModAction(ctrl.tailArgs[idx], activeMods, modId, event, error)) return false;
            }
        }
    }
    for (const auto& child : ctrl.children) {
        if (!ValidateButtonActionsRecursive(child, activeMods, error)) return false;
    }
    return true;
}

bool ReadIntField(const json& obj, const char* key, int32_t& out)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) return false;
    out = it->get<int32_t>();
    return true;
}

bool ReadBoundsArray(const json& obj, const char* key, Rect& out)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array() || it->size() != 4) return false;
    for (size_t i = 0; i < 4; ++i)
        if (!(*it)[i].is_number_integer()) return false;
    out = {(*it)[0].get<int32_t>(), (*it)[1].get<int32_t>(), (*it)[2].get<int32_t>(), (*it)[3].get<int32_t>()};
    return true;
}

bool ApplySinglePatch(const PatchSource& patch, Document& doc, std::unordered_set<std::string>& seenIds,
        std::span<const std::string> activeMods, std::string& error)
{
    const std::string prefix = "mod \"" + patch.modId + "\" (" + patch.file + "): ";
    const json root = json::parse(patch.jsonText, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = prefix + "invalid JSON object";
        return false;
    }
    const auto formatIt = root.find("format");
    if (formatIt == root.end() || !formatIt->is_string() || formatIt->get<std::string>() != kFormat) {
        error = prefix + "unknown format (expected " + kFormat + ")";
        return false;
    }
    const auto versionIt = root.find("version");
    if (versionIt == root.end() || !versionIt->is_number_integer() || versionIt->get<int>() != kVersion) {
        error = prefix + "unsupported version (expected " + std::to_string(kVersion) + ")";
        return false;
    }
    const auto opsIt = root.find("ops");
    if (opsIt == root.end() || !opsIt->is_array()) {
        error = prefix + "\"ops\" is missing or not an array";
        return false;
    }

    for (size_t opIdx = 0; opIdx < opsIt->size(); ++opIdx) {
        const json& opObj = (*opsIt)[opIdx];
        const std::string opPrefix = prefix + "op #" + std::to_string(opIdx);
        if (!opObj.is_object()) {
            error = opPrefix + ": operation is not an object";
            return false;
        }
        const auto opNameIt = opObj.find("op");
        if (opNameIt == opObj.end() || !opNameIt->is_string()) {
            error = opPrefix + ": missing \"op\" string";
            return false;
        }
        const std::string op = opNameIt->get<std::string>();
        const std::string where = opPrefix + " (" + op + "): ";

        if (op == "add_dialog") {
            const auto dialogIt = opObj.find("dialog");
            if (dialogIt == opObj.end() || !dialogIt->is_string()) {
                error = where + "missing \"dialog\" string";
                return false;
            }
            const std::string dialogText = dialogIt->get<std::string>();
            Parser parser{dialogText, 0, {}};
            const std::string kw = parser.ReadWord();
            if (kw != "DialogItem") {
                error = where + "dialog text must start with DialogItem";
                return false;
            }
            ContainerNode c;
            if (!parser.ParseDialogItem(c)) {
                error = where + parser.error;
                return false;
            }
            if (FindContainerByName(doc, c.name)) {
                error = where + "dialog \"" + c.name + "\" already exists";
                return false;
            }
            if (const auto anchorIt = opObj.find("anchor"); anchorIt != opObj.end()) {
                if (!anchorIt->is_string() || !ParseAnchor(anchorIt->get<std::string>(), c.anchor)) {
                    error = where + "invalid anchor";
                    return false;
                }
            }
            std::vector<std::string> newIds;
            std::string idErr;
            if (!AssignIdsRecursive(c.name, c.children, seenIds, newIds, idErr)) {
                error = where + idErr;
                return false;
            }
            for (auto& child : c.children) child.fromMod = true;
            for (const auto& child : c.children) {
                std::string actErr;
                if (!ValidateButtonActionsRecursive(child, activeMods, actErr)) {
                    error = where + actErr;
                    return false;
                }
            }
            TopLevelItem item;
            item.kind = TopLevelItem::Kind::Container;
            item.containerIndex = doc.containers.size();
            doc.containers.push_back(std::move(c));
            doc.order.push_back(std::move(item));
            continue;
        }

        if (op == "add_slot") {
            const auto dlgIt = opObj.find("dialog");
            const auto slotIt = opObj.find("slot");
            if (dlgIt == opObj.end() || !dlgIt->is_string() || slotIt == opObj.end() || !slotIt->is_string() ||
                    slotIt->get<std::string>().empty()) {
                error = where + "missing \"dialog\" or \"slot\" string";
                return false;
            }
            ContainerNode* container = FindContainerByName(doc, dlgIt->get<std::string>());
            if (!container) {
                error = where + "unknown dialog \"" + dlgIt->get<std::string>() + "\"";
                return false;
            }
            const std::string slotName = slotIt->get<std::string>();
            if (FindSlot(*container, slotName)) {
                error = where + "slot \"" + slotName + "\" already exists in \"" + container->name + "\"";
                return false;
            }
            SlotDef slot;
            slot.name = slotName;
            if (!ReadBoundsArray(opObj, "bounds", slot.bounds)) {
                error = where + "missing or invalid \"bounds\" [x0, y0, x1, y1]";
                return false;
            }
            if (const auto dirIt = opObj.find("direction"); dirIt != opObj.end()) {
                if (!dirIt->is_string()) {
                    error = where + "\"direction\" must be \"row\" or \"column\"";
                    return false;
                }
                const std::string d = dirIt->get<std::string>();
                if (d == "row")
                    slot.direction = SlotDirection::Row;
                else if (d == "column")
                    slot.direction = SlotDirection::Column;
                else {
                    error = where + "unknown direction \"" + d + "\" (expected row or column)";
                    return false;
                }
            }
            if (opObj.contains("spacing")) {
                if (!ReadIntField(opObj, "spacing", slot.spacing)) {
                    error = where + "\"spacing\" must be an integer";
                    return false;
                }
            }
            container->slots.push_back(std::move(slot));
            continue;
        }

        if (op == "add_control") {
            const auto dlgIt = opObj.find("dialog");
            const auto ctrlIt = opObj.find("control");
            if (dlgIt == opObj.end() || !dlgIt->is_string() || ctrlIt == opObj.end() || !ctrlIt->is_string()) {
                error = where + "missing \"dialog\" or \"control\" string";
                return false;
            }
            ContainerNode* container = FindContainerByName(doc, dlgIt->get<std::string>());
            if (!container) {
                error = where + "unknown dialog \"" + dlgIt->get<std::string>() + "\"";
                return false;
            }
            const std::string ctrlText = ctrlIt->get<std::string>();
            Parser parser{ctrlText, 0, {}};
            ControlNode ctrl;
            if (!parser.ParseSingleControl(ctrl, &container->slots)) {
                error = where + parser.error;
                return false;
            }
            if (const auto idIt = opObj.find("id"); idIt != opObj.end()) {
                if (!idIt->is_string() || idIt->get<std::string>().empty()) {
                    error = where + "\"id\" must be a non-empty string";
                    return false;
                }
                ctrl.explicitId = idIt->get<std::string>();
            }
            if (const auto anchorIt = opObj.find("anchor"); anchorIt != opObj.end()) {
                if (!anchorIt->is_string() || !ParseAnchor(anchorIt->get<std::string>(), ctrl.anchor)) {
                    error = where + "invalid anchor";
                    return false;
                }
            }
            std::string actErr;
            if (!ValidateButtonActionsRecursive(ctrl, activeMods, actErr)) {
                error = where + actErr;
                return false;
            }
            std::vector<ControlNode> single;
            single.push_back(std::move(ctrl));
            std::vector<std::string> newIds;
            std::string idErr;
            if (!AssignIdsRecursive(container->name, single, seenIds, newIds, idErr)) {
                error = where + idErr;
                return false;
            }
            ctrl = std::move(single.front());
            ctrl.fromMod = true;

            const bool hasSlot = opObj.contains("slot");
            const bool hasAt = opObj.contains("at");
            if (hasSlot && hasAt) {
                error = where + "cannot specify both \"slot\" and \"at\"";
                return false;
            }
            if (hasSlot) {
                if (!opObj["slot"].is_string()) {
                    error = where + "\"slot\" must be a string";
                    return false;
                }
                const std::string slotName = opObj["slot"].get<std::string>();
                SlotDef* slot = FindSlot(*container, slotName);
                if (!slot) {
                    error = where + "unknown slot \"" + slotName + "\" in dialog \"" + container->name + "\"";
                    return false;
                }
                ctrl.slotName = slotName;
                const size_t newIndex = container->children.size();
                container->children.push_back(std::move(ctrl));
                slot->controlIndices.push_back(newIndex);
            } else {
                bool atBack = false;
                if (hasAt) {
                    if (!opObj["at"].is_string()) {
                        error = where + "\"at\" must be \"front\" or \"back\"";
                        return false;
                    }
                    const std::string at = opObj["at"].get<std::string>();
                    if (at == "back")
                        atBack = true;
                    else if (at != "front") {
                        error = where + "unknown \"at\" value \"" + at + "\" (expected front or back)";
                        return false;
                    }
                }
                if (atBack) {
                    container->children.insert(container->children.begin(), std::move(ctrl));
                    // Shift recorded slot indices since we inserted at index 0.
                    for (auto& s : container->slots)
                        for (size_t& idx : s.controlIndices) ++idx;
                } else {
                    container->children.push_back(std::move(ctrl));
                }
            }
            continue;
        }

        if (op == "hide_control") {
            const auto idIt = opObj.find("id");
            if (idIt == opObj.end() || !idIt->is_string()) {
                error = where + "missing \"id\" string";
                return false;
            }
            ControlNode* ctrl = FindControlById(doc, idIt->get<std::string>());
            if (!ctrl) {
                error = where + "unknown control id \"" + idIt->get<std::string>() + "\"";
                return false;
            }
            ctrl->hidden = true;
            continue;
        }

        if (op == "move_control") {
            const auto idIt = opObj.find("id");
            if (idIt == opObj.end() || !idIt->is_string()) {
                error = where + "missing \"id\" string";
                return false;
            }
            ControlNode* ctrl = FindControlById(doc, idIt->get<std::string>());
            if (!ctrl) {
                error = where + "unknown control id \"" + idIt->get<std::string>() + "\"";
                return false;
            }
            if (!ctrl->hasRect) {
                error = where + "control \"" + ctrl->id + "\" has no bounds";
                return false;
            }
            Rect newRect = ctrl->bounds.rect;
            if (opObj.contains("bounds")) {
                if (!ReadBoundsArray(opObj, "bounds", newRect)) {
                    error = where + "invalid \"bounds\" array";
                    return false;
                }
            } else if (opObj.contains("x") || opObj.contains("y")) {
                const int32_t w = newRect.x1 - newRect.x0;
                const int32_t h = newRect.y1 - newRect.y0;
                int32_t x = newRect.x0;
                int32_t y = newRect.y0;
                if (opObj.contains("x") && !ReadIntField(opObj, "x", x)) {
                    error = where + "\"x\" must be an integer";
                    return false;
                }
                if (opObj.contains("y") && !ReadIntField(opObj, "y", y)) {
                    error = where + "\"y\" must be an integer";
                    return false;
                }
                newRect = {x, y, x + w, y + h};
            } else if (opObj.contains("dx") || opObj.contains("dy")) {
                int32_t dx = 0;
                int32_t dy = 0;
                if (opObj.contains("dx") && !ReadIntField(opObj, "dx", dx)) {
                    error = where + "\"dx\" must be an integer";
                    return false;
                }
                if (opObj.contains("dy") && !ReadIntField(opObj, "dy", dy)) {
                    error = where + "\"dy\" must be an integer";
                    return false;
                }
                newRect = {newRect.x0 + dx, newRect.y0 + dy, newRect.x1 + dx, newRect.y1 + dy};
            } else {
                error = where + "expected \"bounds\", \"x\"/\"y\", or \"dx\"/\"dy\"";
                return false;
            }
            ctrl->bounds.rect = newRect;
            ctrl->bounds.flags.clear();
            ctrl->bounds.overridden = true;
            continue;
        }

        if (op == "set_anchor") {
            const auto anchorIt = opObj.find("anchor");
            if (anchorIt == opObj.end() || !anchorIt->is_string()) {
                error = where + "missing \"anchor\" string";
                return false;
            }
            Anchor anchor = Anchor::None;
            if (!ParseAnchor(anchorIt->get<std::string>(), anchor)) {
                error = where + "unknown anchor \"" + anchorIt->get<std::string>() + "\"";
                return false;
            }
            const bool hasDialog = opObj.contains("dialog");
            const bool hasId = opObj.contains("id");
            if (hasDialog == hasId) {
                error = where + "must specify exactly one of \"dialog\" or \"id\"";
                return false;
            }
            if (hasDialog) {
                if (!opObj["dialog"].is_string()) {
                    error = where + "\"dialog\" must be a string";
                    return false;
                }
                ContainerNode* container = FindContainerByName(doc, opObj["dialog"].get<std::string>());
                if (!container) {
                    error = where + "unknown dialog \"" + opObj["dialog"].get<std::string>() + "\"";
                    return false;
                }
                container->anchor = anchor;
            } else {
                if (!opObj["id"].is_string()) {
                    error = where + "\"id\" must be a string";
                    return false;
                }
                ControlNode* ctrl = FindControlById(doc, opObj["id"].get<std::string>());
                if (!ctrl) {
                    error = where + "unknown control id \"" + opObj["id"].get<std::string>() + "\"";
                    return false;
                }
                ctrl->anchor = anchor;
            }
            continue;
        }

        error = where + "unknown operation \"" + op + "\"";
        return false;
    }

    return true;
}

void ResolveSlotsAndAnchors(Document& doc, int32_t screenWidth, int32_t screenHeight)
{
    for (auto& c : doc.containers) {
        if (c.kind == ContainerNode::Kind::Define) continue;

        // 1. Lay out slots in load order.
        for (const auto& slot : c.slots) {
            int32_t cursorX = slot.bounds.x0;
            int32_t cursorY = slot.bounds.y0;
            const int32_t slotW = slot.bounds.x1 - slot.bounds.x0;
            const int32_t slotH = slot.bounds.y1 - slot.bounds.y0;
            for (size_t idx : slot.controlIndices) {
                ControlNode& ctrl = c.children[idx];
                if (!ctrl.hasRect || ctrl.hidden) continue;
                int32_t w = ctrl.bounds.rect.x1 - ctrl.bounds.rect.x0;
                int32_t h = ctrl.bounds.rect.y1 - ctrl.bounds.rect.y0;
                if (w <= 0) w = slotW;
                if (h <= 0) h = slotH;
                ctrl.bounds.rect = {cursorX, cursorY, cursorX + w, cursorY + h};
                ctrl.bounds.flags.clear();
                ctrl.bounds.overridden = true;
                if (slot.direction == SlotDirection::Column)
                    cursorY += h + slot.spacing;
                else
                    cursorX += w + slot.spacing;
            }
        }

        // 2. Resolve container anchor against screen.
        int32_t refW = kReferenceWidth;
        int32_t refH = kReferenceHeight;
        int32_t targetW = screenWidth;
        int32_t targetH = screenHeight;
        if (c.kind == ContainerNode::Kind::Dialog) {
            const int32_t baseW = c.bounds.rect.x1 - c.bounds.rect.x0;
            const int32_t baseH = c.bounds.rect.y1 - c.bounds.rect.y0;
            if (c.anchor != Anchor::None) {
                c.bounds.rect = ApplyAnchor(c.bounds.rect, c.anchor, kReferenceWidth, kReferenceHeight, screenWidth, screenHeight);
                c.bounds.flags.clear();
                c.bounds.overridden = true;
            }
            refW = baseW > 0 ? baseW : kReferenceWidth;
            refH = baseH > 0 ? baseH : kReferenceHeight;
            targetW = c.bounds.rect.x1 - c.bounds.rect.x0;
            targetH = c.bounds.rect.y1 - c.bounds.rect.y0;
        }

        // 3. Resolve child anchors against container bounds.
        for (auto& ctrl : c.children) {
            if (!ctrl.hasRect || ctrl.anchor == Anchor::None) continue;
            ctrl.bounds.rect = ApplyAnchor(ctrl.bounds.rect, ctrl.anchor, refW, refH, targetW, targetH);
            ctrl.bounds.flags.clear();
            ctrl.bounds.overridden = true;
        }
    }
}

void EmitBounds(std::ostringstream& out, const BoundsSpec& b)
{
    if (!b.overridden && !b.flags.empty()) out << b.flags;
    out << b.rect.x0 << ", " << b.rect.y0 << ", " << b.rect.x1 << ", " << b.rect.y1;
}

void EmitControl(std::ostringstream& out, const ControlNode& ctrl, int indent)
{
    if (ctrl.hidden) return;
    for (int i = 0; i < indent; ++i) out << '\t';
    if (ctrl.kind == "DefineUse") {
        out << ctrl.rawText << '\n';
        return;
    }
    if (ctrl.isAngle) {
        out << '<' << ctrl.childHeaderName << '(';
        if (ctrl.hasRect) {
            EmitBounds(out, ctrl.bounds);
            for (const auto& arg : ctrl.tailArgs) out << ", " << arg;
        } else {
            for (size_t i = 0; i < ctrl.tailArgs.size(); ++i) {
                if (i != 0) out << ", ";
                out << ctrl.tailArgs[i];
            }
        }
        out << ")>";
        if (ctrl.hasBraceBlock) {
            out << '\n';
            for (int i = 0; i < indent; ++i) out << '\t';
            out << "{\n";
            for (const auto& child : ctrl.children) EmitControl(out, child, indent + 1);
            for (int i = 0; i < indent; ++i) out << '\t';
            out << "}\n";
        } else {
            out << '\n';
        }
        return;
    }

    out << ctrl.kind << '(';
    if (ctrl.hasRect) {
        EmitBounds(out, ctrl.bounds);
        if (ctrl.hasSecondRect) {
            out << ", ";
            EmitBounds(out, ctrl.secondBounds);
        }
        for (const auto& arg : ctrl.tailArgs) out << ", " << arg;
    }
    out << ')';
    if (ctrl.hasBraceBlock) {
        out << '\n';
        for (int i = 0; i < indent; ++i) out << '\t';
        out << "{\n";
        for (const auto& child : ctrl.children) EmitControl(out, child, indent + 1);
        for (int i = 0; i < indent; ++i) out << '\t';
        out << "}\n";
    } else {
        out << '\n';
    }
}

void EmitDocument(const Document& doc, ApplyResult& out)
{
    std::ostringstream ss;
    out.bindings.clear();
    for (const auto& item : doc.order) {
        if (item.kind == TopLevelItem::Kind::Font || item.kind == TopLevelItem::Kind::TickerText) {
            ss << item.rawText << '\n';
            continue;
        }
        const ContainerNode& c = doc.containers[item.containerIndex];
        if (c.kind == ContainerNode::Kind::Dialog) {
            ss << "DialogItem " << c.name << '(';
            EmitBounds(ss, c.bounds);
            ss << ", " << c.background << ", " << c.positionKey << ", " << c.draggable << ")\n{\n";
        } else if (c.kind == ContainerNode::Kind::MainItem) {
            ss << "MainItem {\n";
        } else if (c.kind == ContainerNode::Kind::MainItem640) {
            ss << "MainItem640 {\n";
        } else {
            ss << "Define " << c.name << " {\n";
        }
        size_t emittedChildIndex = 0;
        for (const auto& ctrl : c.children) {
            if (ctrl.hidden) continue;
            EmitControl(ss, ctrl, 1);
            if (c.kind != ContainerNode::Kind::Define && !ctrl.id.empty()) {
                out.bindings.push_back({ctrl.id, c.name, emittedChildIndex, ctrl.fromMod});
            }
            ++emittedChildIndex;
        }
        ss << "}\n";
    }
    if (doc.hasStartDebug) ss << "StartDebug\n";
    out.scriptText = ss.str();
}

bool IsFile(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string ModNameFromPatchPath(const std::filesystem::path& file)
{
    return file.parent_path().parent_path().filename().string();
}

} // namespace

bool ParseAnchor(std::string_view name, Anchor& out)
{
    if (name == "left") {
        out = Anchor::Left;
        return true;
    }
    if (name == "right") {
        out = Anchor::Right;
        return true;
    }
    if (name == "top") {
        out = Anchor::Top;
        return true;
    }
    if (name == "bottom") {
        out = Anchor::Bottom;
        return true;
    }
    if (name == "center" || name == "centre") {
        out = Anchor::Center;
        return true;
    }
    if (name == "center_x" || name == "centre_x") {
        out = Anchor::CenterX;
        return true;
    }
    if (name == "center_y" || name == "centre_y") {
        out = Anchor::CenterY;
        return true;
    }
    if (name == "top_left") {
        out = Anchor::TopLeft;
        return true;
    }
    if (name == "top_right") {
        out = Anchor::TopRight;
        return true;
    }
    if (name == "bottom_left") {
        out = Anchor::BottomLeft;
        return true;
    }
    if (name == "bottom_right") {
        out = Anchor::BottomRight;
        return true;
    }
    if (name == "stretch") {
        out = Anchor::Stretch;
        return true;
    }
    if (name == "stretch_x") {
        out = Anchor::StretchX;
        return true;
    }
    if (name == "stretch_y") {
        out = Anchor::StretchY;
        return true;
    }
    return false;
}

Rect ApplyAnchor(Rect rect, Anchor anchor, int32_t refW, int32_t refH, int32_t targetW, int32_t targetH)
{
    const int32_t w = rect.x1 - rect.x0;
    const int32_t h = rect.y1 - rect.y0;
    Rect r = rect;

    switch (anchor) {
    case Anchor::Right:
    case Anchor::TopRight:
    case Anchor::BottomRight: {
        const int32_t rightMargin = refW - rect.x1;
        r.x1 = targetW - rightMargin;
        r.x0 = r.x1 - w;
        break;
    }
    case Anchor::Center:
    case Anchor::CenterX:
        r.x0 = (targetW - w) / 2;
        r.x1 = r.x0 + w;
        break;
    case Anchor::Stretch:
    case Anchor::StretchX:
        if (rect.x0 == 0 && rect.x1 >= refW) {
            r.x0 = 0;
            r.x1 = targetW;
        } else {
            const int32_t rightMargin = std::max<int32_t>(0, refW - rect.x1);
            r.x0 = rect.x0;
            r.x1 = std::max<int32_t>(r.x0, targetW - rightMargin);
        }
        break;
    default: break;
    }

    switch (anchor) {
    case Anchor::Bottom:
    case Anchor::BottomLeft:
    case Anchor::BottomRight: {
        const int32_t bottomMargin = refH - rect.y1;
        r.y1 = targetH - bottomMargin;
        r.y0 = r.y1 - h;
        break;
    }
    case Anchor::Center:
    case Anchor::CenterY:
        r.y0 = (targetH - h) / 2;
        r.y1 = r.y0 + h;
        break;
    case Anchor::Stretch:
    case Anchor::StretchY:
        if (rect.y0 == 0 && rect.y1 >= refH) {
            r.y0 = 0;
            r.y1 = targetH;
        } else {
            const int32_t bottomMargin = std::max<int32_t>(0, refH - rect.y1);
            r.y0 = rect.y0;
            r.y1 = std::max<int32_t>(r.y0, targetH - bottomMargin);
        }
        break;
    default: break;
    }

    return r;
}

bool IsModAction(std::string_view action)
{
    return action.size() >= 4 && action.substr(0, 4) == "Mod:";
}

bool ValidateModAction(
        std::string_view action, std::span<const std::string> activeMods, std::string& modId, std::string& event, std::string& error)
{
    if (!IsModAction(action)) {
        error = "action \"" + std::string(action) + "\" is not a Mod:<id>.<event> action";
        return false;
    }
    const std::string_view rest = action.substr(4);
    const size_t dot = rest.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= rest.size()) {
        error = "invalid mod action \"" + std::string(action) + "\" (expected Mod:<id>.<event>)";
        return false;
    }
    const std::string_view id = rest.substr(0, dot);
    const std::string_view ev = rest.substr(dot + 1);
    if (!pack_path::PackDirectory(id)) {
        error = "mod action \"" + std::string(action) + "\": \"" + std::string(id) + "\" is not a valid mod id";
        return false;
    }
    for (char c : ev) {
        if (!IsIdentChar(c)) {
            error = "mod action \"" + std::string(action) + "\": invalid event name \"" + std::string(ev) + "\"";
            return false;
        }
    }
    const bool active = std::any_of(activeMods.begin(), activeMods.end(), [&](const std::string& m) { return m == id; });
    if (!active) {
        error = "action \"" + std::string(action) + "\" references mod \"" + std::string(id) + "\", which is not active";
        return false;
    }
    modId.assign(id);
    event.assign(ev);
    return true;
}

bool DeriveControlIds(std::string_view scriptText, std::vector<std::string>& ids, std::string& error)
{
    error.clear();
    ids.clear();
    Parser parser{scriptText, 0, {}};
    Document doc;
    if (!parser.ParseDocument(doc)) {
        error = parser.error;
        return false;
    }
    std::unordered_set<std::string> seen;
    return AssignDocumentIds(doc, seen, ids, error);
}

bool ApplyPatches(std::string_view baseScriptText, std::span<const PatchSource> patches, std::span<const std::string> activeMods,
        int32_t screenWidth, int32_t screenHeight, ApplyResult& out, std::string& error)
{
    error.clear();
    out = {};
    Parser parser{baseScriptText, 0, {}};
    Document doc;
    if (!parser.ParseDocument(doc)) {
        error = "base GUI layout: " + parser.error;
        return false;
    }
    std::unordered_set<std::string> seenIds;
    std::vector<std::string> derived;
    if (!AssignDocumentIds(doc, seenIds, derived, error)) {
        error = "base GUI layout: " + error;
        return false;
    }
    for (const auto& patch : patches) {
        if (!ApplySinglePatch(patch, doc, seenIds, activeMods, error)) return false;
    }
    ResolveSlotsAndAnchors(doc, screenWidth, screenHeight);
    EmitDocument(doc, out);
    return true;
}

std::vector<std::filesystem::path> FindPatchFiles(const std::filesystem::path& pack, std::string_view layout)
{
    std::vector<std::filesystem::path> files;
    const std::filesystem::path relative = std::filesystem::path("gui") / (std::string(layout) + ".patch.json");
    const auto layers = pack_path::Layers(pack);
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        const auto candidate = *layer / relative;
        // gui/object_browser.patch.json and the like are JSON data patches (formats/data_patch.h), not layout patches.
        if (IsFile(candidate) && data_patch::PatchFormat(candidate) == data_patch::kGuiPatchFormat) files.push_back(candidate);
    }
    return files;
}

bool ApplyPackPatches(const std::filesystem::path& pack, std::string_view layout, std::string_view baseScriptText,
        std::span<const std::string> activeMods, int32_t screenWidth, int32_t screenHeight, ApplyResult& out, std::string& error)
{
    error.clear();
    out = {};
    const auto files = FindPatchFiles(pack, layout);
    if (files.empty()) {
        out.scriptText.assign(baseScriptText);
        return true;
    }
    std::vector<PatchSource> sources;
    sources.reserve(files.size());
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            error = "mod \"" + ModNameFromPatchPath(file) + "\" (" + file.string() + "): cannot open";
            return false;
        }
        PatchSource src;
        src.modId = ModNameFromPatchPath(file);
        src.file = file.string();
        src.jsonText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        sources.push_back(std::move(src));
    }
    return ApplyPatches(baseScriptText, sources, activeMods, screenWidth, screenHeight, out, error);
}

} // namespace obl::gui_patch
