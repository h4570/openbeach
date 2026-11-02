#include "game/map/grid_class_map.h"

#include "game/map/grid_state.h"
#include "game/ui/file_output.h"

#include <cstdint>

namespace obl::map {
namespace {

struct FileEnv {
    void Write(const char* path, const uint32_t* words, uint32_t bytes) { platform::FileWriteAll(path, words, bytes); }
};

} // namespace

void Grid::SaveTileClassMap() const
{
    FileEnv env;
    grid_class_map::Save(*this, env);
}

} // namespace obl::map
