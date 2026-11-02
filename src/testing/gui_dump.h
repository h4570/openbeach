#pragma once

#include "core/char16.h"
#include <string>
#include <vector>

namespace obl::test {
void InitGuiDump(const Text16& gameDirectory);
// gui-dump: the GUI object graph of the loaded script (widgets, dialogs, main container,
// the open-dialogs list) read through the ui:: records, written to <game>/gui-dump.json for tools/check_gui_dump.py.
bool ExecuteGuiDumpCommand(const std::string& name, const std::vector<std::string>& args);
} // namespace obl::test
