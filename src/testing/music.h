#pragma once
#include <string>
#include <vector>

namespace obl::test {
bool ExecuteMusicCommand(const std::string& name, const std::vector<std::string>& args);
bool ReadMusicProbe(const std::string& name, double& out);
// Live MP3 panel fixture driver for ui.music-panel-drive (requires gameplay).
void DriveMusicPanel(int count);
} // namespace obl::test
