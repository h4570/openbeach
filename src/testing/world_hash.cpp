#include "core/char16.h"
#include "world_hash.h"

#include "game/messages/message_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/log.h"
#include "test_util.h"
#include "game/world/world_snapshot.h"
#include "game/world/world_snapshot_model.h"
#include "determinism.h"
#include "core/wide_path.h"
#include "core/os.h"

#include <array>
#include <charconv>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>

namespace obl::test {
namespace {

std::filesystem::path g_directory;
uint64_t g_steps;
unsigned g_every;
unsigned g_checkpoint;
std::string g_lastHash;
uint64_t g_dueSteps;
unsigned g_deferredNow;
unsigned g_deferred;
bool g_pending;
// Records of the last snapshot; sizes the next one up front.
size_t g_lastFields;
// Gameplay with a built GUI and no state switch or save load queued; the same boundary RunSteps renders on.
bool Settled()
{
    const game::Game* game = Game();
    return World() && GameStateIndex() == 0 && game && !game->m_guiReloadPending && game->m_currentState == game->m_nextState &&
            game->m_loadSlot == -1;
}

// The message handler's save writes only the delayed list, so the hash sees the messages that outlive a frame. The working list is the batch
// the message handler's update is delivering; at a settled frame boundary it is empty, otherwise the hash would miss pending deliveries.
size_t WorkingMessages()
{
    const messages::Handler* handler = World()->m_messageHandler;
    return handler ? handler->working.size() : 0;
}

// The snapshot of the current world (schema 2) from the model (world_snapshot_model.h); false with `error` set outside a settled
// world.
bool Snapshot(std::string& text, std::string& hash, size_t& fields, const char*& error)
{
    const game::Game* game = Game();
    if (!World() || GameStateIndex() != 0 || !game || game->m_guiReloadPending) {
        error = "needs gameplay with a built GUI";
        return false;
    }
    if (WorkingMessages()) {
        error = "messages in the working list are not part of the hash";
        return false;
    }
    uint64_t calls = GameRandTape().Calls();
    auto finish = [&](WorldSnapshot& snapshot) {
        snapshot.Normalize();
        snapshot.Class("RNG");
        snapshot.Add("rand-calls", uint8_t(RecordType::Counter), &calls, sizeof(calls));
    };
    const layout::World& world = *World();
    WorldSnapshot snapshot;
    snapshot.Reserve(g_lastFields);
    world_snapshot::EmitWorld(snapshot, world, world.m_worldScript->m_currentState);
    finish(snapshot);
    g_lastFields = snapshot.Count();
    text = snapshot.Text();
    hash = WorldSnapshot::HashOf(text);
    fields = snapshot.Count();
    return true;
}

// `steps` is what the report line and world_hash_compare.ps1 call the checkpoint's position: frames of `step`, or the tick of a replay.
bool Capture(const std::string& expectation = {}, uint64_t steps = g_steps, std::string* snapshot = nullptr)
{
    std::string text, hash;
    size_t fields = 0;
    const char* error = nullptr;
    if (!Snapshot(text, hash, fields, error)) {
        OBL_LOG("autotest: FAILED world-hash (%s)", error);
        return false;
    }
    auto filename = "checkpoint-" + std::to_string(g_checkpoint++) + ".tsv";
    std::ofstream out(g_directory / filename, std::ios::binary);
    out << text;
    out.close();
    if (!out) {
        OBL_LOG("autotest: FAILED world-hash (cannot write snapshot)");
        return false;
    }
    OBL_LOG("autotest: world-hash schema=%d checkpoint=%u steps=%llu fields=%zu hash=%s file=%s", kWorldHashSchema, g_checkpoint - 1,
            static_cast<unsigned long long>(steps), fields, hash.c_str(), filename.c_str());
    if (expectation.starts_with('=')) {
        OBL_LOG("autotest: %s world-hash expect expected=%s current=%s", hash == expectation.substr(1) ? "PASS" : "FAILED",
                expectation.c_str() + 1, hash.c_str());
    } else if (!expectation.empty()) {
        bool pass = !g_lastHash.empty() && ((hash == g_lastHash) == (expectation == "expect-same"));
        OBL_LOG("autotest: %s world-hash %s previous=%s current=%s", pass ? "PASS" : "FAILED", expectation.c_str(), g_lastHash.c_str(),
                hash.c_str());
    }
    g_lastHash = hash;
    if (snapshot) *snapshot = std::move(text);
    return true;
}

} // namespace

bool HashWorld(WorldHash& out, const char*& error)
{
    return Snapshot(out.text, out.hash, out.fields, error);
}

bool WorldSettled()
{
    return Settled();
}

void InitWorldHash(const Text16& gameDirectory)
{
    const auto directory = os::GetEnv("OBL_WORLD_HASH_DIR");
    g_directory = directory && !directory->empty() ? Utf8Path(*directory) : WidePath(gameDirectory) / "world-hashes";
}

void ResetWorldHash()
{
    g_steps = 0;
    g_every = 0;
    g_checkpoint = 0;
    g_lastHash.clear();
    g_dueSteps = 0;
    g_deferredNow = 0;
    g_deferred = 0;
    g_pending = false;
}

bool ExecuteWorldHashCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "world-hash") return false;
    if (args.size() == 1 && args[0] == "off") {
        g_every = 0;
        g_pending = false;
        g_deferredNow = 0;
        OBL_LOG("autotest: world-hash off");
        return true;
    }
    std::string expectation;
    if (args.size() == 1 && (args[0] == "expect-same" || args[0] == "expect-different")) {
        expectation = args[0];
    } else if (args.size() == 2 && args[0] == "expect") {
        // A hash taken elsewhere (another process, a replay file): 16 lowercase hex digits as the report line prints them.
        const bool valid = args[1].size() == 16 && args[1].find_first_not_of("0123456789abcdef") == std::string::npos;
        if (!valid) {
            OBL_LOG("autotest: FAILED world-hash expect (needs 16 lowercase hex digits)");
            return true;
        }
        expectation = '=' + args[1];
    } else if (!args.empty()) {
        unsigned interval = 0;
        if (args.size() == 2 && args[0] == "every") {
            auto result = std::from_chars(args[1].data(), args[1].data() + args[1].size(), interval);
            if (result.ec != std::errc{} || result.ptr != args[1].data() + args[1].size()) interval = 0;
        }
        if (!interval) {
            OBL_LOG("autotest: FAILED world-hash (expected every N > 0, off, expect-same, expect-different, expect <hash>, or no "
                    "arguments)");
            return true;
        }
        g_every = interval;
        g_steps = 0;
        g_pending = false;
        g_deferredNow = 0;
        if (!Settled()) {
            // Enabled before gameplay exists (e.g. right after boot-gameplay): checkpoint 0 waits for the first settled frame.
            std::error_code directoryError;
            std::filesystem::create_directories(g_directory, directoryError);
            g_pending = true;
            g_dueSteps = 0;
            g_deferredNow = 1;
            OBL_LOG("autotest: world-hash deferred due-steps=0 (state change in progress)");
            return true;
        }
    }
    std::error_code error;
    std::filesystem::create_directories(g_directory, error);
    if (error)
        OBL_LOG("autotest: FAILED world-hash (cannot create snapshot directory)");
    else
        Capture(expectation);
    return true;
}

std::optional<TickCheckpoint> CaptureTickCheckpoint(uint32_t tick)
{
    std::error_code error;
    std::filesystem::create_directories(g_directory, error);
    if (error) {
        OBL_LOG("autotest: FAILED world-hash (cannot create snapshot directory)");
        return std::nullopt;
    }
    TickCheckpoint checkpoint;
    if (!Capture({}, tick, &checkpoint.text)) return std::nullopt;
    const auto result = std::from_chars(g_lastHash.data(), g_lastHash.data() + g_lastHash.size(), checkpoint.hash, 16);
    if (result.ec != std::errc{}) return std::nullopt;
    checkpoint.index = g_checkpoint - 1;
    return checkpoint;
}

WorldHashStats GetWorldHashStats()
{
    return {g_checkpoint, g_deferred, g_pending};
}

bool WorldHashStep()
{
    if (!g_every) return true;
    ++g_steps;
    if (g_steps % g_every == 0 && !g_pending) {
        g_pending = true;
        g_dueSteps = g_steps;
    }
    if (!g_pending) return true;
    // A checkpoint that falls inside load-slot/state switches/deferred GUI reload cannot read the world (world::Save
    // needs a built world and GUI). Moving it to the first settled frame is deterministic: the delay depends on game
    // updates only, never on wall time. Several periods elapsing meanwhile collapse into one checkpoint.
    if (!Settled()) {
        if (g_deferredNow++ == 0)
            OBL_LOG("autotest: world-hash deferred due-steps=%llu (state change in progress)", static_cast<unsigned long long>(g_dueSteps));
        return true;
    }
    if (g_deferredNow) {
        OBL_LOG("autotest: world-hash resumed due-steps=%llu delay-steps=%u", static_cast<unsigned long long>(g_dueSteps), g_deferredNow);
        ++g_deferred;
        g_deferredNow = 0;
    }
    g_pending = false;
    return Capture();
}

} // namespace obl::test
