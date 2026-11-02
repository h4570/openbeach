#pragma once

#include "core/mp3info.h"

namespace obl::platform {

// Reads the length of one MP3 file for the playlist (src/core/mp3info.h); counted as
// `call-report ui.mp3-scan`.
void ScanMp3Length(mp3::Mp3Info& info, const char* directory, const char* file);

} // namespace obl::platform
