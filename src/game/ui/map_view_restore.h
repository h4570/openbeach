#pragma once

#include "game/ui/map_view.h"
#include "game/ui/map_restore.h"

#include <cstdint>

#include <string>
#include <vector>

namespace obl::ui {

bool ExecuteMapRestoreCommand(const std::string& name, const std::vector<std::string>& args);

}
