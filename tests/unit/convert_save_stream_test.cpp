#include "importer/src/readers/save/save_stream.h"
#include "unit_test.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
using obl::convert::SaveRecord;
using obl::convert::SaveStream;
using obl::convert::SaveTag;

struct SaveBytes {
    std::vector<uint8_t> data;
    void Bits(uint32_t bits, size_t width = 4)
    {
        for (size_t i = 0; i < width; ++i) data.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    }
    void Scalar(SaveTag tag, uint32_t bits, size_t width = 4)
    {
        data.push_back(static_cast<uint8_t>(tag));
        Bits(bits, width);
    }
    void Text(SaveTag tag, std::string_view text)
    {
        Scalar(tag, static_cast<uint32_t>(text.size()));
        data.insert(data.end(), text.begin(), text.end());
    }
    void Header(bool prefix = false)
    {
        Text(SaveTag::Class, "Binary Serial Stream");
        Text(SaveTag::Class, prefix ? "Class name : BeachLife Save" : "BeachLife Save");
        Scalar(SaveTag::Float, 0x3fcccccd);
        Scalar(SaveTag::Int, 0xf1234567);
        Scalar(SaveTag::Int, 2);
        Scalar(SaveTag::Int, 7);
        Scalar(SaveTag::Float, 0x80000000);
        Scalar(SaveTag::Float, 0x7fc01234);
        Scalar(SaveTag::Int, 3);
        Text(SaveTag::String, "Our island");
        Scalar(SaveTag::Bool, 1, 1);
        Text(SaveTag::Class, prefix ? "Class name : World" : "World");
    }
};
} // namespace

OBL_TEST_CASE("convert.save_stream_all_record_types")
{
    SaveBytes bytes;
    bytes.Text(SaveTag::Class, "Class name : Synthetic");
    bytes.Text(SaveTag::String, std::string("a\0\x9f", 3));
    bytes.Scalar(SaveTag::Bool, 0x0d, 1); // real saves hold uninitialized bool bytes
    bytes.Scalar(SaveTag::Float, 0x7fa12345);
    bytes.Scalar(SaveTag::Int, 0xfffffffe);
    bytes.Scalar(SaveTag::Short, 0xfffd, 2);
    bytes.Scalar(SaveTag::UInt, 0xffffffff);
    bytes.Scalar(SaveTag::IntArray, 2);
    bytes.Bits(1);
    bytes.Bits(0x80000000);
    bytes.Scalar(SaveTag::WideString, 2);
    bytes.Bits(0x008c, 2);
    bytes.Bits(0xd800, 2); // retain even an unpaired surrogate until text conversion
    bytes.Scalar(SaveTag::Byte, 255, 1);
    bytes.Scalar(SaveTag::ByteArray, 0);
    bytes.Scalar(SaveTag::FloatArray, 4);
    for (uint32_t bits : {0x80000000u, 0x7fc12345u, 0x7f800000u, 1u}) bytes.Bits(bits);
    bytes.Scalar(SaveTag::Dword, 0xfedcba98);
    bytes.Scalar(SaveTag::Word, 0xabcd, 2);
    SaveStream stream(bytes.data);
    SaveRecord record;
    std::string error;
    for (uint8_t tag = 0; tag <= 13; ++tag) {
        OBL_REQUIRE(stream.Next(record, error));
        OBL_CHECK_EQ(static_cast<uint8_t>(record.tag), tag);
        OBL_CHECK(error.empty());
        if (record.tag == SaveTag::Class) OBL_CHECK_EQ(record.ClassName(), "Synthetic");
        if (record.tag == SaveTag::String) OBL_CHECK_EQ(record.Text().size(), 3u);
        if (record.tag == SaveTag::Bool) OBL_CHECK_EQ(record.Bits(), 0x0du);
        if (record.tag == SaveTag::Float) OBL_CHECK_EQ(record.Bits(), 0x7fa12345u);
        if (record.tag == SaveTag::Int) OBL_CHECK_EQ(record.Bits(), 0xfffffffeu);
        if (record.tag == SaveTag::Short) OBL_CHECK_EQ(record.Bits(), 0xfffdu);
        if (record.tag == SaveTag::IntArray) OBL_CHECK_EQ(record.Bits(1), 0x80000000u);
        if (record.tag == SaveTag::WideString) OBL_CHECK_EQ(record.Bits(1), 0xd800u);
        if (record.tag == SaveTag::FloatArray) {
            OBL_CHECK_EQ(record.Bits(0), 0x80000000u);
            OBL_CHECK_EQ(record.Bits(1), 0x7fc12345u);
            OBL_CHECK_EQ(record.Bits(2), 0x7f800000u);
            OBL_CHECK_EQ(record.Bits(3), 1u);
        }
        if (record.tag == SaveTag::Dword) OBL_CHECK_EQ(record.Bits(), 0xfedcba98u);
        if (record.tag == SaveTag::Word) OBL_CHECK_EQ(record.Bits(), 0xabcdu);
    }
    OBL_CHECK(!stream.Next(record, error));
    OBL_CHECK(error.empty());
    OBL_CHECK_EQ(stream.Offset(), bytes.data.size());
}

OBL_TEST_CASE("convert.save_stream_rejects_malformed_records")
{
    for (uint8_t tag = 0; tag <= 13; ++tag) {
        SaveBytes bytes;
        const auto kind = static_cast<SaveTag>(tag);
        const bool counted = tag == 0 || tag == 1 || tag == 7 || tag == 8 || tag == 10 || tag == 11;
        const size_t width = tag == 5 || tag == 8 || tag == 13 ? 2 : tag <= 2 || tag == 9 || tag == 10 ? 1 : 4;
        bytes.data.push_back(tag);
        if (counted) bytes.Bits(1);
        bytes.Bits(1, width);
        for (size_t size = 1; size < bytes.data.size(); ++size) {
            SaveStream stream(std::span(bytes.data).first(size));
            SaveRecord record;
            std::string error;
            OBL_CHECK(!stream.Next(record, error));
            OBL_CHECK(!error.empty());
            OBL_CHECK_EQ(stream.Offset(), 0u);
        }
        if (counted) {
            for (uint32_t count : {0x7fffffffu, 0x80000000u, 0xffffffffu}) {
                SaveBytes huge;
                huge.Scalar(kind, count);
                SaveStream stream(huge.data);
                SaveRecord record;
                std::string error;
                OBL_CHECK(!stream.Next(record, error));
                OBL_CHECK(!error.empty());
            }
        }
    }
    for (auto bad : {std::vector<uint8_t>{14}, std::vector<uint8_t>{255}}) {
        SaveStream stream(bad);
        SaveRecord record;
        std::string error;
        OBL_CHECK(!stream.Next(record, error));
        OBL_CHECK(!error.empty());
    }
}

OBL_TEST_CASE("convert.save_header_preserves_bits_and_requires_complete_header")
{
    for (bool prefix : {false, true}) {
        SaveBytes bytes;
        bytes.Header(prefix);
        SaveStream stream(bytes.data);
        obl::convert::OldSaveHeader header;
        std::string error;
        OBL_REQUIRE(ReadOldSaveHeader(stream, header, error));
        OBL_CHECK_EQ(header.savedAt, 0xf1234567u);
        OBL_CHECK_EQ(header.scenario, 2);
        OBL_CHECK_EQ(header.day, 7);
        OBL_CHECK_EQ(header.dayTimeBits, 0x80000000u);
        OBL_CHECK_EQ(header.cashBits, 0x7fc01234u);
        OBL_CHECK_EQ(header.guests, 3);
        OBL_CHECK_EQ(header.title.Text(), "Our island");
        OBL_CHECK(header.sandbox);
        OBL_CHECK_EQ(stream.Offset(), bytes.data.size());
        for (size_t size = 0; size < bytes.data.size(); ++size) {
            SaveStream truncated(std::span(bytes.data).first(size));
            OBL_CHECK(!ReadOldSaveHeader(truncated, header, error));
            OBL_CHECK(!error.empty());
            OBL_CHECK(header.title.bytes.empty());
        }
        // Each header tag is checked, including signatures and the first world marker.
        SaveStream records(bytes.data);
        SaveRecord record;
        while (records.Next(record, error)) {
            auto bad = bytes.data;
            bad[record.offset] = static_cast<uint8_t>(record.tag == SaveTag::Bool ? SaveTag::Byte : SaveTag::Bool);
            SaveStream wrong(bad);
            OBL_CHECK(!ReadOldSaveHeader(wrong, header, error));
        }
    }
}

OBL_TEST_CASE("convert.save_header_rejects_wrong_signatures_and_versions")
{
    SaveBytes bytes;
    bytes.Header();
    SaveStream records(bytes.data);
    SaveRecord record;
    std::string error;
    while (records.Next(record, error)) {
        if (record.tag != SaveTag::Class && record.tag != SaveTag::Float) continue;
        // Version is the first float. Cash and day time deliberately allow arbitrary bits.
        if (record.tag == SaveTag::Float && record.Bits() != 0x3fcccccd) continue;
        auto bad = bytes.data;
        const size_t payload = static_cast<size_t>(record.bytes.data() - bytes.data.data());
        bad[payload] ^= 1;
        SaveStream stream(bad);
        obl::convert::OldSaveHeader header;
        OBL_CHECK(!ReadOldSaveHeader(stream, header, error));
        OBL_CHECK(!error.empty());
    }
}

OBL_TEST_CASE("convert.save_info_reports_header_and_record_counts")
{
    SaveBytes bytes;
    bytes.Header();
    bytes.Scalar(SaveTag::Int, 5);
    bytes.Scalar(SaveTag::Bool, 0x0d, 1);
    std::string report;
    std::string error;
    OBL_REQUIRE(obl::convert::DescribeOldSave(bytes.data, report, error));
    OBL_CHECK(error.empty());
    OBL_CHECK(report.find("saved_at  4045620583\n") != std::string::npos);
    OBL_CHECK(report.find("scenario  2\n") != std::string::npos);
    OBL_CHECK(report.find("day_time  -0 (0x80000000)\n") != std::string::npos);
    OBL_CHECK(report.find("(0x7fc01234)\n") != std::string::npos); // NaN spelling differs by C runtime; the bits do not
    OBL_CHECK(report.find("title     \"Our island\"\n") != std::string::npos);
    OBL_CHECK(report.find("sandbox   yes\n") != std::string::npos);
    OBL_CHECK(report.find("3 body records") != std::string::npos);
    OBL_CHECK(report.find("  class    1\n") != std::string::npos);
    OBL_CHECK(report.find("  bool     1\n") != std::string::npos);

    // Non-ASCII title bytes and the escape character print as hex, never as guessed text.
    SaveBytes title;
    title.Text(SaveTag::Class, "Binary Serial Stream");
    title.Text(SaveTag::Class, "BeachLife Save");
    title.Scalar(SaveTag::Float, 0x3fcccccd);
    for (int i = 0; i < 6; ++i) title.Scalar(i == 3 || i == 4 ? SaveTag::Float : SaveTag::Int, 0);
    title.Text(SaveTag::String, std::string("Pla\xbf\\\0", 6));
    title.Scalar(SaveTag::Bool, 0, 1);
    title.Text(SaveTag::Class, "World");
    OBL_REQUIRE(obl::convert::DescribeOldSave(title.data, report, error));
    OBL_CHECK(report.find("title     \"Pla\\xbf\\x5c\\x00\"\n") != std::string::npos);

    // A malformed record after a valid header fails the whole report.
    bytes.data.push_back(14);
    OBL_CHECK(!obl::convert::DescribeOldSave(bytes.data, report, error));
    OBL_CHECK(error.find("unrecognized record tag 14") != std::string::npos);
    OBL_CHECK(report.empty());
}

OBL_TEST_CASE("convert.save_file_input_is_bounded")
{
    const obl::test::TempPath tmp("obl_convert_save_input");
    const auto& dir = tmp.path;
    std::filesystem::create_directories(dir);
    const auto path = (dir / "save1.dat").string();
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "0123456789";
    }
    std::vector<uint8_t> bytes;
    std::string error;
    OBL_CHECK(obl::convert::ReadBoundedFile(path, 10, bytes, error));
    OBL_CHECK_EQ(bytes.size(), 10u);
    OBL_CHECK_EQ(bytes[9], uint8_t('9'));
    OBL_CHECK(!obl::convert::ReadBoundedFile(path, 9, bytes, error));
    OBL_CHECK(error.find("larger than 9 bytes") != std::string::npos);
    OBL_CHECK(bytes.empty());
    OBL_CHECK(!obl::convert::ReadBoundedFile((dir / "missing.dat").string(), 10, bytes, error));
    OBL_CHECK(error.find("cannot read") != std::string::npos);
}

OBL_TEST_CASE("data.convert_reads_local_save_streams")
{
    const std::filesystem::path dir = std::filesystem::path(OBL_GAME_DIR) / "SaveGames";
    if (!std::filesystem::is_directory(dir)) OBL_SKIP("no local saves");
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".dat") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        SaveStream stream(bytes);
        obl::convert::OldSaveHeader header;
        std::string error;
        const bool ok = ReadOldSaveHeader(stream, header, error);
        if (!ok) obl::unit::ReportFailure(__FILE__, __LINE__, entry.path().string() + ": " + error);
        OBL_REQUIRE(ok);
        SaveRecord record;
        size_t records = 0;
        while (stream.Next(record, error)) ++records;
        OBL_CHECK(error.empty());
        OBL_CHECK(records > 0);
        OBL_CHECK_EQ(stream.Offset(), bytes.size());
        std::string report;
        OBL_CHECK(obl::convert::DescribeOldSave(bytes, report, error));
        ++count;
    }
    if (!count) OBL_SKIP("no local .dat saves");
}
