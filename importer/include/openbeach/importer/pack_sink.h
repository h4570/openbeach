#pragma once

// The importer's output (docs/architecture/data-formats/converter/index.md): the converted pack as named files of
// bytes. The desktop CLI writes a directory, the browser its own storage; the writers see only this interface, so the
// browser adapter needs no file system. Paths are relative to the pack root, with '/'.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {

class PackSink {
public:
    virtual ~PackSink() = default;

    // Replaces the file `path` with `bytes`, creating its parent directories; false with `error` when it fails.
    virtual bool Write(std::string_view path, std::span<const uint8_t> bytes, std::string& error) = 0;
    // A file written earlier (or present before the conversion); nullopt when there is none.
    virtual std::optional<std::vector<uint8_t>> Read(std::string_view path) const = 0;
    // Deletes the file `path`; a missing file is not an error.
    virtual bool Remove(std::string_view path) = 0;
    // Every file under the root, relative with '/', in no particular order.
    virtual std::vector<std::string> Files() const = 0;
    // How errors name `path` (the directory sink gives the full file name).
    virtual std::string Describe(std::string_view path) const;

    bool WriteText(std::string_view path, std::string_view text, std::string& error);
};

// The desktop adapter: files under `root` of the local file system.
std::unique_ptr<PackSink> OpenDirectorySink(const std::filesystem::path& root);

// Files held in memory: the browser adapter hands them to its storage, tests inspect them.
class MemorySink final : public PackSink {
public:
    bool Write(std::string_view path, std::span<const uint8_t> bytes, std::string& error) override;
    std::optional<std::vector<uint8_t>> Read(std::string_view path) const override;
    bool Remove(std::string_view path) override;
    std::vector<std::string> Files() const override;

    const std::map<std::string, std::vector<uint8_t>, std::less<>>& files() const { return files_; }

private:
    std::map<std::string, std::vector<uint8_t>, std::less<>> files_;
};

} // namespace obl::convert
