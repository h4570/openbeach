#pragma once

// InstallShield 6 cabinets of the Beach Life CDs (`data1.hdr` + `data1.cab`, `data2.cab`), read without installing
// (docs/architecture/data-formats/converter/index.md, "Cabinets"). Only what both CDs use is supported: the one header
// version they carry, plain or compressed files in one volume each. Split and obfuscated files are refused, and every
// extracted file is checked against the MD5 the header stores.

#include "importer/src/source/md5.h"
#include "openbeach/importer/byte_source.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {

struct CabinetFile {
    std::string path; // UTF-8, '/'-separated, relative to the installation ("Data/Sprites/BLSprite.hdr")
    std::string group; // the file group ("EURO Comp")
    uint32_t index = 0; // in the header's file table
    uint16_t flags = 0;
    uint16_t volume = 0; // N of dataN.cab
    uint64_t size = 0;
    uint64_t compressedSize = 0;
    uint64_t offset = 0; // in the volume
    Md5Digest md5 = {};

    static constexpr uint16_t kSplit = 1;
    static constexpr uint16_t kObfuscated = 2;
    static constexpr uint16_t kCompressed = 4;
    static constexpr uint16_t kInvalid = 8;

    bool External() const { return !(flags & kCompressed) && offset == 0; }
};

class Cabinet {
public:
    // Reads the header `headerPath` (`data1.hdr`) of `source`; the volumes are read next to it when a file is extracted.
    // The cabinet keeps a pointer to `source`, so the caller keeps the source alive while the cabinet is used.
    static std::optional<Cabinet> Open(const ByteSource& source, std::string_view headerPath, std::string& error);
    // The header file `header` of the local file system; the cabinet owns a source over its directory.
    static std::optional<Cabinet> Open(const std::filesystem::path& header, std::string& error);

    // Every valid entry (invalid placeholders are dropped), in header order.
    const std::vector<CabinetFile>& Files() const { return files_; }

    // The entry at `path` ('/'-separated, ASCII case ignored); nullptr with `error` when there is none or more than one
    // (the language variants of a file live in several groups).
    const CabinetFile* Find(std::string_view path, std::string& error) const;

    // Streams the file's contents to `sink` (false from the sink aborts) and checks size and MD5 at the end; the sink
    // may have received data before an error is reported.
    bool Extract(const CabinetFile& file, const std::function<bool(std::span<const uint8_t>)>& sink, std::string& error) const;
    // The whole file in memory, refused above `maxSize` bytes.
    bool ExtractToMemory(const CabinetFile& file, std::vector<uint8_t>& out, std::string& error, uint64_t maxSize = 64u << 20) const;

private:
    std::shared_ptr<const ByteSource> owned_; // set by the path overload only
    const ByteSource* source_ = nullptr;
    std::string directory_; // of the header inside the source, "" for its root
    std::vector<CabinetFile> files_;
};

} // namespace obl::convert
