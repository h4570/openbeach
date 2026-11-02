#pragma once

#include "core/json_schema.h"

#include <nlohmann/json.hpp>

#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {

// What the script may name: the converted sample names and the sprites with their animations, all lowercase. The engine
// resolves the same names against its sample bank and sprite archive (ScriptedSound's Host).
struct SoundNames {
    std::set<std::string> samples;
    std::map<std::string, std::vector<std::string>> sprites;
};

// Reads the script text (the decoded bytes of Sounds.ble) with the engine's grammar (src/game/ui/scripted_sound.cpp, Load) and
// builds the obl.sound_script document in file order. Refused, with the byte offset: an unrecognized command, a token the grammar
// does not accept, a sample or sprite or animation that `names` does not have, a repeated sprite, animation, building or
// opinion entry (the document keeps one of each), and the engine's limits (30 sprites, 20 buildings, 16 opinions, groups 0..19).
bool ParseSoundScript(std::string_view text, const SoundNames& names, nlohmann::ordered_json& document, std::string& error);

// Converts the installation's sound script into `<out>/sounds/sound_script.json`, validated before it is written. Reads
// `Data/Sounds/Sounds.ble`, `Data/Sounds/blsnd.hdr` (sample names) and `Data/Sprites/BLSprite.hdr` (sprites and animations).
bool ConvertSoundScript(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error);

} // namespace obl::convert
