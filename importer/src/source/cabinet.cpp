#include "importer/src/source/cabinet.h"

#include "importer/src/source/source.h"

#include "core/ascii_case.h"

#include <zlib.h>

#include <algorithm>
#include <iterator>

namespace obl::convert {

namespace {

// Common header of data1.hdr and every volume: signature "ISc(", version, volume info, cab descriptor offset and size.
constexpr uint32_t kSignature = 0x28635349;
// The only version both CDs carry (InstallShield 6); others are refused rather than guessed.
constexpr uint32_t kVersion = 0x0100600C;
constexpr size_t kCommonHeaderSize = 20;

// Cab descriptor (at the common header's descriptor offset) fields.
constexpr size_t kFileTableOffset = 0x0C;
constexpr size_t kDirectoryCount = 0x1C;
constexpr size_t kFileCount = 0x28;
constexpr size_t kFileTableOffset2 = 0x2C;
constexpr size_t kFileGroupOffsets = 0x3E;
constexpr size_t kFileGroupSlots = 71;

// File descriptor (version 6): 0x57 bytes in the second part of the file table.
constexpr size_t kFileDescriptorSize = 0x57;
constexpr size_t kFdFlags = 0x00, kFdSize = 0x02, kFdCompressedSize = 0x0A, kFdOffset = 0x12, kFdMd5 = 0x1A, kFdName = 0x3A,
                 kFdDirectory = 0x3E, kFdVolume = 0x55;

// File group descriptor: name offset, then (version 6) first and last file index.
constexpr size_t kGroupFirstFile = 0x16, kGroupLastFile = 0x1A;

constexpr size_t kOutputBlock = 64 * 1024;

// Bounds-checked little-endian reads from the header bytes. Positions are 64-bit: a damaged offset plus a base must not wrap
// around back into the header in the 32-bit browser build.
class HeaderReader {
public:
    explicit HeaderReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    bool Has(uint64_t at, size_t width) const { return at <= bytes_.size() && bytes_.size() - at >= width; }
    // At most 8 bytes: a wider shift is undefined (UBSan caught the file table's bounds check reading 87 bytes).
    std::optional<uint64_t> Read(uint64_t at, size_t width) const
    {
        if (width > 8 || !Has(at, width)) return std::nullopt;
        uint64_t v = 0;
        for (size_t i = 0; i < width; ++i) v |= static_cast<uint64_t>(bytes_[static_cast<size_t>(at) + i]) << (8 * i);
        return v;
    }
    std::optional<uint32_t> U32(uint64_t at) const
    {
        auto v = Read(at, 4);
        return v ? std::optional<uint32_t>(static_cast<uint32_t>(*v)) : std::nullopt;
    }
    std::optional<uint16_t> U16(uint64_t at) const
    {
        auto v = Read(at, 2);
        return v ? std::optional<uint16_t>(static_cast<uint16_t>(*v)) : std::nullopt;
    }
    // A NUL-terminated name in the CD's code page (Windows-1252), as UTF-8.
    std::optional<std::string> Name(uint64_t at) const
    {
        if (at >= bytes_.size()) return std::nullopt;
        auto end = std::find(bytes_.begin() + static_cast<ptrdiff_t>(at), bytes_.end(), 0);
        if (end == bytes_.end()) return std::nullopt;
        std::string out;
        for (auto it = bytes_.begin() + static_cast<ptrdiff_t>(at); it != end; ++it) AppendUtf8(out, CodePoint(*it));
        return out;
    }

private:
    static uint32_t CodePoint(uint8_t c)
    {
        // Windows-1252 differs from Latin-1 only in 0x80..0x9F; its five unassigned bytes become U+FFFD.
        static constexpr uint16_t kHigh[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122,
                0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
        return c >= 0x80 && c < 0xA0 ? kHigh[c - 0x80] : c;
    }
    static void AppendUtf8(std::string& out, uint32_t cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    const std::vector<uint8_t>& bytes_;
};

} // namespace

std::optional<Cabinet> Cabinet::Open(const std::filesystem::path& header, std::string& error)
{
    std::shared_ptr<const ByteSource> source = OpenDirectorySource(header.parent_path());
    const auto name = header.filename().u8string();
    auto cabinet = Open(*source, std::string_view(reinterpret_cast<const char*>(name.data()), name.size()), error);
    if (cabinet) cabinet->owned_ = std::move(source);
    return cabinet;
}

std::optional<Cabinet> Cabinet::Open(const ByteSource& source, std::string_view headerPath, std::string& error)
{
    error.clear();
    const auto read = source.ReadAll(headerPath);
    if (!read) {
        error = "cannot read " + source.Describe(headerPath);
        return std::nullopt;
    }
    const std::vector<uint8_t>& bytes = *read;
    const HeaderReader hdr(bytes);
    auto fail = [&](const std::string& why) {
        error = source.Describe(headerPath) + ": " + why;
        return std::nullopt;
    };

    if (hdr.U32(0) != kSignature) return fail("not an InstallShield cabinet header");
    if (hdr.U32(4) != kVersion) return fail("unsupported InstallShield version (only that of the Beach Life CDs is read)");
    const uint64_t desc = hdr.U32(12).value_or(0);
    const auto fileTable = hdr.U32(desc + kFileTableOffset);
    const auto directoryCount = hdr.U32(desc + kDirectoryCount);
    const auto fileCount = hdr.U32(desc + kFileCount);
    const auto fileTable2 = hdr.U32(desc + kFileTableOffset2);
    if (desc < kCommonHeaderSize || !fileTable || !directoryCount || !fileCount || !fileTable2) return fail("truncated cab descriptor");
    // Refused before anything is sized by the count: a damaged one would ask for gigabytes.
    if (*fileCount > bytes.size() / kFileDescriptorSize) return fail("truncated file table");
    const uint64_t table = desc + *fileTable;

    std::vector<std::string> directories;
    for (uint32_t i = 0; i < *directoryCount; ++i) {
        auto nameOffset = hdr.U32(table + 4 * uint64_t{i});
        auto name = nameOffset ? hdr.Name(table + *nameOffset) : std::nullopt;
        if (!name) return fail("bad directory name " + std::to_string(i));
        std::replace(name->begin(), name->end(), '\\', '/');
        directories.push_back(*name);
    }

    // File groups own index ranges of the file table.
    std::vector<std::string> groupOf(*fileCount);
    for (size_t slot = 0; slot < kFileGroupSlots; ++slot) {
        auto node = hdr.U32(desc + kFileGroupOffsets + 4 * slot);
        if (!node) return fail("truncated file group table");
        for (size_t guard = 0; *node != 0; ++guard) {
            auto descriptor = hdr.U32(desc + *node + 4);
            auto next = hdr.U32(desc + *node + 8);
            if (!descriptor || !next || guard > *fileCount) return fail("bad file group list");
            const uint64_t group = desc + *descriptor;
            auto nameOffset = hdr.U32(group);
            auto name = nameOffset ? hdr.Name(desc + *nameOffset) : std::nullopt;
            auto first = hdr.U32(group + kGroupFirstFile);
            auto last = hdr.U32(group + kGroupLastFile);
            if (!name || !first || !last || *first > *last || *last >= *fileCount) return fail("bad file group descriptor");
            for (uint32_t i = *first; i <= *last; ++i) {
                if (!groupOf[i].empty()) return fail("file " + std::to_string(i) + " is in two groups");
                groupOf[i] = *name;
            }
            node = next;
        }
    }

    Cabinet cabinet;
    cabinet.source_ = &source;
    const size_t slash = headerPath.rfind('/');
    cabinet.directory_ = slash == std::string_view::npos ? std::string() : std::string(headerPath.substr(0, slash));
    for (uint32_t i = 0; i < *fileCount; ++i) {
        const uint64_t fd = table + *fileTable2 + uint64_t{i} * kFileDescriptorSize;
        auto flags = hdr.U16(fd + kFdFlags);
        if (!flags || !hdr.Has(fd, kFileDescriptorSize)) return fail("truncated file table");
        if (*flags & CabinetFile::kInvalid) continue;
        CabinetFile file;
        file.index = i;
        file.flags = *flags;
        file.size = *hdr.Read(fd + kFdSize, 8);
        file.compressedSize = *hdr.Read(fd + kFdCompressedSize, 8);
        file.offset = *hdr.Read(fd + kFdOffset, 8);
        std::copy_n(bytes.begin() + static_cast<ptrdiff_t>(fd + kFdMd5), file.md5.size(), file.md5.begin());
        file.volume = *hdr.U16(fd + kFdVolume);
        const uint16_t directory = *hdr.U16(fd + kFdDirectory);
        auto name = hdr.Name(table + *hdr.U32(fd + kFdName));
        if (!name || directory >= directories.size()) return fail("bad name of file " + std::to_string(i));
        file.path = directories[directory].empty() ? *name : directories[directory] + "/" + *name;
        file.group = groupOf[i];
        cabinet.files_.push_back(std::move(file));
    }
    return cabinet;
}

const CabinetFile* Cabinet::Find(std::string_view path, std::string& error) const
{
    const CabinetFile* found = nullptr;
    for (const CabinetFile& file : files_) {
        if (!ascii::EqualsNoCase(file.path, path)) continue;
        if (found) {
            error = std::string(path) + " is in more than one file group (" + found->group + ", " + file.group + ")";
            return nullptr;
        }
        found = &file;
    }
    if (!found) error = std::string(path) + " is not in the cabinet";
    return found;
}

bool Cabinet::Extract(const CabinetFile& file, const std::function<bool(std::span<const uint8_t>)>& sink, std::string& error) const
{
    auto fail = [&](const std::string& why) {
        error = file.path + ": " + why;
        return false;
    };
    if (file.External()) return fail("not stored in the cabinet (it lies next to it on the CD)");
    if (file.flags & (CabinetFile::kSplit | CabinetFile::kObfuscated)) return fail("split or obfuscated files are not supported");

    const std::string volumeName = "data" + std::to_string(file.volume) + ".cab";
    const ByteSource& source = *source_;
    const auto volumePath = FindFile(source, directory_.empty() ? volumeName : directory_ + "/" + volumeName);
    if (!volumePath) return fail(volumeName + " not found next to the header");
    std::vector<uint8_t> commonBytes(kCommonHeaderSize);
    const bool commonRead = source.Read(*volumePath, 0, commonBytes);
    const HeaderReader volume(commonBytes);
    if (!commonRead || volume.U32(0) != kSignature || volume.U32(4) != kVersion)
        return fail(volumeName + " is not a matching cabinet volume");

    // The stored bytes must lie inside this volume: a file running past its end would be split, which is refused.
    const auto volumeSize = source.Size(*volumePath);
    const uint64_t stored = (file.flags & CabinetFile::kCompressed) ? file.compressedSize : file.size;
    if (!volumeSize || file.offset < kCommonHeaderSize || file.offset > *volumeSize || *volumeSize - file.offset < stored)
        return fail("stored bytes lie outside " + volumeName);
    // Sequential reads from the file's offset on.
    uint64_t position = file.offset;
    auto readNext = [&](uint8_t* data, size_t size) {
        if (!source.Read(*volumePath, position, {data, size})) return false;
        position += size;
        return true;
    };

    Md5 md5;
    uint64_t written = 0;
    auto emit = [&](std::span<const uint8_t> data) {
        md5.Update(data);
        written += data.size();
        return written <= file.size && sink(data);
    };

    std::vector<uint8_t> input;
    std::vector<uint8_t> output(kOutputBlock);
    if (!(file.flags & CabinetFile::kCompressed)) {
        input.resize(kOutputBlock);
        for (uint64_t left = file.size; left > 0;) {
            const size_t n = static_cast<size_t>(std::min<uint64_t>(left, input.size()));
            if (!readNext(input.data(), n)) return fail("read error");
            if (!emit({input.data(), n})) return fail("output aborted or larger than listed");
            left -= n;
        }
    } else {
        // A sequence of chunks: u16 length, then that many bytes of an independent raw deflate stream.
        z_stream z = {};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return fail("inflateInit2 failed");
        struct Cleanup {
            z_stream& z;
            ~Cleanup() { inflateEnd(&z); }
        } cleanup{z};
        for (uint64_t left = file.compressedSize; left > 0;) {
            uint8_t lengthBytes[2];
            if (left < 2 || !readNext(lengthBytes, 2)) return fail("truncated chunk header");
            const size_t length = lengthBytes[0] | static_cast<size_t>(lengthBytes[1]) << 8;
            left -= 2;
            if (length > left) return fail("chunk runs past the stored size");
            input.resize(length);
            if (!readNext(input.data(), length)) return fail("read error");
            left -= length;

            inflateReset(&z);
            z.next_in = input.data();
            z.avail_in = static_cast<uInt>(length);
            for (;;) {
                z.next_out = output.data();
                z.avail_out = static_cast<uInt>(output.size());
                const int ret = inflate(&z, Z_SYNC_FLUSH);
                const size_t produced = output.size() - z.avail_out;
                // Chunks end on a sync flush rather than a final block, so "no progress, input used up" ends one too.
                if (ret != Z_OK && ret != Z_STREAM_END && !(ret == Z_BUF_ERROR && produced == 0 && z.avail_in == 0))
                    return fail("corrupt compressed data");
                if (produced && !emit({output.data(), produced})) return fail("output aborted or larger than listed");
                if (ret == Z_STREAM_END || (z.avail_in == 0 && z.avail_out != 0)) break;
            }
        }
    }
    if (written != file.size) return fail("size differs from the listed " + std::to_string(file.size) + " bytes");
    if (md5.Digest() != file.md5) return fail("MD5 mismatch (damaged CD or cabinet)");
    return true;
}

bool Cabinet::ExtractToMemory(const CabinetFile& file, std::vector<uint8_t>& out, std::string& error, uint64_t maxSize) const
{
    out.clear();
    if (file.size > maxSize) {
        error = file.path + ": " + std::to_string(file.size) + " bytes is too large to extract into memory";
        return false;
    }
    out.reserve(static_cast<size_t>(file.size));
    return Extract(
            file,
            [&](std::span<const uint8_t> data) {
                out.insert(out.end(), data.begin(), data.end());
                return true;
            },
            error);
}

} // namespace obl::convert
