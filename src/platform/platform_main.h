#pragma once

namespace obl::platform {

// The process start-up after InitGame (docs/architecture/portable-host.md): RunGame runs the static initializers and WinMain
// and returns WinMain's result, which the host returns from main. RegisterPlatformMainReports registers its test reports.
bool RegisterPlatformMainReports();

int RunGame();

} // namespace obl::platform
