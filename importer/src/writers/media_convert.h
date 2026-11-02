#pragma once

#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <string>

namespace obl::convert {

// Copies every `Data/Soundtrack/*.mp3` of the installation to `<out>/music/<name>.mp3` (the name lowercased, a content key)
// and the intro movie `Data/Movies/EidosMovie.mpg` (the file the engine plays) to `<out>/video/intro.mpg`. Each copy is
// checked by its magic bytes first: an MP3 starts with an ID3 tag or a frame sync, an MPEG-1 program stream with a pack
// header. A missing directory, a name that is not a content key, or a file of another kind is an error.
bool ConvertMedia(const ByteSource& source, PackSink& out, int& files, std::string& error);

} // namespace obl::convert
