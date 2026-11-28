#include "script_program_ids.h"

#include "core/file_io.h"
#include "core/pack_path.h"
#include "formats/object_vocabulary.h"
#include "formats/script_pack.h"
#include "game/objects/script_program_api.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace obl::object {
namespace {

const script_program::Memory kValidationMemory = {
        [](size_t size) { return ::operator new(size); }, [](void* block) { ::operator delete(block); }};

void CopyEntries(const std::vector<object_vocabulary::Entry>& source, std::vector<script_program::NamedId>& out)
{
    out.clear();
    out.reserve(source.size());
    for (const auto& entry : source) out.push_back({entry.id, entry.name});
}

} // namespace

bool ParseScriptIds(std::string_view json, script_program::Vocabulary& out, std::string& error)
{
    out = {};
    std::vector<object_vocabulary::Entry> states;
    std::vector<object_vocabulary::Entry> messages;
    if (!object_vocabulary::ParseBase(json, states, messages, error)) return false;
    CopyEntries(states, out.states);
    CopyEntries(messages, out.messages);
    out.functions = script_program::EngineFunctions();
    out.values = script_program::EngineValues();
    return true;
}

bool LoadMergedVocabulary(const std::filesystem::path& packDirectory, std::span<const std::string> mods, script_program::Vocabulary& out,
        std::string& error, std::vector<std::string>* errors)
{
    out = {};
    std::vector<object_vocabulary::Entry> states;
    std::vector<object_vocabulary::Entry> messages;
    if (!object_vocabulary::LoadMerged(packDirectory, mods, states, messages, error, errors)) return false;
    CopyEntries(states, out.states);
    CopyEntries(messages, out.messages);
    out.functions = script_program::EngineFunctions();
    out.values = script_program::EngineValues();
    return true;
}

bool ValidateModObjectScripts(const std::filesystem::path& packDirectory, std::span<const std::string> mods,
        const script_program::Vocabulary& vocabulary, std::string& error, std::vector<std::string>* errors)
{
    error.clear();
    bool valid = true;
    const auto report = [&] {
        valid = false;
        if (errors) errors->push_back(error);
        return errors != nullptr;
    };
    const std::filesystem::path gameDirectory = pack_path::GameDirectory(packDirectory);
    for (const std::string& mod : mods) {
        const auto modDirectory = pack_path::ModDirectory(gameDirectory, mod);
        if (!modDirectory) continue;
        const std::filesystem::path objectDir = *modDirectory / "scripts" / "object";
        std::error_code ec;
        if (!std::filesystem::is_directory(objectDir, ec)) continue;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(objectDir, ec))
            if (entry.is_regular_file(ec) && entry.path().extension() == ".obs") files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            const std::string relative = file.lexically_relative(*modDirectory).generic_string();
            std::string text;
            std::string readError;
            if (!script_pack::ReadScript(file, script_pack::Kind::Object, text, readError)) {
                error = "mod \"" + mod + "\" (" + relative + ", line 1): " + readError;
                if (!report()) return false;
                continue;
            }
            text.push_back(static_cast<char>(file_io::kTerminator));
            auto program = std::make_unique<script::ScriptProgram>();
            script_program::ResetProgram(*program);
            try {
                script_program::Parse(*program, text.data(), text.size() - 1, vocabulary, kValidationMemory, {true, 2});
            } catch (const script_program::ParseError& failure) {
                script_program::Destroy(*program, kValidationMemory);
                error = "mod \"" + mod + "\" (" + relative + "): " + failure.what();
                if (!report()) return false;
                continue;
            }
            script_program::Destroy(*program, kValidationMemory);
        }
    }
    return valid;
}

} // namespace obl::object
