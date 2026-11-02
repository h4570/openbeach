#include "game/game/command_recorder.h"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

namespace obl::command {

namespace {

constexpr std::string_view kMagic = "OBL-replay";

bool ParseU32(std::string_view text, uint32_t& out)
{
    if (text.empty()) return false;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

bool ParseI32(std::string_view text, int32_t& out)
{
    if (text.empty()) return false;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

// Exactly 16 hex digits, the spelling of the world-hash report.
bool ParseHash(std::string_view text, uint64_t& out)
{
    if (text.size() != 16) return false;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out, 16);
    return ec == std::errc{} && end == text.data() + text.size();
}

std::string FormatHash(uint64_t hash)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text(16, '0');
    for (int i = 15; i >= 0; --i, hash >>= 4) text[i] = kHex[hash & 15];
    return text;
}

// "word rest": splits off the first space-separated word.
std::pair<std::string_view, std::string_view> SplitWord(std::string_view line)
{
    const size_t space = line.find(' ');
    if (space == std::string_view::npos) return {line, {}};
    return {line.substr(0, space), line.substr(space + 1)};
}

std::string_view TrimLine(std::string_view line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    return line;
}

} // namespace

bool Recorder::Record(uint32_t tick, const Command& command)
{
    if (m_finished) return false;
    if (m_lastCommand && tick < *m_lastCommand) return false;
    if (m_lastCheckpoint && tick < *m_lastCheckpoint) return false;
    m_recording.commands.push_back({tick, command});
    m_lastCommand = tick;
    return true;
}

bool Recorder::AddCheckpoint(uint32_t tick, uint64_t hash, std::optional<SectionDigests> sections)
{
    if (m_finished) return false;
    if (m_lastCheckpoint && tick <= *m_lastCheckpoint) return false;
    if (m_lastCommand && tick <= *m_lastCommand) return false;
    m_recording.checkpoints.push_back({tick, hash, std::move(sections)});
    m_lastCheckpoint = tick;
    return true;
}

bool Recorder::Finish(uint32_t endTick)
{
    if (m_finished) return false;
    if (m_lastCommand && endTick <= *m_lastCommand) return false;
    if (m_lastCheckpoint && endTick < *m_lastCheckpoint) return false;
    m_recording.endTick = endTick;
    m_finished = true;
    return true;
}

std::string WriteReplay(const Recording& recording)
{
    const ReplayHeader& header = recording.header;
    std::string text;
    text += std::string(kMagic) + ' ' + std::to_string(kReplayFormatVersion) + '\n';
    text += std::string("start ") + (header.startKind == StartKind::Save ? "save " : "scenario ") + EncodeString(header.start) + '\n';
    text += "seed " + std::to_string(header.seed) + '\n';
    text += "fixed-dt " + FormatFloat(header.fixedDt) + '\n';
    text += "hash-schema " + std::to_string(header.hashSchema) + '\n';
    // At equal ticks the checkpoint goes first: it is the state before that tick's commands.
    size_t c = 0;
    size_t h = 0;
    while (c < recording.commands.size() || h < recording.checkpoints.size()) {
        const bool takeHash = h < recording.checkpoints.size() &&
                (c == recording.commands.size() || recording.checkpoints[h].tick <= recording.commands[c].tick);
        if (takeHash) {
            const Checkpoint& point = recording.checkpoints[h++];
            text += '@' + std::to_string(point.tick) + " hash " + FormatHash(point.hash);
            if (point.sections) {
                const std::string sections = FormatSectionDigests(*point.sections);
                if (!sections.empty()) text += ' ' + sections;
            }
            text += '\n';
        } else {
            const TimedCommand& entry = recording.commands[c++];
            text += '@' + std::to_string(entry.tick) + ' ' + Format(entry.command) + '\n';
        }
    }
    text += "end " + std::to_string(recording.endTick) + '\n';
    return text;
}

std::optional<Recording> ReadReplay(std::string_view text, std::string* error)
{
    int lineNumber = 0;
    auto fail = [&](const std::string& why) -> std::optional<Recording> {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + why;
        return std::nullopt;
    };

    // Header lines in their fixed order, then the body.
    enum class Expect : uint8_t { Magic, Start, Seed, FixedDt, HashSchema, Body, Done };
    Expect expect = Expect::Magic;
    ReplayHeader header;
    std::optional<Recorder> recorder;

    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t newline = text.find('\n', pos);
        const size_t stop = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = TrimLine(text.substr(pos, stop - pos));
        pos = stop + 1;
        ++lineNumber;
        if (line.empty() || line.front() == '#') continue;

        const auto [word, rest] = SplitWord(line);
        switch (expect) {
        case Expect::Magic: {
            int32_t version = 0;
            if (word != kMagic || !ParseI32(rest, version)) return fail("expected '" + std::string(kMagic) + " <version>'");
            if (version != kReplayFormatVersion) return fail("unsupported replay format version " + std::string(rest));
            expect = Expect::Start;
            break;
        }
        case Expect::Start: {
            const auto [kind, name] = SplitWord(rest);
            if (word != "start") return fail("expected 'start scenario|save <name>'");
            if (kind == "scenario")
                header.startKind = StartKind::Scenario;
            else if (kind == "save")
                header.startKind = StartKind::Save;
            else
                return fail("no such start kind '" + std::string(kind) + "'");
            auto decoded = DecodeString(name);
            if (!decoded || decoded->empty()) return fail("bad start name");
            header.start = std::move(*decoded);
            expect = Expect::Seed;
            break;
        }
        case Expect::Seed:
            if (word != "seed" || !ParseU32(rest, header.seed)) return fail("expected 'seed <n>'");
            expect = Expect::FixedDt;
            break;
        case Expect::FixedDt: {
            const auto dt = ParseFloat(rest);
            if (word != "fixed-dt" || !dt || !(*dt > 0.0f)) return fail("expected 'fixed-dt <seconds>' above 0");
            header.fixedDt = *dt;
            expect = Expect::HashSchema;
            break;
        }
        case Expect::HashSchema:
            if (word != "hash-schema" || !ParseI32(rest, header.hashSchema)) return fail("expected 'hash-schema <n>'");
            recorder.emplace(header);
            expect = Expect::Body;
            break;
        case Expect::Body: {
            if (word == "end") {
                uint32_t endTick = 0;
                if (!ParseU32(rest, endTick)) return fail("expected 'end <tick>'");
                if (!recorder->Finish(endTick)) return fail("end tick " + std::to_string(endTick) + " does not follow the last entry");
                expect = Expect::Done;
                break;
            }
            uint32_t tick = 0;
            if (word.size() < 2 || word.front() != '@' || !ParseU32(word.substr(1), tick)) return fail("expected '@<tick> ...' or 'end'");
            const auto [kind, args] = SplitWord(rest);
            if (kind == "hash") {
                const auto [total, rest] = SplitWord(args);
                uint64_t hash = 0;
                if (!ParseHash(total, hash)) return fail("expected '@<tick> hash <16 hex digits> [<section>=<digest> ...]'");
                std::optional<SectionDigests> sections;
                if (!rest.empty()) {
                    std::string why;
                    sections = ParseSectionDigests(rest, &why);
                    if (!sections) return fail(why);
                }
                if (!recorder->AddCheckpoint(tick, hash, std::move(sections)))
                    return fail("checkpoint at tick " + std::to_string(tick) + " out of order");
                break;
            }
            std::string why;
            const auto command = Parse(rest, &why);
            if (!command) return fail(why);
            if (!recorder->Record(tick, *command)) return fail("command at tick " + std::to_string(tick) + " out of order");
            break;
        }
        case Expect::Done: return fail("text after 'end'");
        }
    }
    if (expect != Expect::Done) return fail("missing 'end' (truncated replay)");
    return recorder->Result();
}

std::optional<StartScenario> ParseStartScenario(std::string_view name)
{
    StartScenario start;
    std::string_view number;
    if (name.starts_with("sandbox")) {
        start.sandbox = true;
        number = name.substr(7);
    } else if (name.starts_with("s")) {
        number = name.substr(1);
    } else if (!name.empty() && name.front() >= '1' && name.front() <= '9') {
        number = name; // the script recorder's `start scenario <n>`
    } else {
        return std::nullopt;
    }
    if (number.empty() || number.front() == '0' || !ParseI32(number, start.island) || start.island < 1) return std::nullopt;
    return start;
}

std::vector<Command> ReplayCursor::Due(uint32_t tick)
{
    std::vector<Command> due;
    const auto& commands = m_recording->commands;
    while (m_next < commands.size() && commands[m_next].tick <= tick) due.push_back(commands[m_next++].command);
    return due;
}

std::optional<uint64_t> ReplayCursor::ExpectedHash(uint32_t tick) const
{
    const Checkpoint* point = ExpectedCheckpoint(tick);
    if (!point) return std::nullopt;
    return point->hash;
}

const Checkpoint* ReplayCursor::ExpectedCheckpoint(uint32_t tick) const
{
    const auto& points = m_recording->checkpoints;
    const auto it = std::lower_bound(points.begin(), points.end(), tick, [](const Checkpoint& p, uint32_t t) { return p.tick < t; });
    if (it == points.end() || it->tick != tick) return nullptr;
    return &*it;
}

} // namespace obl::command
