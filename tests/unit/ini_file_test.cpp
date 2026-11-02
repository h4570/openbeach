// Portable ini files (src/core/ini_file.h). The expectations are what the Windows profile API did on Windows 11; on
// Windows the same cases also run against GetPrivateProfileStringA/WritePrivateProfileStringA.

#include "unit_test.h"

#include "core/ini_file.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace obl;

namespace {

struct WriteCase {
    const char* name;
    std::string before;
    const char* section;
    const char* key;
    const char* value;
    std::string after;
};

const WriteCase kWrites[] = {
        {"new key", "[Sec]\r\nKey=v1\r\n", "Sec", "Key2", "v2", "[Sec]\r\nKey=v1\r\nKey2=v2\r\n"},
        {"new section", "[Sec]\r\nKey=v1\r\n", "Other", "K", "x", "[Sec]\r\nKey=v1\r\n[Other]\r\nK=x\r\n"},
        {"overwrite ignores case", "[Sec]\r\nKey=v1\r\n", "sec", "KEY", "V3", "[Sec]\r\nKey=V3\r\n"},
        {"before blank lines", "[A]\r\nx=1\r\n\r\n\r\n[B]\r\ny=2", "A", "z", "3", "[A]\r\nx=1\r\nz=3\r\n\r\n\r\n[B]\r\ny=2\r\n"},
        {"unterminated file, in place", "[A]\r\nx=1", "A", "x", "2", "[A]\r\nx=2\r\n"},
        {"unterminated file, same value", "[A]\r\nx=1\r\n[B]\r\ny=2", "A", "x", "1", "[A]\r\nx=1\r\n[B]\r\ny=2\r\n"},
        {"LF file, unterminated", "[A]\nx=1\n[B]\ny=2", "A", "z", "1", "[A]\nx=1\nz=1\r\n[B]\ny=2\r\n"},
        {"ends in LF", "[A]\r\nx=1\r\n[B]\r\ny=2\n", "A", "z", "1", "[A]\r\nx=1\r\nz=1\r\n[B]\r\ny=2\n"},
        {"last line without ending", "[A]\r\nx=1\r\n\r\n\r\n[B]\r\ny=2", "B", "w", "4", "[A]\r\nx=1\r\n\r\n\r\n[B]\r\ny=2\r\nw=4\r\n"},
        {"value in place", "  [ A ]  \r\n  x  =  1 2  \r\n", "A", "x", "9", "  [ A ]  \r\n  x  =9  \r\n"},
        {"after spaced entry", "  [ A ]  \r\n  x  =  1 2  \r\n", "A", "y", "9", "  [ A ]  \r\n  x  =  1 2  \r\ny=9\r\n"},
        {"raw value", "[A]\r\nx=1\r\n", "A", "x", " sp ", "[A]\r\nx= sp \r\n"},
        {"LF file new key", "[A]\nx=1\n", "A", "y", "2", "[A]\nx=1\ny=2\r\n"},
        {"LF file overwrite", "[A]\nx=1\ny=3\n", "A", "x", "2", "[A]\nx=2\ny=3\n"},
        {"empty file", "", "A", "y", "2", "[A]\r\ny=2\r\n"},
        {"empty section", "x=0\r\n[A]\r\n", "A", "y", "2", "x=0\r\n[A]\r\ny=2\r\n"},
        {"header without ending", "x=0\r\n[A]", "A", "y", "2", "x=0\r\n[A]\r\ny=2\r\n"},
        {"first of two sections", "[A]\r\n;c=1\r\nx\r\n[A]\r\nq=5\r\n", "A", "q", "6", "[A]\r\nq=6\r\n;c=1\r\nx\r\n[A]\r\nq=5\r\n"},
        {"before a comment", "[A]\r\nx=1\r\n;comment\r\n\r\n[B]\r\n", "A", "y", "2", "[A]\r\nx=1\r\ny=2\r\n;comment\r\n\r\n[B]\r\n"},
        {"new section after unterminated", "[A]\r\nx=1", "C", "y", "2", "[A]\r\nx=1\r\n[C]\r\ny=2\r\n"},
        {"same value", "[A]\r\nx=1\r\n", "A", "x", "1", "[A]\r\nx=1\r\n"},
        {"header without bracket", "[A\r\nx=1\r\n", "A", "x", "2", "[A\r\nx=2\r\n"},
        {"header with trailing text", "[A] junk\r\nx=1\r\n", "A", "x", "2", "[A] junk\r\nx=2\r\n"},
        {"key with spaces", "[A]\r\nmy key = 1\r\n", "A", "my key", "2", "[A]\r\nmy key =2\r\n"},
        {"tabs", "[A]\r\n\tx\t=\t1\t\r\n", "A", "x", "2", "[A]\r\n\tx\t=2\t\r\n"},
};

struct ReadCase {
    const char* name;
    std::string text;
    const char* section;
    const char* key;
    const char* fallback;
    size_t size;
    std::string expected;
};

const ReadCase kReads[] = {
        {"basic", "[A]\r\nx=hello\r\n", "A", "x", "DEF", 64, "hello"},
        {"trimmed", "  [ A ]  \r\n  x  =  1 2  \r\n", "A", "x", "DEF", 64, "1 2"},
        {"trimmed names", "  [ A ]  \r\n  x  =  1 2  \r\n", " A ", " x ", "DEF", 64, "1 2"},
        {"double quotes", "[A]\r\nx=\"q v\"\r\n", "A", "x", "DEF", 64, "q v"},
        {"single quotes", "[A]\r\ny='s'\r\n", "A", "y", "DEF", 64, "s"},
        {"open quote", "[A]\r\nz=\"a\r\n", "A", "z", "DEF", 64, "\"a"},
        {"inner quote", "[A]\r\nw=\"a\"b\"\r\n", "A", "w", "DEF", 64, "a\"b"},
        {"empty quotes", "[A]\r\nx=\"\"\r\n", "A", "x", "DEF", 64, ""},
        {"quoted blank", "[A]\r\ny=\" \"\r\n", "A", "y", "DEF", 64, " "},
        {"missing key", "[A]\r\nx=1\r\n", "A", "y", "DEF", 64, "DEF"},
        {"missing section", "[A]\r\nx=1\r\n", "B", "x", "DEF", 64, "DEF"},
        {"fallback trailing blanks", "[A]\r\nx=1\r\n", "A", "y", "  d  ", 64, "  d"},
        {"truncated", "[A]\r\nx=abcdef\r\n", "A", "x", "DEF", 4, "abc"},
        {"size one", "[A]\r\nx=abcdef\r\n", "A", "x", "DEF", 1, ""},
        {"truncated fallback", "[A]\r\nx=abcdef\r\n", "A", "y", "longdefault", 4, "lon"},
        {"first duplicate key", "[A]\r\nx=1\r\nx=2\r\n[a]\r\nx=3\r\ny=4\r\n", "A", "x", "DEF", 64, "1"},
        {"only the first section", "[A]\r\nx=1\r\nx=2\r\n[a]\r\nx=3\r\ny=4\r\n", "A", "y", "DEF", 64, "DEF"},
        {"no equals", "[A]\r\nx\r\n", "A", "x", "DEF", 64, "DEF"},
        {"empty value", "[A]\r\nx=\r\n", "A", "x", "DEF", 64, ""},
        {"comment", "[A]\r\n;x=1\r\nx=2\r\n", "A", "x", "DEF", 64, "2"},
        {"comment key", "[A]\r\n;x=1\r\nx=2\r\n", "A", ";x", "DEF", 64, "DEF"},
        {"header junk", "[A] junk\r\nx=1\r\n[B\r\ny=2\r\n", "A", "x", "DEF", 64, "1"},
        {"header without bracket", "[A] junk\r\nx=1\r\n[B\r\ny=2\r\n", "B", "y", "DEF", 64, "2"},
        {"equals in value", "[A]\r\nx=a=b\r\n", "A", "x", "DEF", 64, "a=b"},
        {"LF", "[A]\nx=1\n", "A", "x", "DEF", 64, "1"},
        {"CR", "[A]\rx=1\r", "A", "x", "DEF", 64, "1"},
        {"tabs", "[A]\r\n\tx\t=\t1\t\r\n", "A", "x", "DEF", 64, "1"},
        {"high bytes", "[A]\r\nx=\xe9\xbf\r\n", "A", "x", "DEF", 64, "\xe9\xbf"},
        {"BOM hides the first header", "\xef\xbb\xbf[A]\r\nx=1\r\n", "A", "x", "DEF", 64, "DEF"},
        {"key before any section", "x=1\r\n[A]\r\n", "A", "x", "DEF", 64, "DEF"},
};

struct TempIni {
    obl::test::TempPath file{"obl_ini_file_test", ".ini"};
    const std::filesystem::path& path = file.path;
    void Put(const std::string& text) const { std::ofstream(path, std::ios::binary) << text; }
    std::string Get() const
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    ~TempIni()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

} // namespace

OBL_TEST_CASE("ini: writes like WritePrivateProfileString")
{
    for (const WriteCase& c : kWrites) {
        const std::string after = ini::Set(c.before, c.section, c.key, c.value);
        if (after != c.after) ::obl::unit::ReportFailure(__FILE__, __LINE__, std::string("write case: ") + c.name);
    }
}

OBL_TEST_CASE("ini: reads like GetPrivateProfileString")
{
    TempIni file;
    for (const ReadCase& c : kReads) {
        file.Put(c.text);
        const std::string got = ini::ReadString(file.path, c.section, c.key, c.fallback, c.size);
        if (got != c.expected) ::obl::unit::ReportFailure(__FILE__, __LINE__, std::string("read case: ") + c.name + " got " + got);
    }
}

OBL_TEST_CASE("ini: files, missing files and numbers")
{
    TempIni file;
    std::error_code ec;
    std::filesystem::remove(file.path, ec);
    OBL_CHECK(ini::ReadString(file.path, "A", "x", "none", 64) == "none");
    OBL_CHECK_EQ(ini::ReadInt(file.path, "A", "x", 77), 77);
    OBL_CHECK(ini::WriteString(file.path, "Sec", "Key", "v1"));
    OBL_CHECK(file.Get() == "[Sec]\r\nKey=v1\r\n");
    struct {
        const char* value;
        int expected;
    } const numbers[] = {{"12", 12}, {" 34 ", 34}, {"-5", -5}, {"0x10", 16}, {"7abc", 7}, {"abc", 0}, {"", 77}, {"+3", 3}, {"\"9\"", 9},
            {"2147483648", -2147483647 - 1}, {"010", 10}, {"1.9", 1}};
    for (const auto& n : numbers) {
        file.Put(std::string("[A]\r\nx=") + n.value + "\r\n");
        OBL_CHECK_EQ(ini::ReadInt(file.path, "A", "x", 77), n.expected);
        OBL_CHECK_EQ(ini::ReadInt(file.path, "A", "missing", 77), 77);
    }
}

#ifdef _WIN32
OBL_TEST_CASE("ini: the same cases through the Windows profile API")
{
    TempIni file;
    const std::string path = file.path.string();
    for (const WriteCase& c : kWrites) {
        file.Put(c.before);
        WritePrivateProfileStringA(c.section, c.key, c.value, path.c_str());
        if (file.Get() != c.after) ::obl::unit::ReportFailure(__FILE__, __LINE__, std::string("Windows write case: ") + c.name);
    }
    for (const ReadCase& c : kReads) {
        file.Put(c.text);
        std::string buffer(c.size + 1, '\0');
        buffer.resize(GetPrivateProfileStringA(c.section, c.key, c.fallback, buffer.data(), static_cast<DWORD>(c.size), path.c_str()));
        if (buffer != c.expected) ::obl::unit::ReportFailure(__FILE__, __LINE__, std::string("Windows read case: ") + c.name);
    }
}
#endif
