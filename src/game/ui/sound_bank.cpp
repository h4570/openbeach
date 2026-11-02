#include "game/ui/sound_bank.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <system_error>
#include <unordered_set>

namespace obl::audio {
namespace {
std::string Key(const char* name)
{
    std::string key = name ? name : "";
    for (char& c : key)
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return key;
}
uint32_t U32(const uint8_t* p)
{
    return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
std::mutex g_waveMutex;
struct WaveHint {
    const char* name;
    std::optional<std::string> previous;
    WaveHint(const char* key, const char* value) : name(key)
    {
        if (const char* old = SDL_GetHint(name)) previous = old;
        SDL_SetHintWithPriority(name, value, SDL_HINT_OVERRIDE);
    }
    ~WaveHint()
    {
        if (previous)
            SDL_SetHintWithPriority(name, previous->c_str(), SDL_HINT_OVERRIDE);
        else
            SDL_ResetHint(name);
    }
};
} // namespace

SoundBank::~SoundBank()
{
    Close();
}
void SoundBank::Close()
{
    entries_.clear();
    index_.clear();
}
bool SoundBank::Open(const std::filesystem::path& directory)
{
    return Open(std::span<const std::filesystem::path>(&directory, 1));
}
bool SoundBank::Open(std::span<const std::filesystem::path> directories)
{
    Close();
    std::error_code ec;
    std::vector<BankEntry> found;
    std::unordered_set<std::string> names;
    for (const auto& directory : directories) {
        // A layer that is not there (no mod/ folder) contributes nothing; the error code of the probe is not an error.
        const bool present = std::filesystem::is_directory(directory, ec);
        ec.clear();
        if (!present) continue;
        // Directory iteration with error codes, as os.cpp does: a missing or unreadable directory is a failed open, not a throw.
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            const auto& path = it->path();
            if (!it->is_regular_file(ec) || Key(path.extension().string().c_str()) != ".WAV") continue;
            BankEntry entry;
            entry.name = Key(path.filename().string().c_str());
            // An earlier layer's file of the same name shadows this one.
            if (!names.insert(entry.name).second) continue;
            entry.path = path;
            entry.size = static_cast<uint32_t>(it->file_size(ec));
            if (ec || !entry.size) {
                Close();
                return false;
            }
            found.push_back(std::move(entry));
        }
        if (ec) {
            Close();
            return false;
        }
    }
    if (found.empty()) {
        Close();
        return false;
    }
    // Directory order is not defined; the entries are sorted so that the list and the cache walk are the same every run.
    std::sort(found.begin(), found.end(), [](const BankEntry& a, const BankEntry& b) { return a.name < b.name; });
    for (auto& entry : found) {
        index_.emplace(entry.name, entries_.size());
        entries_.push_back(std::move(entry));
    }
    return true;
}
BankEntry* SoundBank::Find(const char* name)
{
    auto it = index_.find(Key(name));
    return it == index_.end() ? nullptr : &entries_[it->second];
}
const uint8_t* SoundBank::Get(const char* name, uint32_t& size)
{
    size = 0;
    auto* e = Find(name);
    if (!e) return nullptr;
    if (e->bytes.empty()) {
        // Read on first use and keep until Update drops it; the files are small and one open per sound is cheap.
        std::ifstream file(e->path, std::ios::binary);
        e->bytes.resize(e->size);
        if (!file.read(reinterpret_cast<char*>(e->bytes.data()), static_cast<std::streamsize>(e->size))) {
            std::vector<uint8_t>().swap(e->bytes);
            return nullptr;
        }
    }
    ++e->references;
    size = e->size;
    return e->bytes.data();
}
void SoundBank::Release(const char* name)
{
    auto* e = Find(name);
    if (e && --e->references == 0) e->idle = 0;
}
void SoundBank::Update(float dt)
{
    for (auto& e : entries_)
        if (!e.bytes.empty() && e.references == 0 && (e.idle += dt) > 5.0f) std::vector<uint8_t>().swap(e.bytes);
}
size_t SoundBank::CachedBytes() const
{
    size_t size = 0;
    for (const auto& e : entries_) size += e.bytes.size();
    return size;
}
bool DecodeWave(std::span<const uint8_t> bytes, Wave& wave)
{
    wave = {};
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4) ||
            uint64_t(U32(bytes.data() + 4)) + 8 > bytes.size())
        return false;
    SDL_AudioSpec spec{};
    std::lock_guard lock(g_waveMutex);
    // The bank's IMA files end in partial blocks. SDL's default drops those blocks entirely.
    WaveHint truncation(SDL_HINT_WAVE_TRUNCATION, "dropframe");
    // SDL 3.4.16 advances to the next stereo channel too early when fact cuts an 8-frame group.
    // Decode full groups and trim fact ourselves, preserving the final right-channel samples.
    WaveHint fact(SDL_HINT_WAVE_FACT_CHUNK, "ignore");
    uint8_t* data = nullptr;
    uint32_t length = 0;
    if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true, &spec, &data, &length)) return false;
    bool ok = spec.format == SDL_AUDIO_S16LE && spec.channels >= 1 && spec.channels <= 2 && spec.freq > 0 && length > 0 &&
            length % (sizeof(int16_t) * spec.channels) == 0;
    if (ok) {
        wave.channels = spec.channels;
        wave.rate = spec.freq;
        wave.pcm.resize(length / sizeof(int16_t));
        std::memcpy(wave.pcm.data(), data, length);
        const size_t end = size_t(U32(bytes.data() + 4)) + 8;
        for (size_t p = 12; p + 8 <= end;) {
            uint32_t size = U32(bytes.data() + p + 4);
            if (uint64_t(p) + 8 + size > end) {
                ok = false;
                break;
            }
            if (!std::memcmp(bytes.data() + p, "fact", 4) && size >= 4) {
                uint64_t frames = U32(bytes.data() + p + 8);
                if (frames && frames * spec.channels < wave.pcm.size()) wave.pcm.resize(static_cast<size_t>(frames * spec.channels));
            }
            p += 8ull + size + (size & 1);
        }
    }
    SDL_free(data);
    if (!ok) wave = {};
    return ok;
}
} // namespace obl::audio
