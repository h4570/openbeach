#include "game/mods/ed25519.h"
#include "game/mods/mod_catalog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;

std::string Trim(std::string_view s)
{
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) ++start;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) --end;
    return std::string(s.substr(start, end - start));
}

std::string ToLower(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

template<size_t N> bool ParseHex(std::string_view text, std::array<uint8_t, N>& out)
{
    if (text.size() != N * 2) return false;
    for (size_t i = 0; i < N; ++i) {
        unsigned value = 0;
        const auto res = std::from_chars(text.data() + i * 2, text.data() + i * 2 + 2, value, 16);
        if (res.ec != std::errc{} || res.ptr != text.data() + i * 2 + 2) return false;
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

std::string BytesToHex(std::span<const uint8_t> bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0f]);
    }
    return out;
}

bool ReadTextFile(const fs::path& path, std::string& out, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open file for reading: " + path.string();
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = "failed reading file: " + path.string();
        return false;
    }
    return true;
}

bool WriteTextFile(const fs::path& path, std::string_view text, std::string& error)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot open file for writing: " + path.string();
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        error = "failed writing file: " + path.string();
        return false;
    }
    return true;
}

bool LoadSeed(
        const std::optional<std::string>& keyEnv, const std::optional<fs::path>& keyFile, obl::ed25519::Seed& seed, std::string& error)
{
    if (keyEnv && keyFile) {
        error = "specify only one of --key-env or --key-file";
        return false;
    }
    std::string raw;
    if (keyFile) {
        if (!ReadTextFile(*keyFile, raw, error)) return false;
    } else {
        const std::string envName = keyEnv.value_or("OBL_CATALOG_PRIVATE_KEY");
        const char* value = std::getenv(envName.c_str());
        if (!value || !*value) {
            error = "private key environment variable is not set: " + envName;
            return false;
        }
        raw = value;
    }
    const std::string hex = ToLower(Trim(raw));
    if (!ParseHex<obl::ed25519::kSeedSize>(hex, seed)) {
        error = "private key must be 64 hexadecimal characters (32-byte Ed25519 seed)";
        return false;
    }
    return true;
}

void PrintUsage()
{
    std::cerr << "Usage:\n"
              << "  obl_catalog_sign sign --input <unsigned.json> --output <catalog.json> [--key-env <VAR> | --key-file <path>]\n"
              << "  obl_catalog_sign verify --input <catalog.json> [--url <catalog-url>] [--allow-local] [--public-key <64-hex>]\n"
              << "  obl_catalog_sign pubkey [--key-env <VAR> | --key-file <path>]\n";
}

int RunSign(int argc, char** argv)
{
    std::optional<fs::path> input;
    std::optional<fs::path> output;
    std::optional<std::string> keyEnv;
    std::optional<fs::path> keyFile;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            input = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--key-env" && i + 1 < argc) {
            keyEnv = argv[++i];
        } else if (arg == "--key-file" && i + 1 < argc) {
            keyFile = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }
    if (!input || !output) {
        std::cerr << "sign requires --input and --output\n";
        PrintUsage();
        return 2;
    }

    obl::ed25519::Seed seed{};
    std::string error;
    if (!LoadSeed(keyEnv, keyFile, seed, error)) {
        std::cerr << "obl_catalog_sign: " << error << "\n";
        return 1;
    }
    std::string unsignedText;
    if (!ReadTextFile(*input, unsignedText, error)) {
        std::cerr << "obl_catalog_sign: " << error << "\n";
        return 1;
    }
    std::string signedText;
    if (!obl::mods::SignCatalogJson(unsignedText, seed, signedText, error)) {
        std::cerr << "obl_catalog_sign: " << error << "\n";
        return 1;
    }
    signedText.push_back('\n');
    if (!WriteTextFile(*output, signedText, error)) {
        std::cerr << "obl_catalog_sign: " << error << "\n";
        return 1;
    }
    return 0;
}

int RunVerify(int argc, char** argv)
{
    std::optional<fs::path> input;
    std::string url = "https://openbeach.app/mods/catalog.json";
    bool allowLocal = false;
    std::optional<std::string> expectedPublicKey;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            input = argv[++i];
        } else if (arg == "--url" && i + 1 < argc) {
            url = argv[++i];
        } else if (arg == "--allow-local") {
            allowLocal = true;
        } else if (arg == "--public-key" && i + 1 < argc) {
            expectedPublicKey = ToLower(Trim(argv[++i]));
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }
    if (!input) {
        std::cerr << "verify requires --input\n";
        PrintUsage();
        return 2;
    }

    std::string text;
    std::string error;
    if (!ReadTextFile(*input, text, error)) {
        std::cerr << "obl_catalog_sign: " << error << "\n";
        return 1;
    }
    obl::mods::Catalog catalog;
    if (!obl::mods::ParseCatalog(text, url, allowLocal, catalog, error)) {
        std::cerr << "obl_catalog_sign: " << error << "\n";
        return 1;
    }
    if (expectedPublicKey && catalog.publicKeyHex != *expectedPublicKey) {
        std::cerr << "obl_catalog_sign: public key mismatch (got " << catalog.publicKeyHex << ", expected " << *expectedPublicKey << ")\n";
        return 1;
    }
    return 0;
}

int RunPubkey(int argc, char** argv)
{
    std::optional<std::string> keyEnv;
    std::optional<fs::path> keyFile;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--key-env" && i + 1 < argc) {
            keyEnv = argv[++i];
        } else if (arg == "--key-file" && i + 1 < argc) {
            keyFile = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }
    obl::ed25519::Seed seed{};
    std::string error;
    if (!LoadSeed(keyEnv, keyFile, seed, error)) {
        std::cerr << "obl_catalog_sign: " << error << "\n";
        return 1;
    }
    const auto pk = obl::ed25519::PublicKeyFromSeed(seed);
    std::cout << BytesToHex(pk) << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        PrintUsage();
        return 2;
    }
    const std::string_view cmd = argv[1];
    if (cmd == "sign") return RunSign(argc, argv);
    if (cmd == "verify") return RunVerify(argc, argv);
    if (cmd == "pubkey") return RunPubkey(argc, argv);
    std::cerr << "Unknown command: " << cmd << "\n";
    PrintUsage();
    return 2;
}
