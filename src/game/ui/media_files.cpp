#include "game/ui/media_files.h"

#include "game/game/game_accessors.h"
#include "core/config.h"
#include "game/ui/media_pack.h"
#include "game/game/diagnostics.h"

#include <cstdlib>

namespace obl::ui::media_files {

void RequireWindowedFile(const std::string& path)
{
    if (config::Get().headless) return;
    std::string error;
    if (media_pack::CheckReadable(path, error)) return;
    game::FatalError("%s", error.c_str());
    std::abort(); // FatalError exits the process
}

} // namespace obl::ui::media_files
