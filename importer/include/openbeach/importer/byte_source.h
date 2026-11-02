#pragma once

// The importer's input (docs/architecture/data-formats/converter/index.md): the user's copy of the game as named
// files of bytes. The desktop CLI reads an installation directory, the browser the files or the disc image the player
// picked; the readers see only this interface, so both share every decoder. Paths are relative to the source's root,
// with '/', spelled as the source stores them; FindFile resolves names case-insensitively.

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {

struct SourceEntry {
    std::string name; // UTF-8, one path component
    bool directory = false;
};

class ByteSource {
public:
    virtual ~ByteSource() = default;

    // The entries of `directory` ("" is the root) in the source's own order; nullopt when it is not a directory.
    virtual std::optional<std::vector<SourceEntry>> List(std::string_view directory) const = 0;
    // The size of the file `path`; nullopt when it is not a file.
    virtual std::optional<uint64_t> Size(std::string_view path) const = 0;
    // Fills `out` with the bytes of `path` from `offset`; false when the file is missing, shorter or unreadable.
    virtual bool Read(std::string_view path, uint64_t offset, std::span<uint8_t> out) const = 0;
    // How errors name `path` (the desktop source gives the full file name).
    virtual std::string Describe(std::string_view path) const;

    // The whole file; nullopt when it cannot be read.
    std::optional<std::vector<uint8_t>> ReadAll(std::string_view path) const;
};

// `relative` (with '/') in `source`, matching every component without regard to ASCII case, since the old game
// compares names case-insensitively and Linux file systems do not; an exact match wins. The path as the source spells
// it, or nullopt when a component is missing.
std::optional<std::string> FindFile(const ByteSource& source, std::string_view relative);

// The desktop adapter: the files under `root` of the local file system.
std::unique_ptr<ByteSource> OpenDirectorySource(const std::filesystem::path& root);

// Files held in memory: the browser adapter fills one from the player's picked files, tests from synthetic bytes.
class MemorySource final : public ByteSource {
public:
    // Adds or replaces the file `path` (with '/'); its parent directories exist from then on.
    void Add(std::string_view path, std::vector<uint8_t> bytes);

    std::optional<std::vector<SourceEntry>> List(std::string_view directory) const override;
    std::optional<uint64_t> Size(std::string_view path) const override;
    bool Read(std::string_view path, uint64_t offset, std::span<uint8_t> out) const override;

private:
    std::map<std::string, std::vector<uint8_t>, std::less<>> files_;
};

} // namespace obl::convert
