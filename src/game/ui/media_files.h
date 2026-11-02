#pragma once

#include <string>

namespace obl::ui::media_files {

// The windowed game stops with the setup hint when a converted file (media_pack.h) is missing at the moment it is opened. The headless
// run skips the check: it has no audio or video, and the open fails quietly as it does for any unreadable file.
void RequireWindowedFile(const std::string& path);

} // namespace obl::ui::media_files
