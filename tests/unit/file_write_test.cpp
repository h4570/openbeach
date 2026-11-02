#include "unit_test.h"
#include "platform/file_write.h"
#include "temp_dir.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace obl;

OBL_TEST_CASE("file_write.write_all_writes_everything_or_fails")
{
    const obl::test::TempPath tmp("obl_file_write_write_all", ".bin");
    const std::string path = tmp.path.string();
    std::vector<char> data(70000, 'x');
    OBL_CHECK(file_write::WriteAll(path.c_str(), data.data(), data.size()));
    OBL_CHECK_EQ(std::filesystem::file_size(path), data.size());
    OBL_CHECK(file_write::WriteAll(path.c_str(), data.data(), 0)); // truncates to an empty file
    OBL_CHECK_EQ(std::filesystem::file_size(path), 0u);
    OBL_CHECK(!file_write::WriteAll("/no/such/dir/obl.bin", data.data(), data.size()));
}
