// The browser import's WebAssembly entry (docs/architecture/data-formats/converter/index.md, "Browser adapter"),
// run inside a Web Worker by import_worker.js. All calls are synchronous: the worker reads the player's files with
// FileReaderSync and keeps written files as Blobs, so the converters need no asynchronous file system. JavaScript
// provides `Module.oblImport` with read/put/get/delete/keys/progress and the spool calls (import_worker.js documents the
// contract) and calls
//   obl_import(request JSON) -> 0 on success, then obl_import_language() for the pack's language, or obl_import_error()
//   for the message of a failure.
// Request: {"source": "files" | "iso", "files": [{"path", "size"}] | "isoSize": n, "root": "packs/base", "language"?,
// "preferredLanguage"?, "stage"?, "shard"?}. `stage` splits one import between instances (shard.js):
//   absent    the whole import by this instance
//   "prepare" a CD's sprite archive is extracted into the spool and kept there; nothing is converted
//   "shard"   with "shard": {"index", "count"}: converts that share of the maps' and sprites' units and hands each to
//             Module.oblImport.unitPut/unitEnd; writes nothing to the pack
//   "units"   the whole import, taking the maps' and sprites' units from Module.oblImport.unitOpen/... (the shards' output)

#include "importer/web/storage_sink.h"
#include "importer/web/web_source.h"

#include "formats/schemas.h"
#include "openbeach/importer/import.h"

#include "core/json_schema.h"

#include <emscripten/emscripten.h>
#include <nlohmann/json.hpp>

#include <cstdlib>

// clang-format off
EM_JS(int, ReadPickedJs, (int index, double offset, uint8_t* out, size_t length), {
    return Module.oblImport.read(index, offset, HEAPU8.subarray(out, out + length)) ? 1 : 0;
});
EM_JS(int, PutJs, (const char* key, const uint8_t* bytes, size_t length), {
    return Module.oblImport.put(UTF8ToString(key), HEAPU8.slice(bytes, bytes + length)) ? 1 : 0;
});
EM_JS(uint8_t*, GetJs, (const char* key, size_t* length), {
    const bytes = Module.oblImport.get(UTF8ToString(key));
    if (!bytes) return 0;
    const out = _malloc(Math.max(bytes.length, 1));
    HEAPU8.set(bytes, out);
    HEAPU32[length >> 2] = bytes.length;
    return out;
});
EM_JS(int, DeleteJs, (const char* key), {
    return Module.oblImport.delete(UTF8ToString(key)) ? 1 : 0;
});
EM_JS(char*, KeysJs, (const char* prefix), {
    return stringToNewUTF8(Module.oblImport.keys(UTF8ToString(prefix)).join("\n"));
});
EM_JS(int, SpoolCreateJs, (const char* name), {
    return Module.oblImport.spoolCreate(UTF8ToString(name)) ? 1 : 0;
});
EM_JS(int, SpoolAppendJs, (const char* name, const uint8_t* bytes, size_t length), {
    return Module.oblImport.spoolAppend(UTF8ToString(name), HEAPU8.slice(bytes, bytes + length)) ? 1 : 0;
});
EM_JS(int, SpoolReadJs, (const char* name, double offset, uint8_t* out, size_t length), {
    return Module.oblImport.spoolRead(UTF8ToString(name), offset, HEAPU8.subarray(out, out + length)) ? 1 : 0;
});
EM_JS(void, SpoolRemoveJs, (const char* name), {
    Module.oblImport.spoolRemove(UTF8ToString(name));
});
EM_JS(int, SpoolHasJs, (const char* name), {
    return Module.oblImport.spoolHas(UTF8ToString(name)) ? 1 : 0;
});
EM_JS(void, UnitPutJs, (const char* step, size_t unit, const char* path, const uint8_t* bytes, size_t length), {
    Module.oblImport.unitPut(UTF8ToString(step), unit, UTF8ToString(path), HEAPU8.slice(bytes, bytes + length));
});
EM_JS(void, UnitEndJs, (const char* step, size_t unit, const char* error), {
    Module.oblImport.unitEnd(UTF8ToString(step), unit, UTF8ToString(error));
});
EM_JS(int, UnitOpenJs, (const char* step, size_t unit), {
    return Module.oblImport.unitOpen(UTF8ToString(step), unit);
});
EM_JS(char*, UnitPathJs, (int index), {
    return stringToNewUTF8(Module.oblImport.unitPath(index));
});
EM_JS(uint8_t*, UnitBytesJs, (int index, size_t* length), {
    const bytes = Module.oblImport.unitBytes(index);
    const out = _malloc(Math.max(bytes.length, 1));
    HEAPU8.set(bytes, out);
    HEAPU32[length >> 2] = bytes.length;
    return out;
});
EM_JS(char*, UnitErrorJs, (), {
    return stringToNewUTF8(Module.oblImport.unitError());
});
EM_JS(void, ProgressJs, (const char* step), {
    Module.oblImport.progress(UTF8ToString(step));
});
EM_JS(void, CountJs, (const char* step, double done, double total), {
    Module.oblImport.count(UTF8ToString(step), done, total);
});
// clang-format on

namespace {

using namespace obl::convert;

// The image is read as file -1 of the same callback.
constexpr int kIsoIndex = -1;

class JsStorage final : public web::PackStorage {
public:
    bool Put(std::string_view key, std::span<const uint8_t> bytes) override
    {
        return PutJs(std::string(key).c_str(), bytes.data(), bytes.size()) != 0;
    }

    std::optional<std::vector<uint8_t>> Get(std::string_view key) const override
    {
        size_t length = 0;
        uint8_t* bytes = GetJs(std::string(key).c_str(), &length);
        if (!bytes) return std::nullopt;
        std::vector<uint8_t> out(bytes, bytes + length);
        std::free(bytes);
        return out;
    }

    bool Delete(std::string_view key) override { return DeleteJs(std::string(key).c_str()) != 0; }

    std::vector<std::string> Keys(std::string_view prefix) const override
    {
        char* joined = KeysJs(std::string(prefix).c_str());
        std::vector<std::string> keys;
        for (std::string_view rest = joined; !rest.empty();) {
            const size_t newline = std::min(rest.find('\n'), rest.size());
            keys.emplace_back(rest.substr(0, newline));
            rest.remove_prefix(std::min(newline + 1, rest.size()));
        }
        std::free(joined);
        return keys;
    }
};

// A CD's files too large for the heap (the 467 MB sprite archive) live in JavaScript, as Blobs the browser may page out.
class JsSpool final : public Spool {
public:
    bool Create(std::string_view name) override { return SpoolCreateJs(std::string(name).c_str()) != 0; }

    bool Append(std::string_view name, std::span<const uint8_t> bytes) override
    {
        return SpoolAppendJs(std::string(name).c_str(), bytes.data(), bytes.size()) != 0;
    }

    bool Read(std::string_view name, uint64_t offset, std::span<uint8_t> out) override
    {
        return SpoolReadJs(std::string(name).c_str(), static_cast<double>(offset), out.data(), out.size()) != 0;
    }

    void Remove(std::string_view name) override { SpoolRemoveJs(std::string(name).c_str()); }

    bool Has(std::string_view name) const override { return SpoolHasJs(std::string(name).c_str()) != 0; }
};

// The units of the maps and sprites another instance converted, taken from JavaScript's store.
bool TakeUnit(std::string_view step, size_t unit, UnitOutput& output)
{
    const int files = UnitOpenJs(std::string(step).c_str(), unit);
    if (files < 0) return false;
    for (int i = 0; i < files; ++i) {
        char* path = UnitPathJs(i);
        size_t length = 0;
        uint8_t* bytes = UnitBytesJs(i, &length);
        output.files.emplace_back(path, std::vector<uint8_t>(bytes, bytes + length));
        std::free(path);
        std::free(bytes);
    }
    char* error = UnitErrorJs();
    output.error = error;
    std::free(error);
    return true;
}

void GiveUnit(std::string_view step, size_t unit, UnitOutput& output)
{
    for (const auto& [path, bytes] : output.files) UnitPutJs(std::string(step).c_str(), unit, path.c_str(), bytes.data(), bytes.size());
    UnitEndJs(std::string(step).c_str(), unit, output.error.c_str());
}

std::string g_error;
std::string g_language;

bool Run(const char* requestText)
{
    const auto request = nlohmann::json::parse(requestText);
    std::unique_ptr<ByteSource> source;
    std::string error;
    if (request.at("source") == "iso") {
        source = web::OpenIsoSource(
                request.at("isoSize").get<uint64_t>(),
                [](uint64_t offset, std::span<uint8_t> out) {
                    return ReadPickedJs(kIsoIndex, static_cast<double>(offset), out.data(), out.size()) != 0;
                },
                error);
    } else {
        std::vector<web::PickedFile> files;
        for (const auto& file : request.at("files")) files.push_back({file.at("path").get<std::string>(), file.at("size").get<uint64_t>()});
        source = web::OpenPickedFilesSource(
                files,
                [](size_t index, uint64_t offset, std::span<uint8_t> out) {
                    return ReadPickedJs(static_cast<int>(index), static_cast<double>(offset), out.data(), out.size()) != 0;
                },
                error);
    }
    obl::json_schema::Registry schemas;
    if (!source || !obl::formats::LoadEmbeddedSchemas(schemas, error)) {
        g_error = error;
        return false;
    }
    JsStorage storage;
    const auto pack = web::OpenStorageSink(storage, request.value("root", std::string("packs/base")));
    ImportOptions options;
    if (request.contains("language")) options.language = request.at("language").get<std::string>();
    if (request.contains("preferredLanguage")) options.preferredLanguage = request.at("preferredLanguage").get<std::string>();
    options.chosenLanguage = &g_language;
    options.count = [](std::string_view step, uint64_t done, uint64_t total) {
        CountJs(std::string(step).c_str(), static_cast<double>(done), static_cast<double>(total));
    };
    JsSpool spool;
    options.spool = &spool;
    const std::string stage = request.value("stage", std::string());
    Shard shard;
    if (stage == "prepare") {
        options.prepareSpool = true;
    } else if (stage == "shard") {
        shard = {request.at("shard").at("index").get<int>(), request.at("shard").at("count").get<int>()};
        if (shard.count < 1 || shard.index < 0 || shard.index >= shard.count) {
            g_error = "bad import request: shard out of range";
            return false;
        }
        options.exchange.shard = &shard;
        options.exchange.sink = GiveUnit;
    } else if (stage == "units") {
        options.exchange.source = TakeUnit;
    } else if (!stage.empty()) {
        g_error = "bad import request: unknown stage " + stage;
        return false;
    }
    options.progress = [](std::string_view step) { ProgressJs(std::string(step).c_str()); };
    if (!ImportPack(*source, *pack, schemas, options, error)) {
        g_error = error;
        return false;
    }
    return true;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int obl_import(const char* request)
{
    g_error.clear();
    g_language.clear();
    try {
        return Run(request) ? 0 : 1;
    } catch (const std::exception& e) {
        g_error = std::string("bad import request: ") + e.what();
        return 1;
    }
}

EMSCRIPTEN_KEEPALIVE const char* obl_import_error()
{
    return g_error.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* obl_import_language()
{
    return g_language.c_str();
}

} // extern "C"
