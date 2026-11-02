#include "platform/mp3_length_scan.h"

#include "core/call_counter.h"

namespace obl::platform {
namespace {

// Counts scans (`call-report ui.mp3-scan`).
test::CallCounter g_scan("ui.mp3-scan");

} // namespace

void ScanMp3Length(mp3::Mp3Info& info, const char* directory, const char* file)
{
    g_scan.Count();
    mp3::Init(info);
    mp3::ScanFile(info, directory, file);
}

} // namespace obl::platform
