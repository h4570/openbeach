#include "unit_test.h"
#include "importer/src/writers/script_convert.h"
#include "importer/src/source/source.h"
#include "temp_dir.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// The installed form of text: every byte XOR 0xFF.
std::vector<uint8_t> Installed(const std::string& plain)
{
    std::vector<uint8_t> bytes;
    for (char c : plain) bytes.push_back(static_cast<uint8_t>(c) ^ 0xFF);
    return bytes;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

OBL_TEST_CASE("convert.script_decode_ascii_only")
{
    // The text is the installed bytes XOR 0xFF, CRLF kept; any byte outside ASCII and empty input are refused.
    std::string text, error;
    OBL_REQUIRE(DecodeScript(Installed("// a\r\nState X\r\n"), text, error));
    OBL_CHECK(text == "// a\r\nState X\r\n");
    OBL_CHECK(!DecodeScript({}, text, error));
    OBL_CHECK(!DecodeScript(Installed(std::string("A\xB9", 2)), text, error));
    // A UTF-16 byte-order mark (FF FE) is not ASCII: refused, the encode-back check would not hold for it.
    OBL_CHECK(!DecodeScript(Installed(std::string("\xFF\xFE"
                                                  "A\0",
                                    4)),
            text, error));
}

OBL_TEST_CASE("convert.script_vocabulary_refuses_a_missing_or_broken_exe")
{
    const obl::test::TempPath tmp_root("obl-scripts-other-exe");
    const auto& root = tmp_root.path;
    const auto text = Installed("// x\r\n");
    const auto write = [&](const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    };
    write(root / "Data" / "Script" / "Object" / "guest.ble", text);
    write(root / "Data" / "Scenario" / "Scenario1" / "a.ble", text);
    int files = 0;
    std::string error;
    OBL_CHECK(!ConvertScripts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), files, error));
    OBL_CHECK(error.find("BeachLife.exe") != std::string::npos);
    write(root / "BeachLife.exe", Installed("MZ not a PE file"));
    OBL_CHECK(!ConvertScripts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), files, error));
    OBL_CHECK(!error.empty());
}

OBL_TEST_CASE("convert.script_refuses_bad_scenario_folders")
{
    // A scenario folder without an id rule, or with two .ble files, stops the conversion; nothing is guessed.
    const obl::test::TempPath tmp_root("obl-scripts-bad-folders");
    const auto& root = tmp_root.path;
    const auto text = Installed("// x\r\n");
    const auto write = [&](const std::filesystem::path& file) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(text.data()), std::streamsize(text.size()));
    };
    write(root / "Data" / "Script" / "Object" / "guest.ble");
    write(root / "Data" / "Scenario" / "Other" / "other.ble");
    int files = 0;
    std::string error;
    OBL_CHECK(!ConvertScripts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), files, error));
    OBL_CHECK(error.find("Other") != std::string::npos);

    std::filesystem::remove_all(root / "Data" / "Scenario" / "Other");
    write(root / "Data" / "Scenario" / "Scenario1" / "a.ble");
    write(root / "Data" / "Scenario" / "Scenario1" / "b.ble");
    OBL_CHECK(!ConvertScripts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), files, error));
    OBL_CHECK(error.find("expected one .ble script, found 2") != std::string::npos);
}

OBL_TEST_CASE("data.convert_scripts_of_installation")
{
    // Every installed object script and every scenario's world script: the decoded text has the header line, and encoding
    // the text back gives the installed file exactly. Scenario folders map to ids as maps-scenarios.md says.
    const auto objectDir = FindInstalledFile(OBL_GAME_DIR, "Data/Script/Object");
    const auto scenarioDir = FindInstalledFile(OBL_GAME_DIR, "Data/Scenario");
    if (!objectDir || !scenarioDir) OBL_SKIP("no installation scripts");
    const obl::test::TempPath tmp_out("obl-scripts-installation");
    const auto& out = tmp_out.path;
    int files = 0;
    std::string error;
    OBL_REQUIRE(ConvertScripts(*obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(out), files, error));

    // The written file is the header line plus the text; the text XOR 0xFF is the installed file.
    const auto check = [](const std::filesystem::path& installed, const std::filesystem::path& written, const std::string& header) {
        const auto text = ReadFile(written);
        OBL_REQUIRE(text.size() > header.size());
        OBL_CHECK(std::string(text.begin(), text.begin() + std::ptrdiff_t(header.size())) == header);
        std::vector<uint8_t> encoded;
        for (size_t i = header.size(); i < text.size(); ++i) encoded.push_back(text[i] ^ 0xFF);
        OBL_CHECK(encoded == ReadFile(installed));
    };

    int expected = 0;
    for (const auto& entry : std::filesystem::directory_iterator(*objectDir)) {
        if (!entry.is_regular_file() || Lower(entry.path().extension().string()) != ".ble") continue;
        ++expected;
        const auto written = out / "scripts" / "object" / (Lower(entry.path().stem().string()) + ".obs");
        check(entry.path(), written, "#obl-script 1 object\n");
    }
    OBL_CHECK(expected == 8);

    for (const auto& entry : std::filesystem::directory_iterator(*scenarioDir)) {
        if (!entry.is_directory()) continue;
        const std::string folder = Lower(entry.path().filename().string());
        std::string id;
        if (folder.starts_with("sandscenario"))
            id = "sandbox" + folder.substr(12);
        else
            id = "s" + folder.substr(8);
        std::filesystem::path script;
        for (const auto& file : std::filesystem::directory_iterator(entry.path()))
            if (Lower(file.path().extension().string()) == ".ble") script = file.path();
        OBL_REQUIRE(!script.empty());
        ++expected;
        check(script, out / "scenarios" / id / "world.wsc", "#obl-script 1 world\n");
    }
    OBL_CHECK(expected == 32);
    OBL_CHECK(files == expected);
}
