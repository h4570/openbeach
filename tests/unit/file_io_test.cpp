
#include "unit_test.h"

#include "core/file_io.h"
#include "temp_dir.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace obl::file_io;

namespace {

// A file in the temp directory that disappears with the test.
struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& name, const std::string& content, bool create = true)
        : path(obl::test::UniqueTempPath("obl_file_io_" + name))
    {
        if (create) {
            std::ofstream out(path, std::ios::binary);
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
    }
    ~TempFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    std::string Path() const { return path.string(); }
};

std::vector<void*> g_allocations;
size_t g_lastRequest = 0;

void* CountingAllocate(size_t size)
{
    g_lastRequest = size;
    void* block = std::malloc(size);
    g_allocations.push_back(block);
    return block;
}

void FreeAll()
{
    for (void* block : g_allocations) std::free(block);
    g_allocations.clear();
}

// fread over a byte string; the stream pointer is the object with the read position.
struct MemoryStream {
    std::string bytes;
    size_t position = 0;
};

size_t MemoryRead(void* destination, size_t size, size_t count, void* stream)
{
    MemoryStream* memory = static_cast<MemoryStream*>(stream);
    size_t wanted = size * count;
    size_t available = memory->bytes.size() - memory->position;
    size_t taken = std::min(wanted, available);
    std::memcpy(destination, memory->bytes.data() + memory->position, taken); // a short element still lands in the buffer
    memory->position += taken;
    return taken / size;
}

} // namespace

OBL_TEST_CASE("file_io.file_size")
{
    TempFile three("size3", "abc");
    TempFile empty("size0", "");
    TempFile missing("size_missing", "", false);
    OBL_CHECK_EQ(FileSize(three.Path().c_str()), 3u);
    OBL_CHECK_EQ(FileSize(empty.Path().c_str()), 0u);
    OBL_CHECK_EQ(FileSize(missing.Path().c_str()), 0u);
}

OBL_TEST_CASE("file_io.read_all")
{
    std::string content("\x00\x01\xfe\xff text\r\n", 11);
    TempFile file("all", content);
    auto* buffer = static_cast<uint8_t*>(ReadAll(file.Path().c_str(), &CountingAllocate, false));
    OBL_REQUIRE(buffer != nullptr);
    OBL_CHECK_EQ(g_lastRequest, content.size() + 1);
    OBL_CHECK(std::memcmp(buffer, content.data(), content.size()) == 0);
    OBL_CHECK_EQ(static_cast<int>(buffer[content.size()]), 0x2e); // `.` rather than NUL
    FreeAll();

    auto* inverted = static_cast<uint8_t*>(ReadAll(file.Path().c_str(), &CountingAllocate, true));
    OBL_REQUIRE(inverted != nullptr);
    bool same = true;
    for (size_t i = 0; i < content.size(); ++i) same = same && inverted[i] == static_cast<uint8_t>(~static_cast<uint8_t>(content[i]));
    OBL_CHECK(same);
    OBL_CHECK_EQ(static_cast<int>(inverted[content.size()]), 0x2e); // the terminator is not inverted
    FreeAll();

    TempFile empty("all_empty", "");
    TempFile missing("all_missing", "", false);
    OBL_CHECK(ReadAll(empty.Path().c_str(), &CountingAllocate, false) == nullptr);
    OBL_CHECK(ReadAll(missing.Path().c_str(), &CountingAllocate, true) == nullptr);
    OBL_CHECK(g_allocations.empty()); // nothing allocated for a file that has no content
}

OBL_TEST_CASE("file_io.read_into")
{
    TempFile file("into", "0123456789");
    char buffer[16];
    std::memset(buffer, 'x', sizeof(buffer));
    OBL_CHECK(ReadInto(file.Path().c_str(), buffer, 4));
    OBL_CHECK(std::memcmp(buffer, "0123xxxx", 8) == 0);

    std::memset(buffer, 'x', sizeof(buffer));
    OBL_CHECK(ReadInto(file.Path().c_str(), buffer, 16)); // shorter file: the rest is untouched, still a success
    OBL_CHECK(std::memcmp(buffer, "0123456789xxxxxx", 16) == 0);

    TempFile missing("into_missing", "", false);
    OBL_CHECK(!ReadInto(missing.Path().c_str(), buffer, 4));
}

OBL_TEST_CASE("file_io.read_scalar")
{
    MemoryStream stream{std::string("\x12\x34\x56\x78\x9a\xbc\xde\xf0", 8)};
    const uint32_t handle = 0xa1b2c3d4u;
    OBL_CHECK_EQ(ReadScalar(1, &stream, handle, &MemoryRead), 0x12u);
    OBL_CHECK_EQ(ReadScalar(2, &stream, handle, &MemoryRead), 0x5634u); // little endian
    OBL_CHECK_EQ(ReadScalar(4, &stream, handle, &MemoryRead), 0xdebc9a78u); // bit 31 set, not sign-extended away
    OBL_CHECK_EQ(stream.position, size_t{7});

    OBL_CHECK_EQ(ReadScalar(1, &stream, handle, &MemoryRead), 0xf0u);
    OBL_CHECK_EQ(ReadScalar(1, &stream, handle, &MemoryRead), 0xd4u); // nothing left: low byte of the handle
    OBL_CHECK_EQ(ReadScalar(2, &stream, handle, &MemoryRead), 0xc3d4u);
    OBL_CHECK_EQ(ReadScalar(4, &stream, handle, &MemoryRead), handle);

    MemoryStream shortStream{std::string("\x11\x22\x33", 3)};
    OBL_CHECK_EQ(ReadScalar(4, &shortStream, handle, &MemoryRead), 0xa1332211u); // three bytes arrive, the top one is the handle's
}
