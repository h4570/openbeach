#include "game/objects/script_program.h"

#include "core/ascii_case.h"
#include "game/map/path_tile_script.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace obl::script_program {
namespace {

using namespace script;

[[noreturn]] void Fail(const std::string& message)
{
    throw ParseError(message);
}

bool StartsWith(const std::string& text, const char* prefix)
{
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

// These operand kinds own heap data released during command list teardown. Also accept 0xbc, 0xc2,
// 0xc6 and 0xd2..0xd5, which the parser never produces (their data is zero), so freeing them is harmless.
bool OwnsData(int32_t kind)
{
    switch (kind) {
    case ValueString:
    case ValueRandDword:
    case ValueRandom:
    case ValueNumber:
    case 0xbc:
    case 0xc2:
    case 0xc6:
    case 0xd2:
    case 0xd3:
    case 0xd4:
    case 0xd5: return true;
    default: return false;
    }
}

// ---- The parser ---------------------------------------------------------------------------------------------------------------------

// Which list of the state being filled `AppendCommand` reaches when no `If` or `Switch` is open (appending a command to the state).
enum List : int { OnEntry = 0, OnExit = 1, OnUpdate = 2, OnReceipt = 3, OnSend = 4 };

class Parser {
public:
    Parser(ScriptProgram& script, const char* text, size_t size, const Vocabulary& vocabulary, const Memory& memory)
        : m_script(script), m_vocabulary(vocabulary), m_memory(memory), m_size(size)
    {
        // The terminator the decoded script reader appends, then a tail that stops every scan: a newline ends a comment, and a `;` is a
        // token that needs no look further (so no scan reads past the buffer).
        m_text.assign(text, size + 1);
        m_text += "\n\n;";
    }

    // Loading an object script: `State` blocks until the text is used up.
    void Load()
    {
        if (m_size == 0) return;
        do {
            m_pos += path_tile_script::IsSpace(m_text[m_pos]) ? SkipSpace() : 0;
            m_pos += path_tile_script::SkipComments(m_text.c_str() + m_pos);
            if (m_size <= m_pos) break;
            std::string token = Read();
            if (token == "State")
                ParseState();
            else
                Fail("unrecognized top-level command in the object script: " + token);
        } while (m_pos < m_size);
    }

private:
    ScriptProgram& m_script;
    const Vocabulary& m_vocabulary;
    const Memory& m_memory;
    size_t m_size;
    std::string m_text;
    size_t m_pos = 0;
    IfCommand* m_openIf = nullptr; // the `If` being filled, null at state level
    SwitchCommand* m_openSwitch = nullptr; // the `Switch` being filled, null outside one
    bool m_inIf = false; // an `If` body is being parsed, so commands skip the open switch
    std::vector<IfCommand*> m_ifStack; // the `If`s a nested one interrupted

    // ---- Memory ------------------------------------------------------------------------------------------------------------------

    void* Allocate(size_t size)
    {
        void* block = m_memory.allocate(size);
        std::memset(block, 0, size);
        return block;
    }

    template<typename T> T* New() { return static_cast<T*>(Allocate(sizeof(T))); }

    char* NewString(const std::string& text)
    {
        char* block = static_cast<char*>(m_memory.allocate(text.size() + 1));
        std::memcpy(block, text.c_str(), text.size() + 1);
        return block;
    }

    void* NewInt(int32_t value)
    {
        void* block = m_memory.allocate(sizeof(int32_t));
        std::memcpy(block, &value, sizeof(value));
        return block;
    }

    void* NewFloat(float value)
    {
        void* block = m_memory.allocate(sizeof(float));
        std::memcpy(block, &value, sizeof(float));
        return block;
    }

    // ---- Text --------------------------------------------------------------------------------------------------------------------

    size_t SkipSpace()
    {
        size_t at = 0;
        while (path_tile_script::IsSpace(m_text[m_pos + at])) ++at;
        return at;
    }

    // token boundaries follow the same rules as path tile scripts.
    std::string Read()
    {
        std::string token;
        m_pos += static_cast<size_t>(path_tile_script::ReadToken(m_text.c_str() + m_pos, token));
        if (m_pos > m_size) Fail("script ends inside a command");
        return token;
    }

    char At(size_t at) const
    {
        if (at > m_size) Fail("script ends inside a command");
        return m_text[at];
    }

    // The `(...)` of an `If` or `Switch`: skips to the `(`, copies it up to and including the `)` and moves behind it.
    std::string ReadParenthesis()
    {
        size_t at = m_pos;
        while (At(at) != '(') ++at;
        std::string text;
        while (At(at) != ')') text.push_back(m_text[at++]);
        text.push_back(')');
        m_pos = at + 1;
        return text;
    }

    // The condition buffers act as NUL-terminated: past the `)` they read as a stop.
    static char Peek(const std::string& text, size_t at) { return at < text.size() ? text[at] : '\0'; }

    // ---- Identifiers -------------------------------------------------------------------------------------------------------------

    // Linear search over the names; -1 when the name is not in the table.
    static int FindExact(const std::vector<NamedId>& table, const std::string& name)
    {
        for (const NamedId& row : table) {
            if (row.name == name) return row.id;
        }
        return -1;
    }

    static int FindNoCase(const std::vector<NamedId>& table, const std::string& name)
    {
        for (const NamedId& row : table) {
            if (ascii::EqualsNoCase(row.name, name)) return row.id;
        }
        return -1;
    }

    int StateId(const std::string& name) const { return FindExact(m_vocabulary.states, name); }
    int MessageId(const std::string& name) const { return FindExact(m_vocabulary.messages, name); }

    // Resolving a script operand: a variable or constant of the vocabulary, a quoted text, `RandDWORD<n>`,
    // `Random<n>`, `STATE_<name>` or a number.
    ScriptOperand Resolve(const std::string& token)
    {
        int id = FindNoCase(m_vocabulary.values, token);
        if (id >= 0) return {id, nullptr};
        if (!token.empty() && token[0] == '"') return {ValueString, NewString(Unquote(token))};
        if (StartsWith(token, "RandDWORD")) return {ValueRandDword, NewInt(std::atoi(token.c_str() + 9))};
        if (StartsWith(token, "Random")) return {ValueRandom, NewInt(std::atoi(token.c_str() + 6))};
        if (StartsWith(token, "STATE_")) {
            int state = StateId(token);
            if (state < 0) Fail("unrecognized state name: " + token);
            return {ValueNumber, NewFloat(static_cast<float>(state))};
        }
        char first = token.empty() ? '\0' : token[0];
        if ((first < '0' || first > '9') && first != '-') Fail("unrecognized name in the object script: " + token);
        return {ValueNumber, NewFloat(static_cast<float>(std::atof(token.c_str())))};
    }

    // A quoted token keeps everything between its first and last byte (the last byte is taken to be the closing quote).
    static std::string Unquote(const std::string& token) { return token.size() >= 2 ? token.substr(1, token.size() - 2) : std::string(); }

    // ---- Command lists -----------------------------------------------------------------------------------------------------------

    ScriptCommand* NewNode(ObjectOpcode opcode, void* payload)
    {
        auto* node = New<ScriptCommand>();
        node->m_opcode = opcode;
        node->m_data = payload;
        return node;
    }

    static void AppendToList(ScriptCommand*& head, ScriptCommand* node)
    {
        if (!head) {
            head = node;
            return;
        }
        ScriptCommand* last = head;
        while (last->m_next) last = last->m_next;
        last->m_next = node;
    }

    // Append to the selected list of the state currently being parsed.
    void AddToState(ScriptCommand* node, int list)
    {
        if (m_script.m_NumStates >= kMaxProgramStates) Fail("too many states");
        ProgramState& state = m_script.m_states[m_script.m_NumStates];
        switch (list) {
        case OnEntry: AppendToList(state.m_onEntry, node); return;
        case OnExit: AppendToList(state.m_onExit, node); return;
        case OnUpdate: AppendToList(state.m_onUpdate, node); return;
        case OnReceipt:
        case OnSend:
            if (state.m_NumHandlers >= kScriptProgramHandlers) Fail("too many message handlers in one state");
            AppendToList(
                    list == OnReceipt ? state.m_handlers[state.m_NumHandlers].m_onReceipt : state.m_handlers[state.m_NumHandlers].m_onSend,
                    node);
            return;
        default: Fail("command added to a list that does not exist");
        }
    }

    // Appending a script command: into the open `If` (its `Else` list once `Else` was seen), else into the last `Case`
    // of the open `Switch` unless an `If` is open, else into the list of the state.
    void Append(ScriptCommand* node, int list)
    {
        if (!m_openSwitch || m_inIf) {
            if (!m_openIf) {
                AddToState(node, list);
                return;
            }
            AppendToList(m_openIf->m_inElse == 0 ? m_openIf->m_then : m_openIf->m_else, node);
            return;
        }
        SwitchCase* last = m_openSwitch->m_firstCase;
        if (!last) Fail("Switch without a Case");
        while (last->m_next) last = last->m_next;
        AppendToList(last->m_commands, node);
    }

    // ---- Blocks ------------------------------------------------------------------------------------------------------------------

    // Parsing a state block, after the `State` token: the name, then OnEntry/OnExit/OnUpdate/Message blocks up to `End`.
    void ParseState()
    {
        if (m_script.m_NumStates >= kMaxProgramStates) Fail("too many states");
        ProgramState& state = m_script.m_states[m_script.m_NumStates];
        std::string name = Read();
        int id = StateId(name);
        if (id < 0) Fail("unrecognized state name: " + name);
        state.m_stateId = id;
        for (std::string token = Read(); token != "End"; token = Read()) {
            if (token == "OnEntry")
                ParseCommands(OnEntry);
            else if (token == "OnExit")
                ParseCommands(OnExit);
            else if (token == "OnUpdate")
                ParseCommands(OnUpdate);
            else if (token == "Message")
                ParseMessageHandler();
            else
                Fail("unrecognized command in a state: " + token);
        }
        ++m_script.m_NumStates;
    }

    // Parsing a message handler, after `Message`: `<MT_...>` with OnReceipt/OnSend blocks up to `End`.
    void ParseMessageHandler()
    {
        ProgramState& state = m_script.m_states[m_script.m_NumStates];
        if (state.m_NumHandlers >= kScriptProgramHandlers) Fail("too many message handlers in one state");
        std::string name = Read();
        int id = MessageId(name);
        if (id < 0) Fail("unrecognized message name: " + name);
        state.m_handlers[state.m_NumHandlers].m_messageId = id;
        for (std::string token = Read(); token != "End"; token = Read()) {
            if (token == "OnReceipt")
                ParseCommands(OnReceipt);
            else if (token == "OnSend")
                ParseCommands(OnSend);
            else
                Fail("unrecognized command in a message handler: " + token);
        }
        ++state.m_NumHandlers;
    }

    // Parsing a command block: commands up to the `End` that closes the block.
    void ParseCommands(int list)
    {
        for (std::string token = Read(); token != "End"; token = Read()) {
            if (token == "PlayAnimation")
                ParseAnimation(list);
            else if (token == "Function")
                ParseFunction(list);
            else if (token == "Message")
                ParseSendMessage(list);
            else if (token == "ChangeState")
                ParseChangeState(list);
            else if (token == "If")
                ParseIf(list);
            else if (token == "Else") {
                if (!m_openIf) Fail("Else without If");
                m_openIf->m_inElse = 1;
            } else if (token == "Switch")
                ParseSwitch(list);
            else if (token == "Case")
                ParseCase();
            else if (token == "Add")
                ParseArithmetic(OpAdd, list);
            else if (token == "Minus")
                ParseArithmetic(OpMinus, list);
            else if (token == "Divide")
                ParseArithmetic(OpDivide, list);
            else if (token == "Multiply")
                ParseArithmetic(OpMultiply, list);
            else if (token == "Set")
                ParseArithmetic(OpSet, list);
            else if (token == "PlaySound")
                ParsePlaySound(list);
            else
                Fail("unrecognized command: " + token);
        }
    }

    // ---- Commands ----------------------------------------------------------------------------------------------------------------

    // Parsing a function call: `Function <name>;` with the id of the name in the function table.
    void ParseFunction(int list)
    {
        std::string name = Read();
        int id = FindNoCase(m_vocabulary.functions, name);
        if (id < 0) Fail("unrecognized function in the object script: " + name);
        auto* payload = New<FunctionCommand>();
        payload->m_functionId = id;
        Read(); // `;`
        Append(NewNode(OpFunction, payload), list);
    }

    // Parsing a message command: `Message <MT_...> <receiver> <data> <flags> {Time t | MaxTime t | FloatData v | State v
    // | SendFrom v} ;`.
    void ParseSendMessage(int list)
    {
        auto* payload = New<MessageCommand>();
        payload->m_floatData.m_kind = ValueNone;
        payload->m_state.m_kind = ValueNone;
        payload->m_sendFrom.m_kind = ValueNone;
        std::string name = Read();
        int id = MessageId(name);
        if (id < 0) Fail("unrecognized message for SendMessage: " + name);
        payload->m_messageId = id;
        payload->m_receiver = Resolve(Read());
        payload->m_data = Resolve(Read());
        payload->m_flags = Resolve(Read());
        for (std::string token = Read(); token != ";"; token = Read()) {
            if (token == "Time")
                payload->m_time = static_cast<float>(std::atof(Read().c_str()));
            else if (token == "MaxTime")
                payload->m_maxTime = static_cast<float>(std::atof(Read().c_str()));
            else if (token == "FloatData")
                payload->m_floatData = Resolve(Read());
            else if (token == "State")
                payload->m_state = Resolve(Read());
            else if (token == "SendFrom")
                payload->m_sendFrom = Resolve(Read());
            else
                Fail("unrecognized command after a message: " + token);
        }
        Append(NewNode(OpMessage, payload), list);
    }

    // Parsing a state change: `ChangeState <STATE_...> [Time t];`. An unknown state name leaves the id 0.
    void ParseChangeState(int list)
    {
        auto* payload = New<ChangeStateCommand>();
        std::string name = Read();
        int id = StateId(name);
        payload->m_stateId = id < 0 ? 0 : id;
        std::string token = Read();
        if (token != ";") {
            if (token != "Time") Fail("unrecognized ChangeState option: " + token);
            payload->m_time = static_cast<float>(std::atof(Read().c_str()));
            Read(); // `;`
        }
        Append(NewNode(OpChangeState, payload), list);
    }

    // Parsing an animation command: `PlayAnimation <name> {Finish n | NotLooped | ExitState s | MaxTime t | EndMessage m |
    // Reverse} ;`.
    void ParseAnimation(int list)
    {
        std::string name = Read();
        auto* payload = New<AnimationCommand>();
        payload->m_name = NewString(name);
        payload->m_endMessage = kNoMessage;
        for (std::string token = Read(); token != ";"; token = Read()) {
            if (token == "Finish")
                payload->m_finish = NewString(Read());
            else if (token == "NotLooped")
                payload->m_notLooped = 1;
            else if (token == "ExitState") {
                int id = StateId(Read());
                if (id >= 0) payload->m_exitState = id;
            } else if (token == "MaxTime")
                payload->m_maxTime = static_cast<float>(std::atof(Read().c_str()));
            else if (token == "EndMessage") {
                int id = MessageId(Read());
                if (id >= 0) payload->m_endMessage = id;
            } else if (token == "Reverse")
                payload->m_reverse = 1;
            else
                Fail("unrecognized animation command: " + token);
        }
        Append(NewNode(OpAnimation, payload), list);
    }

    // Parse Add, Minus, Divide, Multiply and Set commands: `<command> <target> <source>;`.
    void ParseArithmetic(ObjectOpcode opcode, int list)
    {
        auto* payload = New<ArithmeticCommand>();
        payload->m_target = Resolve(Read());
        payload->m_source = Resolve(Read());
        Read(); // `;`
        Append(NewNode(opcode, payload), list);
    }

    // Parsing a sound command: `PlaySound <name>;`.
    void ParsePlaySound(int list)
    {
        auto* payload = New<SoundCommand>();
        payload->m_name = NewString(Read());
        Read(); // `;`
        Append(NewNode(OpPlaySound, payload), list);
    }

    // Parsing an if branch: `If (left [op right])... [Else...] End`. The parenthesis is cut out of the text and its
    // parts are split at single spaces; the operator ends at a space only.
    void ParseIf(int list)
    {
        bool savedInIf = m_inIf;
        std::string text = ReadParenthesis();
        auto* condition = New<IfCommand>();
        size_t at = 1;
        while (Peek(text, at) == ' ') ++at;
        std::string left;
        while (Peek(text, at) != ' ' && Peek(text, at) != ')') left.push_back(text[at++]);
        condition->m_left = Resolve(left);
        while (Peek(text, at) == ' ') ++at;
        if (Peek(text, at) == ')') {
            condition->m_right.m_kind = ValueNone;
        } else {
            std::string op;
            while (Peek(text, at) != ' ') {
                if (at >= text.size()) Fail("unrecognized If operator: " + op);
                op.push_back(text[at++]);
            }
            condition->m_op = Operator(op);
            while (Peek(text, at) == ' ') ++at;
            std::string right;
            while (Peek(text, at) != ' ' && Peek(text, at) != ')') right.push_back(text[at++]);
            if (!right.empty() && right[0] == '"')
                condition->m_right = {ValueString, NewString(Unquote(right))};
            else
                condition->m_right = Resolve(right);
        }
        Append(NewNode(OpIf, condition), list);
        m_inIf = true;
        if (!m_openIf) {
            m_openIf = condition;
        } else {
            m_ifStack.push_back(m_openIf);
            m_openIf = condition;
        }
        ParseCommands(list);
        m_inIf = savedInIf;
        if (!m_ifStack.empty()) {
            m_openIf = m_ifStack.back();
            m_ifStack.pop_back();
        } else {
            m_openIf = nullptr;
        }
    }

    static int32_t Operator(const std::string& op)
    {
        if (op == "&&") return 0;
        if (op == "||") return 1;
        if (op == ">") return 2;
        if (op == "<") return 3;
        if (op == "==") return 4;
        if (op == "<=" || op == "=<") return 5;
        if (op == ">=" || op == "=>") return 6;
        if (op == "!=") return 7;
        Fail("unrecognized If operator: " + op);
    }

    // Parsing a switch block: `Switch (value)` then `Case` blocks up to `End`. Commands go to the last case, not to an
    // enclosing `If`'s lists, and the open switch is forgotten afterwards (a nested `Switch` ends its parent's cases too).
    void ParseSwitch(int list)
    {
        bool savedInIf = m_inIf;
        std::string text = ReadParenthesis();
        auto* control = New<SwitchCommand>();
        size_t at = 1;
        while (Peek(text, at) == ' ') ++at;
        std::string value;
        while (Peek(text, at) != ' ' && Peek(text, at) != ')') value.push_back(text[at++]);
        control->m_value = Resolve(value);
        Append(NewNode(OpSwitch, control), list);
        m_inIf = false;
        m_openSwitch = control;
        ParseCommands(list);
        m_inIf = savedInIf;
        m_openSwitch = nullptr;
    }

    // Parsing a case branch: `Case <value>:` appends a case to the open switch.
    void ParseCase()
    {
        std::string token = Read();
        size_t colon = token.find(':');
        if (colon == std::string::npos) Fail("Case without ':' in " + token);
        if (!m_openSwitch) Fail("Case outside a Switch");
        auto* entry = New<SwitchCase>();
        entry->m_value = Resolve(token.substr(0, colon));
        if (!m_openSwitch->m_firstCase) {
            m_openSwitch->m_firstCase = entry;
        } else {
            SwitchCase* last = m_openSwitch->m_firstCase;
            while (last->m_next) last = last->m_next;
            last->m_next = entry;
        }
    }
};

// ---- Destroy ------------------------------------------------------------------------------------------------------------------------

class Releaser {
public:
    explicit Releaser(const Memory& memory) : m_memory(memory) {}

    // Script state teardown: the three state lists and the handlers' lists.
    void State(ProgramState& state)
    {
        List(state.m_onEntry);
        List(state.m_onExit);
        List(state.m_onUpdate);
        for (uint32_t i = 0; i < kScriptProgramHandlers; ++i) {
            List(state.m_handlers[i].m_onReceipt);
            List(state.m_handlers[i].m_onSend);
        }
        state.m_NumHandlers = 0;
    }

private:
    const Memory& m_memory;

    void Free(void* block)
    {
        if (block) m_memory.release(block);
    }

    // Release owned operand data and clear the pointer.
    void Value(ScriptOperand& value)
    {
        if (OwnsData(value.m_kind)) Free(value.m_data);
        value.m_data = nullptr;
    }

    // Release each node and its payload, including the list head.
    void List(ScriptCommand*& head)
    {
        ScriptCommand* node = head;
        head = nullptr;
        while (node) {
            ScriptCommand* next = node->m_next;
            Payload(*node);
            Free(node);
            node = next;
        }
    }

    void Payload(ScriptCommand& node)
    {
        void* data = node.m_data;
        if (!data) return;
        switch (node.m_opcode) {
        case OpFunction:
        case OpChangeState: break;
        case OpMessage: {
            auto* message = static_cast<MessageCommand*>(data);
            Value(message->m_receiver);
            Value(message->m_data);
            Value(message->m_flags);
            Value(message->m_floatData);
            Value(message->m_state);
            Value(message->m_sendFrom);
            break;
        }
        case OpAnimation: {
            auto* animation = static_cast<AnimationCommand*>(data);
            Free(animation->m_name);
            Free(animation->m_finish);
            break;
        }
        case OpIf: {
            auto* condition = static_cast<IfCommand*>(data);
            List(condition->m_else);
            List(condition->m_then);
            Value(condition->m_left);
            Value(condition->m_right);
            break;
        }
        case OpSwitch: {
            auto* control = static_cast<SwitchCommand*>(data);
            Value(control->m_value);
            SwitchCase* entry = control->m_firstCase;
            while (entry) {
                SwitchCase* next = entry->m_next;
                List(entry->m_commands);
                Value(entry->m_value);
                Free(entry);
                entry = next;
            }
            break;
        }
        case OpAdd:
        case OpMinus:
        case OpDivide:
        case OpMultiply:
        case OpSet: {
            auto* arithmetic = static_cast<ArithmeticCommand*>(data);
            Value(arithmetic->m_target);
            Value(arithmetic->m_source);
            break;
        }
        case OpPlaySound: Free(static_cast<SoundCommand*>(data)->m_name); break;
        default: break;
        }
        Free(data);
        node.m_data = nullptr;
    }
};

// ---- Diff ---------------------------------------------------------------------------------------------------------------------------

std::string Hex(int64_t value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<long long>(value));
    return buffer;
}

bool SameFloat(const void* a, const void* b)
{
    return a && b && std::memcmp(a, b, sizeof(float)) == 0;
}

bool SameText(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    return std::strcmp(a, b) == 0;
}

class Comparer {
public:
    std::string Run(const ScriptProgram& a, const ScriptProgram& b)
    {
        if (a.m_NumStates != b.m_NumStates) return "state count " + std::to_string(a.m_NumStates) + " vs " + std::to_string(b.m_NumStates);
        for (uint32_t i = 0; i < a.m_NumStates && i < kMaxProgramStates; ++i) {
            m_where = "state " + std::to_string(i);
            const ProgramState& x = a.m_states[i];
            const ProgramState& y = b.m_states[i];
            if (x.m_stateId != y.m_stateId) return Where("id " + std::to_string(x.m_stateId) + " vs " + std::to_string(y.m_stateId));
            std::string base = m_where + " (id " + std::to_string(x.m_stateId) + ")";
            if (x.m_NumHandlers != y.m_NumHandlers)
                return base + ": handler count " + std::to_string(x.m_NumHandlers) + " vs " + std::to_string(y.m_NumHandlers);
            if (!List(x.m_onEntry, y.m_onEntry, base + "/OnEntry") || !List(x.m_onExit, y.m_onExit, base + "/OnExit") ||
                    !List(x.m_onUpdate, y.m_onUpdate, base + "/OnUpdate"))
                return m_message;
            for (uint32_t h = 0; h < x.m_NumHandlers && h < kScriptProgramHandlers; ++h) {
                std::string handler = base + "/Message[" + std::to_string(h) + "]";
                if (x.m_handlers[h].m_messageId != y.m_handlers[h].m_messageId)
                    return handler + ": message " + std::to_string(x.m_handlers[h].m_messageId) + " vs " +
                            std::to_string(y.m_handlers[h].m_messageId);
                if (!List(x.m_handlers[h].m_onReceipt, y.m_handlers[h].m_onReceipt, handler + "/OnReceipt") ||
                        !List(x.m_handlers[h].m_onSend, y.m_handlers[h].m_onSend, handler + "/OnSend"))
                    return m_message;
            }
        }
        return {};
    }

private:
    std::string m_where;
    std::string m_message;

    std::string Where(const std::string& detail) { return m_where + ": " + detail; }

    bool Fail(const std::string& path, const std::string& detail)
    {
        m_message = path + ": " + detail;
        return false;
    }

    bool Value(const ScriptOperand& a, const ScriptOperand& b, const std::string& path)
    {
        if (a.m_kind != b.m_kind) return Fail(path, "value kind " + Hex(a.m_kind) + " vs " + Hex(b.m_kind));
        switch (a.m_kind) {
        case ValueString:
            return SameText(static_cast<const char*>(a.m_data), static_cast<const char*>(b.m_data)) || Fail(path, "text differs");
        case ValueRandDword:
        case ValueRandom:
        case ValueNumber: return SameFloat(a.m_data, b.m_data) || Fail(path, "number differs");
        default:
            // No value, or a variable: the data half of the pair carries no meaning, so it is not compared.
            return true;
        }
    }

    bool Cases(const SwitchCase* a, const SwitchCase* b, const std::string& path)
    {
        for (int index = 0; a || b; ++index, a = a->m_next, b = b->m_next) {
            std::string at = path + "/Case[" + std::to_string(index) + "]";
            if (!a || !b) return Fail(at, "case count differs");
            if (!Value(a->m_value, b->m_value, at) || !List(a->m_commands, b->m_commands, at)) return false;
        }
        return true;
    }

    bool Command(const ScriptCommand& a, const ScriptCommand& b, const std::string& path)
    {
        if (a.m_opcode != b.m_opcode) return Fail(path, "opcode " + std::to_string(a.m_opcode) + " vs " + std::to_string(b.m_opcode));
        if (!a.m_data || !b.m_data) return a.m_data == b.m_data || Fail(path, "payload missing");
        switch (a.m_opcode) {
        case OpFunction: {
            auto* x = static_cast<const FunctionCommand*>(a.m_data);
            auto* y = static_cast<const FunctionCommand*>(b.m_data);
            return x->m_functionId == y->m_functionId ||
                    Fail(path, "function " + std::to_string(x->m_functionId) + " vs " + std::to_string(y->m_functionId));
        }
        case OpMessage: {
            auto* x = static_cast<const MessageCommand*>(a.m_data);
            auto* y = static_cast<const MessageCommand*>(b.m_data);
            if (x->m_messageId != y->m_messageId)
                return Fail(path, "message " + std::to_string(x->m_messageId) + " vs " + std::to_string(y->m_messageId));
            if (std::memcmp(&x->m_time, &y->m_time, 4) != 0 || std::memcmp(&x->m_maxTime, &y->m_maxTime, 4) != 0)
                return Fail(path, "Time/MaxTime differ");
            return Value(x->m_receiver, y->m_receiver, path + " receiver") && Value(x->m_data, y->m_data, path + " data") &&
                    Value(x->m_flags, y->m_flags, path + " flags") && Value(x->m_floatData, y->m_floatData, path + " FloatData") &&
                    Value(x->m_state, y->m_state, path + " State") && Value(x->m_sendFrom, y->m_sendFrom, path + " SendFrom");
        }
        case OpChangeState: {
            auto* x = static_cast<const ChangeStateCommand*>(a.m_data);
            auto* y = static_cast<const ChangeStateCommand*>(b.m_data);
            if (x->m_stateId != y->m_stateId || std::memcmp(&x->m_time, &y->m_time, 4) != 0)
                return Fail(path, "ChangeState " + std::to_string(x->m_stateId) + " vs " + std::to_string(y->m_stateId));
            return true;
        }
        case OpAnimation: {
            auto* x = static_cast<const AnimationCommand*>(a.m_data);
            auto* y = static_cast<const AnimationCommand*>(b.m_data);
            if (!SameText(x->m_name, y->m_name) || !SameText(x->m_finish, y->m_finish)) return Fail(path, "animation names differ");
            if (x->m_endMessage != y->m_endMessage || x->m_exitState != y->m_exitState || x->m_reverse != y->m_reverse ||
                    x->m_notLooped != y->m_notLooped || std::memcmp(&x->m_maxTime, &y->m_maxTime, 4) != 0)
                return Fail(path, "animation options differ");
            return true;
        }
        case OpIf: {
            auto* x = static_cast<const IfCommand*>(a.m_data);
            auto* y = static_cast<const IfCommand*>(b.m_data);
            if (!Value(x->m_left, y->m_left, path + " left") || !Value(x->m_right, y->m_right, path + " right")) return false;
            // Without a right value the operator carries no meaning, so it is compared only with one.
            if (x->m_right.m_kind != ValueNone && x->m_op != y->m_op)
                return Fail(path, "operator " + std::to_string(x->m_op) + " vs " + std::to_string(y->m_op));
            if (x->m_inElse != y->m_inElse) return Fail(path, "else flag differs");
            return List(x->m_then, y->m_then, path + "/then") && List(x->m_else, y->m_else, path + "/else");
        }
        case OpSwitch: {
            auto* x = static_cast<const SwitchCommand*>(a.m_data);
            auto* y = static_cast<const SwitchCommand*>(b.m_data);
            return Value(x->m_value, y->m_value, path + " value") && Cases(x->m_firstCase, y->m_firstCase, path);
        }
        case OpAdd:
        case OpMinus:
        case OpDivide:
        case OpMultiply:
        case OpSet: {
            auto* x = static_cast<const ArithmeticCommand*>(a.m_data);
            auto* y = static_cast<const ArithmeticCommand*>(b.m_data);
            return Value(x->m_target, y->m_target, path + " target") && Value(x->m_source, y->m_source, path + " source");
        }
        case OpPlaySound:
            return SameText(static_cast<const SoundCommand*>(a.m_data)->m_name, static_cast<const SoundCommand*>(b.m_data)->m_name) ||
                    Fail(path, "sound name differs");
        default: return Fail(path, "unsupported opcode " + std::to_string(a.m_opcode));
        }
    }

    bool List(const ScriptCommand* a, const ScriptCommand* b, const std::string& path)
    {
        for (int index = 0; a || b; ++index, a = a->m_next, b = b->m_next) {
            std::string at = path + "[" + std::to_string(index) + "]";
            if (!a || !b) return Fail(at, "list length differs");
            if (!Command(*a, *b, at)) return false;
        }
        return true;
    }
};

void CountList(const ScriptCommand* node, ListStats& stats)
{
    for (; node; node = node->m_next) {
        ++stats.nodes;
        if (node->m_opcode >= 0 && node->m_opcode < 16) ++stats.opcodes[node->m_opcode];
        if (node->m_opcode == OpIf && node->m_data) {
            auto* condition = static_cast<const IfCommand*>(node->m_data);
            CountList(condition->m_then, stats);
            CountList(condition->m_else, stats);
        } else if (node->m_opcode == OpSwitch && node->m_data) {
            for (const SwitchCase* entry = static_cast<const SwitchCommand*>(node->m_data)->m_firstCase; entry; entry = entry->m_next)
                CountList(entry->m_commands, stats);
        }
    }
}

} // namespace

void Parse(ScriptProgram& script, const char* text, size_t size, const Vocabulary& vocabulary, const Memory& memory)
{
    script.m_text = nullptr;
    Parser(script, text, size, vocabulary, memory).Load();
}

void Destroy(ScriptProgram& script, const Memory& memory)
{
    Releaser releaser(memory);
    for (int i = 0; i < kMaxProgramStates; ++i) releaser.State(script.m_states[i]);
    script.m_NumStates = 0;
}

void Construct(ScriptProgram& script)
{
    for (auto& state : script.m_states) {
        state.m_stateId = 0x0fffffff;
        for (auto& handler : state.m_handlers) {
            handler.m_messageId = 0x0fffffff;
            handler.m_onReceipt = nullptr;
            handler.m_onSend = nullptr;
        }
        state.m_NumHandlers = 0;
        state.m_onEntry = nullptr;
        state.m_onExit = nullptr;
        state.m_onUpdate = nullptr;
    }
    script.m_NumStates = 0;
    script.m_openCommand = nullptr;
    script.m_openSwitch = nullptr;
}

namespace {
void FreeOwnedValue(const ScriptOperand& value, const Memory& memory)
{
    if (OwnsData(value.m_kind)) memory.release(value.m_data);
}

void FreeHead(ScriptCommand* head, const Memory& memory)
{
    if (head) {
        FreeCommand(*head, memory);
        memory.release(head);
    }
}
} // namespace

void FreeCommand(ScriptCommand& head, const Memory& memory)
{
    // Tail first, like the recursive reference. Its only write is clearing the successor pointer.
    if (head.m_next) {
        FreeHead(head.m_next, memory);
        head.m_next = nullptr;
    }
    void* data = head.m_data;
    if (!data) return;
    switch (head.m_opcode) {
    case OpFunction:
    case OpChangeState: break;
    case OpMessage: {
        const auto& m = *static_cast<MessageCommand*>(data);
        for (const auto* v : {&m.m_receiver, &m.m_data, &m.m_flags, &m.m_floatData, &m.m_state, &m.m_sendFrom}) FreeOwnedValue(*v, memory);
        break;
    }
    case OpAnimation: {
        const auto& a = *static_cast<AnimationCommand*>(data);
        if (a.m_name) memory.release(a.m_name);
        if (a.m_finish) memory.release(a.m_finish);
        break;
    }
    case OpIf: {
        const auto& c = *static_cast<IfCommand*>(data);
        FreeHead(c.m_else, memory);
        FreeHead(c.m_then, memory);
        FreeOwnedValue(c.m_left, memory);
        FreeOwnedValue(c.m_right, memory);
        break;
    }
    case OpSwitch: {
        const auto& s = *static_cast<SwitchCommand*>(data);
        FreeOwnedValue(s.m_value, memory);
        for (auto* c = s.m_firstCase; c;) {
            FreeHead(c->m_commands, memory);
            FreeOwnedValue(c->m_value, memory);
            auto* next = c->m_next;
            memory.release(c);
            c = next;
        }
        break;
    }
    case OpAdd:
    case OpMinus:
    case OpDivide:
    case OpMultiply:
    case OpSet: {
        const auto& a = *static_cast<ArithmeticCommand*>(data);
        FreeOwnedValue(a.m_target, memory);
        FreeOwnedValue(a.m_source, memory);
        break;
    }
    case OpPlaySound: {
        const auto& s = *static_cast<SoundCommand*>(data);
        if (s.m_name) memory.release(s.m_name);
        break;
    }
    default: return; // Unknown opcodes leave their payload alone, but their successors are still destroyed.
    }
    memory.release(data);
}

void DestructStates(ScriptProgram& script, const Memory& memory)
{
    // Array destruction visits all 60 states and all 30 handlers in reverse order.
    for (int i = kMaxProgramStates - 1; i >= 0; --i) {
        auto& state = script.m_states[i];
        FreeHead(state.m_onEntry, memory);
        FreeHead(state.m_onExit, memory);
        FreeHead(state.m_onUpdate, memory);
        for (int h = kScriptProgramHandlers - 1; h >= 0; --h) {
            FreeHead(state.m_handlers[h].m_onReceipt, memory);
            FreeHead(state.m_handlers[h].m_onSend, memory);
        }
    }
}

std::string Diff(const ScriptProgram& a, const ScriptProgram& b)
{
    return Comparer().Run(a, b);
}

ListStats Count(const ScriptProgram& script)
{
    ListStats stats;
    for (uint32_t i = 0; i < script.m_NumStates && i < kMaxProgramStates; ++i) {
        const ProgramState& state = script.m_states[i];
        CountList(state.m_onEntry, stats);
        CountList(state.m_onExit, stats);
        CountList(state.m_onUpdate, stats);
        for (uint32_t h = 0; h < state.m_NumHandlers && h < kScriptProgramHandlers; ++h) {
            CountList(state.m_handlers[h].m_onReceipt, stats);
            CountList(state.m_handlers[h].m_onSend, stats);
        }
    }
    return stats;
}

} // namespace obl::script_program
