#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace obl::audio {
struct BankEntry {
    std::string name;
    std::filesystem::path path;
    uint32_t size = 0;
    std::vector<uint8_t> bytes;
    float idle = 0;
    int references = 0;
};

class SoundBank {
public:
    SoundBank() = default;
    SoundBank(const SoundBank&) = delete;
    SoundBank& operator=(const SoundBank&) = delete;
    ~SoundBank();
    // Every `<name>.wav` file of `directory` is one entry (converter/sounds.md, `sounds/`). Names are keys like the old
    // bank's: upper case with the ".WAV" suffix, so `name + ".wav"` finds them. False, with no entries, when the
    // directory is missing, has no .wav file, or a .wav file is empty.
    bool Open(const std::filesystem::path& directory);
    // The layered form: the directories are read in order and a name found in an earlier one shadows the later files of
    // that name (the mod layer before packs/base, pack_path.h). A directory that does not exist is skipped; the bank
    // is refused when no entry is found at all.
    bool Open(std::span<const std::filesystem::path> directories);
    void Close();
    const uint8_t* Get(const char* name, uint32_t& size);
    void Release(const char* name);
    void Update(float dt);
    bool Contains(const char* name) { return Find(name) != nullptr; }
    size_t CachedBytes() const;
    const std::vector<BankEntry>& Entries() const { return entries_; }

private:
    BankEntry* Find(const char* name);
    FILE* file_ = nullptr;
    std::vector<BankEntry> entries_;
    std::unordered_map<std::string, size_t> index_;
};

struct Wave {
    int channels = 0, rate = 0;
    std::vector<int16_t> pcm;
};
// SDL's RIFF decoder handles PCM and Microsoft IMA ADPCM, including stereo blocks.
bool DecodeWave(std::span<const uint8_t> bytes, Wave& wave);
} // namespace obl::audio
